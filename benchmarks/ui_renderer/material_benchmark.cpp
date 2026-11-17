/** @file @brief Compare sampled and shared-time material animation through Direct3D 12. */
#include "material_workload.hpp"
#include <algorithm>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui_renderer/paint_renderer.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace atlas::ui_renderer::benchmarks
{
namespace
{
using clock_type = std::chrono::steady_clock;          ///< Coordinator wall time.
constexpr renderer::surface_extent extent{1920, 1080}; ///< Offscreen render size.
constexpr unsigned warmups = 6;                        ///< Warm the render path before sampling.

/// @brief Keep hardware selection and native validation explicit in the report.
renderer::device_options device_options(bool warp)
{
    renderer::device_options options;
    options.adapter = warp ? renderer::adapter_preference::software
                           : renderer::adapter_preference::prefer_hardware;
    options.debug = renderer::debug_validation::disabled;
    return options;
}

/// @brief Compare RGBA8 output, allowing one code value for periodic floating-point evaluation.
bool pixels_match(std::span<const std::byte> expected, std::span<const std::byte> actual)
{
    return std::ranges::equal(expected, actual,
                              [](std::byte a, std::byte b)
                              {
                                  return std::abs(std::to_integer<int>(a) -
                                                  std::to_integer<int>(b)) <= 1;
                              });
}

/// @brief Report tails without setting timing thresholds in CI.
void report(std::string_view name, std::vector<double> samples)
{
    if (samples.empty())
    {
        std::cout << ' ' << name << "=unavailable";
        return;
    }

    std::ranges::sort(samples);
    const auto percentile = [&](double fraction)
    {
        return samples.at(
            static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(samples.size()))) -
            1);
    };
    std::cout << ' ' << name << "_p50=" << percentile(0.50) << ' ' << name
              << "_p95=" << percentile(0.95) << ' ' << name << "_p99=" << percentile(0.99);
}

/// @brief Own one shader, render target and persistent worker pool for all observations.
struct probe
{
    /// @brief Collect fenced GPU timestamps when instrumentation was compiled in.
    probe(bool warp, const diagnostics::services& sinks)
        : device{device_options(warp), {}, sinks}, adapter{resources, passes, {}, sinks},
          target{resources.create_texture(
              {extent.width, extent.height, renderer::texture_role::color_target})}
    {
    }

    jobs::job_system workers{4};                ///< CPU layout/paint uses the application pool.
    renderer::device device;                    ///< Actual graphics adapter.
    renderer::resource_store resources{device}; ///< Retain resources until completion.
    renderer::pass_renderer passes{resources};  ///< Timestamp the native graphics pass.
    paint_renderer adapter;                     ///< Regular production UI submission path.
    renderer::texture_handle target;            ///< Reused offscreen color attachment.
};

/// @brief Publish sampled values into the tree and require unchanged geometry and order.
ui::paint_snapshot sample_packet(ui::element_tree& tree, ui::paint_engine& paint,
                                 const ui::layout_snapshot& geometry,
                                 std::span<const ui::element_handle> cards, scenario shape,
                                 float phase)
{
    for (std::size_t index = 0; index < cards.size(); ++index)
    {
        tree.set_material(cards[index], card_material(shape, {.card = index, .time = phase}));
    }

    auto packet = paint.calculate(tree.snapshot(), geometry);
    if (packet.revision() != geometry.revision() || packet.quads().size() != cards.size())
    {
        throw std::logic_error("Material animation invalidated fixed geometry");
    }

    return packet;
}

/// @brief Verify GPU shared time matches sampled time, including wrap, seek and replay.
void verify_pixels(probe& host, ui::element_tree& tree, ui::paint_engine& paint,
                   const ui::layout_snapshot& geometry, std::span<const ui::element_handle> cards,
                   scenario shape, const ui::paint_snapshot& cached)
{
    std::vector<std::byte> replay;
    for (const float phase : {0.9375F, 0.5F, 0.9375F})
    {
        const auto sampled = sample_packet(tree, paint, geometry, cards, shape, phase);
        host.resources.wait(host.adapter.submit(sampled, host.target, extent));
        const auto expected = host.resources.readback(host.target);
        host.resources.wait(host.adapter.submit_quads({cached.viewport(), cached.quads(), phase},
                                                      host.target, extent));
        auto actual = host.resources.readback(host.target);
        if (!pixels_match(expected, actual) || (phase == 0.5F && replay == actual) ||
            (phase == 0.9375F && !replay.empty() && replay != actual))
        {
            throw std::logic_error("Shared and sampled material clocks produced different pixels");
        }

        if (replay.empty())
        {
            replay = std::move(actual);
        }
    }
}

/// @brief Retain independent CPU and GPU intervals for one material update policy.
struct samples
{
    std::vector<double> prepare; ///< Owner sampling and paint packet creation.
    std::vector<double> submit;  ///< Adapter CPU preparation, upload submission and recording.
    std::vector<double> gpu;     ///< Fence-completed pass timestamps.
};

struct frame_input
{
    float shared_phase;         ///< Zero after owner sampling, explicit clock for shared time.
    double prepare_us;          ///< CPU time that produced or acquired the packet.
    std::size_t expected_draws; ///< Expected batches from the fixture's material pattern.
}; ///< Submission inputs and observations for one frame.

/// @brief Exercise immutable shared time or per-element sampling through the same pass adapter.
void measure_frame(probe& host, const ui::paint_snapshot& packet, frame_input frame,
                   samples* result)
{
    const auto before = host.passes.gpu_timing().completed_frames;
    const auto start = clock_type::now();
    const auto done = host.adapter.submit_quads(
        {packet.viewport(), packet.quads(), frame.shared_phase}, host.target, extent);
    const double submit_us =
        std::chrono::duration<double, std::micro>(clock_type::now() - start).count();
    host.resources.wait(done);
    host.passes.collect();
    host.resources.collect();
    const auto timing = host.passes.gpu_timing();
    if (host.adapter.last_draws() != frame.expected_draws ||
        host.adapter.last_vertex_bytes() != packet.quads().size() * 6 * sizeof(renderer::vertex))
    {
        throw std::logic_error("Material draw batching or vertex upload changed");
    }

    if (result != nullptr)
    {
        result->prepare.push_back(frame.prepare_us);
        result->submit.push_back(submit_us);
        if (timing.completed_frames > before)
        {
            result->gpu.push_back(timing.last_duration_us);
        }
    }
}

struct workload_size
{
    unsigned cards;  ///< Number of painted nodes.
    unsigned frames; ///< Recorded frames for each policy.
}; ///< Select the smoke or full benchmark workload.

/// @brief Match layouts and use known rectangle coverage to compare rasterization pressure.
void run(probe& host, scenario shape, workload_size size)
{
    ui::element_tree tree;
    const auto cards = populate(tree, shape, size.cards);
    ui::layout_engine layout{host.workers.dispatcher()};
    const auto geometry = layout.update(
        tree, {0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
    ui::paint_engine paint{host.workers.dispatcher()};
    const auto cached = paint.calculate(tree.snapshot(), geometry);
    const auto expected_draws = shape.staggered ? cards.size() : 1U;
    if (cached.quads().size() != cards.size())
    {
        throw std::logic_error("Material workload no longer has known coverage");
    }

    for (std::size_t index = 0; index < cached.quads().size(); ++index)
    {
        const auto& quad = cached.quads()[index];
        if (quad.target != cards[index] || quad.bounds != card_bounds(shape, index) ||
            ui::intersect_clip(quad.bounds, quad.clip) != quad.bounds)
        {
            throw std::logic_error("Material workload changed order or rectangle coverage");
        }
    }

    verify_pixels(host, tree, paint, geometry, cards, shape, cached);
    std::array<samples, 2> measured;
    for (unsigned frame = 0; frame < warmups + size.frames; ++frame)
    {
        const auto phase = static_cast<float>(frame % 64) / 64.0F;
        for (unsigned step = 0; step < measured.size(); ++step)
        {
            const auto index = (step + frame) % measured.size();
            const auto start = clock_type::now();
            const auto packet =
                index == 0 ? sample_packet(tree, paint, geometry, cards, shape, phase) : cached;
            const double prepare_us =
                std::chrono::duration<double, std::micro>(clock_type::now() - start).count();
            auto* retained = frame >= warmups ? &measured.at(index) : nullptr;
            measure_frame(host, packet, {index == 0 ? 0.0F : phase, prepare_us, expected_draws},
                          retained);
        }
    }

    constexpr auto card_area = card_width * card_height;
    std::cout << shape.name << " cards=" << size.cards << " draws=" << expected_draws
              << " vertex_bytes=" << host.adapter.last_vertex_bytes()
              << " submitted_pixels=" << size.cards * card_area
              << " unique_rectangle_pixels=" << size.cards / shape.layers * card_area
              << " rectangle_overdraw=" << shape.layers << '\n';
    for (auto& result : measured)
    {
        if (result.gpu.size() != (diagnostics::instrumentation_enabled ? size.frames : 0U))
        {
            throw std::logic_error("Material GPU samples did not complete");
        }

        std::cout << (&result == &measured.front() ? "sampled" : "shared");
        report("cpu_prepare_us", std::move(result.prepare));
        report("cpu_submit_us", std::move(result.submit));
        report("gpu_pass_us", std::move(result.gpu));
        std::cout << '\n';
    }
}
} // namespace
} // namespace atlas::ui_renderer::benchmarks

/// @brief Run on hardware by default and on WARP for reproducible smoke coverage.
int main(int argc, char** argv)
{
    using namespace atlas::ui_renderer::benchmarks;
    try
    {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (argc > 2 || (argc == 2 && mode != "--warp" && mode != "--smoke"))
        {
            throw std::invalid_argument("use --warp, --smoke or no arguments");
        }

        const bool smoke = mode == "--smoke";
        const auto capture = std::make_shared<atlas::diagnostics::profiler>();
        const atlas::diagnostics::services sinks{{}, capture};
        probe host{!mode.empty(), sinks};
        const auto info = host.device.info();
        std::cout << "adapter=" << info.adapter_name << " software=" << info.software
                  << " compiler=" << ATLAS_MATERIAL_COMPILER << " config=" << ATLAS_MATERIAL_BUILD
                  << " profiling=" << atlas::diagnostics::instrumentation_enabled
                  << " viewport=" << extent.width << 'x' << extent.height
                  << " samples=" << (smoke ? 2 : 64) << " warmups=" << warmups << '\n';
        for (const auto shape :
             {scenario{"uniform_grid", 1, false}, scenario{"staggered_grid", 1, true},
              scenario{"uniform_overlap", 4, false}})
        {
            run(host, shape, {.cards = smoke ? 8U : 192U, .frames = smoke ? 2U : 64U});
        }

        run(host, {"overlap_baseline", 1, false},
            {.cards = smoke ? 2U : 48U, .frames = smoke ? 2U : 64U});

        return capture->statistics().errors == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        (void)std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    catch (...)
    {
        (void)std::fputs("Unclassified material benchmark failure\n", stderr);
        return 1;
    }
}
