/** @file @brief Manual loose-asset/job/render handoff measurements, never automatic FPS thresholds.
 */
#include <algorithm>
#include <atlas/memory/memory_resource.hpp>
#include <atlas/render_host/render_loop.hpp>
#include <atlas/renderer_content/gpu_content.hpp>
#include <atlas/renderer_content/loader.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <cstdio>
#include <iostream>

namespace
{
/// @brief Worker-owned samples read by main only after joined shutdown.
struct measurements
{
    std::vector<double> acquisition; ///< Coordinator round-trip including I/O, dispatch and WIC.
    std::vector<double> cpu;         ///< CPU validation/record/join/submission only.
    std::vector<double> gpu; ///< Actual fenced GPU pass intervals, excluding upload/present.
    atlas::streaming::streamer_statistics streaming{}; ///< IOCP/residency observations.
    std::string adapter;                               ///< Actual selected backend.
};

/// @brief Convert an explicit benchmark wall interval into microseconds.
double elapsed(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - start}
        .count();
}

/// @brief Report distributions outside the timed workload; empty GPU samples remain unavailable.
double percentile(std::vector<double> samples, double fraction)
{
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1))];
}

/// @brief One render coordinator shares main's worker pool, streamer boundary and PMR owner.
class measured_handler final : public atlas::render_host::frame_handler
{
  public:
    measured_handler(atlas::jobs::job_system& jobs,
                     const atlas::window::presentation_target& target,
                     const std::filesystem::path& assets, bool warp,
                     const std::shared_ptr<std::pmr::memory_resource>& memory,
                     const atlas::diagnostics::services& destinations, measurements& results)
        : graphics_(device_options(warp), memory, destinations),
          surface_(graphics_, target, {2, false}), resources_(graphics_), passes_(resources_),
          streamer_(jobs, {{"image", atlas::streaming::loose_file_asset{assets / "checker.bmp"}}},
                    {}, memory, destinations),
          loader_(streamer_, atlas::renderer_content::make_image_decoder(), memory, destinations),
          recording_(jobs.dispatcher()), cache_(passes_), results_(results),
          destinations_(destinations)
    {
        results_.adapter = graphics_.info().adapter_name;
        const auto extent = target.state();
        output_ = resources_.create_texture(
            {extent.width, extent.height, atlas::renderer::texture_role::color_target});
        const std::array<atlas::renderer::vertex, 3> vertices{{{{-1, -1, 0}, {1, 1, 1, 1}, {0, 1}},
                                                               {{-1, 3, 0}, {1, 1, 1, 1}, {0, -1}},
                                                               {{3, -1, 0}, {1, 1, 1, 1}, {2, 1}}}};
        geometry_ = resources_.create_buffer({sizeof(vertices)});
        resources_.upload(geometry_, std::as_bytes(std::span{vertices}));
        atlas::renderer::pipeline_description pipeline;
        pipeline.program = atlas::renderer::shader_program::textured;
        pipeline_ = cache_.get(pipeline);
    } ///< Compile once before timed samples and keep graphics lifetime on render.

    void render(const atlas::render_host::frame_snapshot& frame) override
    {
        const auto read_start = std::chrono::steady_clock::now();
        auto request = loader_.read_image({"image", 0, 70});
        const auto image =
            request.get(); // Deliberate coordinator round-trip measurement, never a worker wait.
        const double read_us = elapsed(read_start);
        atlas::renderer_content::texture_upload upload{resources_, image, destinations_};
        graphics_.wait_idle();
        const auto binding = upload.binding();
        atlas::renderer::draw_packet draw;
        draw.pipeline = pipeline_;
        draw.vertices = geometry_;
        draw.texture = binding.view;
        draw.vertex_count = 3;
        atlas::renderer::pass_description pass;
        pass.color.target = output_;
        pass.draws.assign(64, draw);
        const std::array descriptions{pass};
        const atlas::renderer::frame_plan plan{descriptions};
        const auto cpu_start = std::chrono::steady_clock::now();
        const auto done = passes_.submit(plan, recording_);
        const double cpu_us = elapsed(cpu_start);
        resources_.wait(done);
        passes_.collect();
        const auto gpu = passes_.gpu_timing();
        (void)surface_.present(resources_, output_);
        graphics_.wait_idle();
        if (frame.id > 16)
        {
            results_.acquisition.push_back(read_us);
            results_.cpu.push_back(cpu_us);
            if (gpu.completed_frames != 0)
            {
                results_.gpu.push_back(gpu.last_duration_us);
            }
        }
    } ///< Sixteen warmups, sixty-four measurements; GPU waits are outside CPU submission timing.

    void shutdown() override
    {
        graphics_.wait_idle();
        passes_.collect();
        streamer_.shutdown();
        results_.streaming = streamer_.statistics();
    } ///< Snapshot observations only after all accepted work has drained.
  private:
    static atlas::renderer::device_options device_options(bool warp)
    {
        atlas::renderer::device_options policy;
        policy.debug = atlas::renderer::debug_validation::disabled;
        if (warp)
        {
            policy.adapter = atlas::renderer::adapter_preference::software;
        }

        return policy;
    } ///< Benchmark native validation separately from correctness suites.

    atlas::renderer::device graphics_;               ///< Thread-affine GPU owner.
    atlas::renderer::presentation_surface surface_;  ///< Main keeps the native window pumping.
    atlas::renderer::resource_store resources_;      ///< Bounded resources and upload staging.
    atlas::renderer::pass_renderer passes_;          ///< Generic ordered pass submission.
    atlas::streaming::asset_streamer streamer_;      ///< Sole loose-file boundary.
    atlas::renderer_content::content_loader loader_; ///< Ready-job WIC preparation.
    atlas::renderer_jobs::executor recording_;       ///< Existing application pool.
    atlas::renderer_content::pipeline_cache cache_;  ///< One device-local PSO.
    measurements& results_;                          ///< Main observes after join.
    atlas::diagnostics::services destinations_;      ///< Optional GPU/CPU capture.
    atlas::renderer::texture_handle output_;         ///< Fixed 240x180 target.
    atlas::renderer::buffer_handle geometry_;        ///< One full-screen triangle.
    atlas::renderer::pipeline_handle pipeline_;      ///< Borrowed cache-owned pipeline.
};
} // namespace

/// @brief No production telemetry is collected; execution and capture are explicit benchmark
/// choices.
int main(int argc, char** argv)
{
    try
    {
        if (argc < 2 || argc > 3)
        {
            throw std::invalid_argument(
                "usage: atlas_renderer_readiness_benchmark <asset-directory> [--warp]");
        }

        if (!atlas::renderer::backend_available())
        {
            throw std::runtime_error("native graphics unavailable");
        }

        const bool warp = argc == 3 && std::string_view{argv[2]} == "--warp";
        const std::filesystem::path assets = argv[1];
        auto memory = atlas::memory::make_resource({true});
        auto capture = std::make_shared<atlas::diagnostics::profiler>();
        atlas::jobs::job_system_options job_options;
        job_options.worker_count = 4;
        job_options.scratch.upstream = memory.get();
        atlas::jobs::job_system jobs{job_options};
        atlas::window::window target{{"Readiness benchmark", 240, 180, false}};
        const auto lease = target.share_presentation();
        measurements results;
        atlas::render_host::render_loop loop{
            [&]
            {
                return std::make_unique<measured_handler>(jobs, lease, assets, warp, memory,
                                                          atlas::diagnostics::services{{}, capture},
                                                          results);
            }};
        const auto pump = [&]
        {
            target.poll_events();
            target.wait_for_events(1);
        };
        for (std::uint64_t frame = 1; frame <= 80 && !loop.finished();)
        {
            if (loop.try_submit({frame, target.poll_events()}))
            {
                ++frame;
            }

            pump();
        }

        loop.stop_and_join(pump);
        std::cout << "adapter=" << results.adapter
                  << "; workers=4; draws=64; samples=" << results.cpu.size()
                  << "; profiling=" << atlas::diagnostics::instrumentation_enabled
                  << "; debug_layer=off\n";
        for (const auto& [name, samples] :
             {std::pair{"read_decode_us", &results.acquisition},
              std::pair{"cpu_submit_us", &results.cpu}, std::pair{"gpu_pass_us", &results.gpu}})
        {
            if (samples->empty())
            {
                std::cout << name << ",unavailable\n";
            }
            else
            {
                std::cout << name << ",p50=" << percentile(*samples, 0.5)
                          << ",p95=" << percentile(*samples, 0.95) << '\n';
            }
        }

        std::cout << "pmr_peak_requested_bytes=" << memory->statistics().peak_requested_bytes
                  << "; encoded_peak_bytes=" << results.streaming.peak_resident_bytes
                  << "; peak_io=" << results.streaming.peak_io_in_flight
                  << "; peak_queued_frames=" << loop.statistics().peak_queued << '\n';
        return results.cpu.size() == 64 && capture->statistics().errors == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "Unknown readiness benchmark failure\n");
        return 1;
    }
}
