/** @file @brief Real ordered/clipped rectangle preparation, independent of GPU and file decoding.
 */
#include "../jobs/allocation_counter.hpp"
#include "layout_probe.hpp"
#include <algorithm>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using clock_type = std::chrono::steady_clock; ///< Monotonic coordinator wall time.
using namespace atlas;

struct scenario
{
    const char* name; ///< Stable XML scenario name.
    unsigned roots;   ///< Independent canvases.
    unsigned panels;  ///< Nested stack/overlay groups per canvas.
    unsigned leaves;  ///< Overlapping translucent, textured cards per group.
}; ///< Shapes exercise actual paint kernels rather than unrelated arithmetic.

/// @brief Generic authoring templates use current canvas, overlay and stack contracts.
void populate(ui::element_tree& tree, scenario shape)
{
    for (unsigned root = 0; root < shape.roots; ++root)
    {
        const auto canvas = tree.create();
        tree.set_panel(canvas, ui::canvas_layout{});
        for (unsigned p = 0; p < shape.panels; ++p)
        {
            const auto panel = tree.create(canvas);
            ui::layout_style style;
            style.constraints.minimum = {800, 600};
            style.constraints.maximum = {800, 600};
            tree.set_layout(panel, style);
            ui::paint_style visual;
            visual.clip = p % 3 == 0;
            visual.layer = static_cast<std::int32_t>(p % 5);
            tree.set_paint(panel, visual);
            const auto overlay = tree.create(panel);
            if (p % 3 == 0)
            {
                tree.set_panel(overlay, ui::vertical_layout{2});
            }

            if (p % 3 == 1)
            {
                tree.set_panel(overlay, ui::horizontal_layout{2});
            }

            for (unsigned i = 0; i < shape.leaves; ++i)
            {
                const auto card = tree.create(overlay);
                ui::layout_style card_size;
                card_size.intrinsic = {8, 8};
                card_size.constraints.minimum = {8, 8};
                tree.set_layout(card, card_size);
                visual.fill = true;
                visual.clip = i % 7 == 0;
                visual.translation = {static_cast<float>(i % 13), static_cast<float>(i % 17)};
                visual.layer = static_cast<std::int32_t>(i % 9);
                visual.color = {0.25f, 0.5f, 0.75f, 0.6f};
                visual.texture = i % 4;
                tree.set_paint(card, visual);
            }
        }
    }

    tree.set_viewport({0, 0, 1920, 1080});
}

/// @brief Use nearest-rank quantiles over retained raw samples.
double quantile(std::vector<double> samples, double fraction)
{
    std::sort(samples.begin(), samples.end());
    const auto index =
        static_cast<std::size_t>(std::ceil(static_cast<double>(samples.size()) * fraction)) - 1;
    return samples[index];
}

/// @brief Rotate policy order; validation is outside timing and no threshold is a test assertion.
void measure(scenario shape, jobs::job_system& workers, bool smoke)
{
    const auto capacity = shape.roots * (1 + shape.panels * (2 + shape.leaves));
    ui::element_tree tree{{capacity}};
    populate(tree, shape);
    ui::layout_engine layout{workers.dispatcher()};
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    constexpr std::array modes{ui::paint_execution::sequential, ui::paint_execution::roots,
                               ui::paint_execution::chunks, ui::paint_execution::chunks};
    constexpr std::array names{"serial", "roots", "chunks4", "chunks16"};
    constexpr std::array caps{1u, 8u, 4u, 16u};
    std::array<std::shared_ptr<ui::benchmarks::probe_resource>, modes.size()> memory;
    std::array<std::unique_ptr<ui::paint_engine>, modes.size()> engines;
    std::array<ui::paint_snapshot, modes.size()> last;
    std::array<std::vector<double>, modes.size()> total, capture, calculate, retire;
    for (std::size_t p = 0; p < modes.size(); ++p)
    {
        memory[p] = std::make_shared<ui::benchmarks::probe_resource>();
        engines[p] = std::make_unique<ui::paint_engine>(
            workers.dispatcher(), ui::paint_limits{65536, 1, caps[p], modes[p]}, memory[p]);
    }

    const unsigned retained = smoke ? 2 : 20;
    for (unsigned iteration = 0; iteration < retained + 3; ++iteration)
    {
        for (std::size_t step = 0; step < modes.size(); ++step)
        {
            const auto p = (step + iteration) % modes.size();
            const auto begin = clock_type::now();
            auto input = tree.snapshot();
            const auto captured = clock_type::now();
            auto output = engines[p]->calculate(input, geometry);
            const auto computed = clock_type::now();
            last[p] = std::move(output);
            input = {};
            const auto end = clock_type::now();
            if (iteration >= 3)
            {
                const auto micros = [](auto a, auto b)
                {
                    return std::chrono::duration<double, std::micro>(b - a).count();
                };
                total[p].push_back(micros(begin, end));
                capture[p].push_back(micros(begin, captured));
                calculate[p].push_back(micros(captured, computed));
                retire[p].push_back(micros(computed, end));
            }
        }

        for (std::size_t p = 1; p < modes.size(); ++p)
        {
            if (!std::ranges::equal(last[0].quads(), last[p].quads()))
            {
                throw std::runtime_error("paint benchmark numeric reference mismatch");
            }
        }
    }

    std::cout << "<scenario name=\"" << shape.name << "\" nodes=\"" << tree.size() << "\" quads=\""
              << last[0].quads().size() << "\">\n";
    for (std::size_t p = 0; p < modes.size(); ++p)
    {
        memory[p]->reset();
        jobs::workloads::begin_allocation_counting();
        try
        {
            last[p] = engines[p]->calculate(source, geometry);
        }
        catch (...)
        {
            (void)jobs::workloads::end_allocation_counting();
            throw;
        }

        const auto allocations = jobs::workloads::end_allocation_counting();
        std::cout << "<policy name=\"" << names[p] << "\" p50_us=\"" << quantile(total[p], 0.5)
                  << "\" p95_us=\"" << quantile(total[p], 0.95) << "\" capture_p50_us=\""
                  << quantile(capture[p], 0.5) << "\" calculate_p50_us=\""
                  << quantile(calculate[p], 0.5) << "\" retire_p50_us=\""
                  << quantile(retire[p], 0.5) << "\" pmr_peak_bytes=\"" << memory[p]->peak.load()
                  << "\" pmr_retained_bytes=\"" << memory[p]->live.load() << "\" pmr_calls=\""
                  << memory[p]->calls.load() << "\" cpp_requests=\"" << allocations.calls
                  << "\" cpp_requested_bytes=\"" << allocations.requested_bytes << "\" jobs=\""
                  << engines[p]->statistics().admitted_jobs << "\">\n";
        for (const auto value : total[p])
        {
            std::cout << "<sample us=\"" << value << "\"/>\n";
        }

        std::cout << "</policy>\n";
    }

    std::cout << "</scenario>\n";
}
} // namespace

/// @brief Emit machine-readable measurements; --smoke checks references without speed claims.
int main(int argc, char** argv)
{
    try
    {
        const bool smoke = argc == 2 && std::string_view{argv[1]} == "--smoke";
        if (argc > 2 || (argc == 2 && !smoke))
        {
            throw std::invalid_argument("use --smoke or no arguments");
        }

        atlas::jobs::workloads::verify_allocation_counter();
        atlas::jobs::job_system workers{4};
        std::cout << "<paint_benchmark compiler=\"" << ATLAS_BENCHMARK_COMPILER << "\" system=\""
                  << ATLAS_BENCHMARK_SYSTEM << "\" workers=\"4\" warmup=\"3\" profiling_compiled=\""
                  << atlas::diagnostics::instrumentation_enabled << "\" jobs_checker=\""
                  << workers.sanitizer_report().enabled << "\" pointer_bits=\"" << sizeof(void*) * 8
                  << "\" hardware_threads=\"" << std::thread::hardware_concurrency() << "\">\n";
        constexpr std::array cases{scenario{"small", 1, 2, 8}, scenario{"nested_cards", 1, 16, 256},
                                   scenario{"large_canvas", 1, 32, 512},
                                   scenario{"many_canvases", 8, 4, 256}};
        for (const auto shape : cases)
        {
            measure(shape, workers, smoke);
        }

        std::cout << "</paint_benchmark>\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        (void)std::fputs(error.what(), stderr);
        (void)std::fputc('\n', stderr);
        return 1;
    }
    catch (...)
    {
        (void)std::fputs("unclassified paint benchmark failure\n", stderr);
        return 1;
    }
}
