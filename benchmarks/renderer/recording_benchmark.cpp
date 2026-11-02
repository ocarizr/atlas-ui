/** @file @brief Reproducible CPU submission timing across sequential and coarse recording policies.
 */
#include <algorithm>
#include <atlas/jobs/job_system.hpp>
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string_view>

namespace
{
/// @brief Named workload dimensions avoid interchangeable count parameters.
struct workload_size
{
    std::size_t draws;  ///< Total draws in a frame.
    std::size_t passes; ///< Number of ordered passes.
};

/// @brief Small GPU footprint isolates CPU recording without claiming GPU performance.
struct workload
{
    atlas::renderer::resource_store resources; ///< Coordinator-owned GPU data.
    atlas::renderer::pass_renderer renderer;   ///< Configured large frame admission.
    atlas::renderer::texture_handle target;    ///< One small offscreen output.
    atlas::renderer::buffer_handle geometry;   ///< Repeated immutable primitive.
    atlas::renderer::pipeline_handle pipeline; ///< Repeated immutable pipeline.

    /// @brief Allocate reusable geometry once outside measured submissions.
    explicit workload(atlas::renderer::device& device)
        : resources(device), renderer(resources, {4, 16, 65536, 2, 8})
    {
        using namespace atlas::renderer;
        target = resources.create_texture({4, 4, texture_role::color_target});
        const std::array<vertex, 3> vertices{{{{-1, -1, 0}, {1, 1, 1, 1}, {}},
                                              {{-1, 3, 0}, {1, 1, 1, 1}, {}},
                                              {{3, -1, 0}, {1, 1, 1, 1}, {}}}};
        geometry = resources.create_buffer({sizeof(vertices)});
        resources.wait(resources.upload(geometry, std::as_bytes(std::span{vertices})));
        pipeline = renderer.create_pipeline({});
    }

    /// @brief Build immutable input outside the measured interval.
    atlas::renderer::frame_plan frame(workload_size size) const
    {
        using namespace atlas::renderer;
        auto descriptions = std::vector<pass_description>(size.passes);
        for (std::size_t index = 0; index < size.passes; ++index)
        {
            auto& pass = descriptions[index];
            pass.color.target = target;
            pass.color.load = index == 0 ? load_action::clear : load_action::load;
            for (std::size_t draw = index; draw < size.draws; draw += size.passes)
            {
                draw_packet packet;
                packet.pipeline = pipeline;
                packet.vertices = geometry;
                packet.vertex_count = 3;
                pass.draws.push_back(packet);
            }
        }

        return frame_plan{descriptions};
    }
};

/// @brief Report median CPU validation/record/join/submit time, excluding explicit GPU waits.
void measure(workload& work, const atlas::renderer::frame_plan& frame,
             atlas::renderer_jobs::executor& executor, unsigned batches)
{
    std::vector<double> samples;
    for (unsigned iteration = 0; iteration < 9; ++iteration)
    {
        work.renderer.collect();
        work.resources.collect();
        const auto start = std::chrono::steady_clock::now();
        const auto done = batches == 1
                              ? work.renderer.submit(frame)
                              : work.renderer.submit(frame, executor, {1024, batches, true});
        const auto end = std::chrono::steady_clock::now();
        work.resources.wait(done);
        if (iteration >= 2)
        {
            samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        }
    }

    std::sort(samples.begin(), samples.end());
    const auto statistics = work.renderer.last_recording();
    std::cout << statistics.draws << ',' << frame.passes().size() << ',' << batches << ','
              << statistics.command_lists << ',' << samples[samples.size() / 2] << '\n';
}
} // namespace

/// @brief Run manually on hardware or WARP; an unavailable backend is an explicit failure.
int main(int argc, char** argv)
{
    using namespace atlas;
    try
    {
        if (!renderer::backend_available())
        {
            std::cerr << "Native renderer unavailable\n";
            return 1;
        }

        renderer::device_options options;
        options.debug = renderer::debug_validation::disabled;
        if (argc == 2 && std::string_view{argv[1]} == "--warp")
        {
            options.adapter = renderer::adapter_preference::software;
        }

        renderer::device graphics{options};
        jobs::job_system pool{4};
        renderer_jobs::executor executor{pool.dispatcher()};
        workload work{graphics};
        std::cout << "adapter=" << graphics.info().adapter_name
                  << "; workers=4; median CPU microseconds; GPU waits excluded\n";
        std::cout << "draws,passes,max_batches,actual_lists,cpu_us\n";
        for (const auto passes : {std::size_t{1}, std::size_t{8}})
        {
            for (const auto draws :
                 {std::size_t{64}, std::size_t{1024}, std::size_t{4096}, std::size_t{16384}})
            {
                const auto frame = work.frame({draws, passes});
                for (const auto batches : {1U, 2U, 4U})
                {
                    measure(work, frame, executor, batches);
                }
            }
        }

        graphics.wait_idle();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
