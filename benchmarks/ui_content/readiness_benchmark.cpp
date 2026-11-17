/** @file @brief Measured UI owner frames with an exact sequential reference. */
#include "../ui/layout_probe.hpp"
#include "readiness_scenarios.hpp"
#include <algorithm>
#include <atlas/ui/paint_engine.hpp>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace atlas::ui_content::readiness
{
namespace
{
using clock_type = std::chrono::steady_clock;         ///< Monotonic owner-thread wall time.
constexpr ui::layout_rect viewport{0, 0, 1920, 1080}; ///< Common logical resolution.
constexpr std::size_t warmups = 5;                    ///< Unretained frames per policy.

/// @brief Separate compositor work from host startup and optional trace capture.
struct sample
{
    double us = 0;               ///< Owner frame wall time through paint completion.
    std::size_t arranged = 0;    ///< Nodes recalculated in the most recent layout.
    std::size_t layout_jobs = 0; ///< Joined layout admissions.
    std::size_t paint_jobs = 0;  ///< Joined paint admissions.
};

/// @brief Stable asset identity and one real, in-memory streamer mount for all scenarios.
struct fixture
{
    assets::asset_ref source{{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), 1},
                             assets::asset_type::material}; ///< Shared shape template.
    assets::asset_ref model{{source.id.space, 2}, assets::asset_type::view_model}; ///< Frame state.
    assets::asset_ref script{{source.id.space, 3}, assets::asset_type::script}; ///< UI dispatcher.
    jobs::job_system pool{4};          ///< Fixed worker count; outlives the mount and all frames.
    streaming::catalog_streamer mount; ///< Typed material source for private compositions.

    /// @brief Build a typed source without adding permanent assets to either application.
    fixture() : mount{make_mount()}
    {
    }

    /// @brief Use one canonical rounded shape shared by all painted rows.
    static std::string material()
    {
        material_template shape;
        shape.defaults.kind = ui::material_kind::shape;
        shape.defaults.radius = {0.2F, 0.2F, 0.2F, 0.2F};
        return write_material_xml(shape);
    }

    /// @brief Supply typed material, ViewModel and Lua bytes through the streamer boundary.
    streaming::catalog_streamer make_mount()
    {
        assets::catalog_document catalog{source.id.space, 4};
        streaming::storage_mount backing;
        const auto add = [&](assets::asset_ref ref, const std::string& file, std::string_view bytes)
        {
            catalog.records.push_back({ref, {"readiness", file}, 1, bytes.size()});
            backing.sources.emplace(
                file, streaming::memory_asset{std::make_shared<const streaming::asset_bytes>(
                          bytes.begin(), bytes.end())});
            backing.routes.push_back({ref, 1, file, 0});
        };
        add(source, "shape.xml", material());
        add(model, "frame.xml",
            "<ViewModel version='1' name='frame'><Fields>"
            "<Field name='ticks' type='integer' value='0'/></Fields></ViewModel>");
        add(script, "frame.lua",
            "local ticks = model.field('ticks'); "
            "return {tick=function() model.add_integer(ticks,1) end}");
        return {pool, assets::catalog::compile(std::move(catalog)), std::move(backing)};
    }
};

/// @brief Run both policies on independent compositions with the same authored values.
class policy
{
  public:
    /// @brief Initialize caches and publish input geometry outside the timed interval.
    policy(const document& source, const construction& options, const jobs::job_dispatcher& workers,
           update change, bool parallel)
        : view_{instance::build(source, options)},
          layout_{workers, ui::layout_limits{4096, 256, parallel ? 8U : 1U, parallel ? 128U : 0U},
                  options.memory, options.diagnostics},
          paint_{workers,
                 ui::paint_limits{4096, 128, 8,
                                  parallel ? ui::paint_execution::chunks
                                           : ui::paint_execution::sequential},
                 options.memory, options.diagnostics},
          geometry_{layout_.update(view_->tree(), viewport)}, change_{change}
    {
        start(*view_, change);
    }

    /// @brief Time frame dispatch, ordered writes, layout publication and paint packet replacement.
    sample frame(std::size_t index)
    {
        const auto begin = clock_type::now();
        dispatch(*view_, geometry_, index + 2);
        auto bounds = viewport;
        if (change_ == update::resize && index % 2 == 0)
        {
            bounds.width -= 160;
        }

        auto& tree = view_->tree();
        const auto previous_revision = geometry_.revision();
        geometry_ = layout_.update(tree, bounds);
        if ((change_ == update::material || change_ == update::drawer) &&
            geometry_.revision() != previous_revision)
        {
            throw std::runtime_error("A paint-only animation invalidated layout");
        }

        packet_ = paint_.calculate(tree.snapshot(), geometry_);
        const auto end = clock_type::now();
        const auto layout = layout_.statistics();
        return {std::chrono::duration<double, std::micro>(end - begin).count(),
                layout.arranged_nodes, layout.admitted_jobs, paint_.statistics().admitted_jobs};
    }

    /// @brief Check numeric geometry and complete painter order across owner identities.
    void check(const policy& reference, std::size_t expected_ticks) const
    {
        const auto geometry_value = [](ui::layout_item item)
        {
            item.target.session = 0;
            return item;
        };
        const auto quad_value = [](ui::paint_quad quad)
        {
            quad.target.session = 0;
            return quad;
        };
        const auto& model = view_->model("frame");
        if (model.integer(model.description().field("ticks")) !=
                static_cast<std::int64_t>(expected_ticks) ||
            packet_.quads().empty() ||
            !std::ranges::equal(geometry_.items(), reference.geometry_.items(),
                                std::ranges::equal_to{}, geometry_value, geometry_value) ||
            !std::ranges::equal(packet_.quads(), reference.packet_.quads(), std::ranges::equal_to{},
                                quad_value, quad_value))
        {
            throw std::runtime_error("Readiness frame differs from the sequential reference");
        }
    }

  private:
    std::unique_ptr<instance> view_; ///< Owner-affine composition and ordered command source.
    ui::layout_engine layout_;       ///< Policy-specific persistent geometry cache.
    ui::paint_engine paint_;         ///< Policy-specific paint scheduler.
    ui::layout_snapshot geometry_;   ///< Input geometry for the next frame.
    ui::paint_snapshot packet_;      ///< Retain one consumer packet between frames.
    update change_;                  ///< Scenario mutation applied on the owner.
};

/// @brief Emit tails and observed scheduling maxima without imposing timing assertions in CI.
void report(std::string_view name, std::span<const sample> samples)
{
    std::vector<double> times;
    std::size_t arranged = 0;
    std::size_t layout_jobs = 0;
    std::size_t paint_jobs = 0;
    for (const auto value : samples)
    {
        times.push_back(value.us);
        arranged = std::max(arranged, value.arranged);
        layout_jobs = std::max(layout_jobs, value.layout_jobs);
        paint_jobs = std::max(paint_jobs, value.paint_jobs);
    }

    std::ranges::sort(times);
    const auto percentile = [&](double fraction)
    {
        return times.at(static_cast<std::size_t>(std::ceil(fraction * double(times.size()))) - 1);
    };
    std::cout << name << " p50_us=" << percentile(0.50) << " p95_us=" << percentile(0.95)
              << " p99_us=" << percentile(0.99) << " max_arranged=" << arranged
              << " max_layout_jobs=" << layout_jobs << " max_paint_jobs=" << paint_jobs << '\n';
}

/// @brief Run an untimed, scoped PMR census and demand complete resource retirement.
void census(const document& source, construction options, const jobs::job_dispatcher& workers,
            update change, bool parallel)
{
    const auto memory = std::make_shared<ui::benchmarks::probe_resource>();
    options.memory = memory;
    {
        policy measured{source, options, workers, change, parallel};
        (void)measured.frame(0);
        const auto retained = memory->live.load();
        memory->reset();
        (void)measured.frame(1);
        std::cout << (parallel ? "candidate" : "serial") << " pmr_retained_before=" << retained
                  << " pmr_peak=" << memory->peak.load() << " pmr_calls=" << memory->calls.load()
                  << " pmr_retained_after=" << memory->live.load() << '\n';
    }

    if (memory->live.load() != 0)
    {
        throw std::runtime_error("Readiness frame leaked host PMR allocations");
    }
}

/// @brief Reopen an authored document, alternate policy order and check every complete frame.
void run(fixture& assets, scenario shape, std::size_t count, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {shape.name, "readiness"}};
    const auto authored = make_document(
        shape, {.material = assets.source, .model = assets.model, .script = assets.script});
    const auto xml = write_xml(authored, sinks);
    const auto source = parse_xml({xml}, sinks);
    if (write_xml(source) != xml)
    {
        throw std::runtime_error("Readiness authoring round-trip mismatch");
    }

    const construction options{.storage = assets.mount, .diagnostics = sinks};
    const auto workers = assets.pool.dispatcher();
    policy serial{source, options, workers, shape.change, false};
    policy parallel{source, options, workers, shape.change, true};
    std::array<std::vector<sample>, 2> samples;
    const std::array policies{&serial, &parallel};
    for (std::size_t frame = 0; frame < count + warmups; ++frame)
    {
        for (std::size_t step = 0; step < policies.size(); ++step)
        {
            const auto index = (frame + step) % policies.size();
            const auto value = policies.at(index)->frame(frame);
            if (frame >= warmups)
            {
                samples.at(index).push_back(value);
            }
        }

        serial.check(serial, frame + 1);
        parallel.check(serial, frame + 1);
    }

    if (!sinks.profile)
    {
        std::cout << shape.name << " nodes=" << source.elements.size() << '\n';
        report("serial", samples.front());
        report("candidate", samples.back());
        census(source, options, workers, shape.change, false);
        census(source, options, workers, shape.change, true);
    }
}
} // namespace
} // namespace atlas::ui_content::readiness

/// @brief Run deterministic smoke, host measurements or a separate Chrome trace.
int main(int argc, char** argv)
{
    using namespace atlas::ui_content::readiness;
    try
    {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (argc > 2 || (argc == 2 && mode != "--smoke" && mode != "--trace"))
        {
            throw std::invalid_argument("use --smoke, --trace or no arguments");
        }

        const bool trace = mode == "--trace";
        if (trace && !atlas::diagnostics::instrumentation_enabled)
        {
            throw std::invalid_argument("trace requires profiling instrumentation");
        }

        fixture assets;
        const auto capture = trace ? std::make_shared<atlas::diagnostics::profiler>() : nullptr;
        const atlas::diagnostics::services sinks{{}, capture};
        const std::size_t count = mode.empty() ? 100 : 3;
        const std::size_t panels = mode.empty() ? 24 : 4;
        const std::size_t rows = mode.empty() ? 96 : 36;
        if (!trace)
        {
            std::cout << "compiler=" << ATLAS_READINESS_COMPILER
                      << " config=" << ATLAS_READINESS_BUILD << " samples=" << count
                      << " warmups=" << warmups << " workers=4 viewport=1920x1080"
                      << " jobs_checker=" << assets.pool.sanitizer_report().enabled << '\n';
        }

        for (const auto shape : std::array{scenario{"idle_menu", 2, 12, update::idle},
                                           scenario{"idle_preview", panels, rows, update::idle},
                                           scenario{"resize_preview", panels, rows, update::resize},
                                           scenario{"shape_glow", panels, rows, update::material},
                                           scenario{"clipped_drawer", panels, rows, update::drawer},
                                           scenario{"layout_reflow", panels, rows, update::reflow}})
        {
            run(assets, shape, count, sinks);
        }

        if (capture)
        {
            if (capture->statistics().dropped != 0 || capture->statistics().errors != 0)
            {
                throw std::runtime_error("Readiness trace was incomplete");
            }

            capture->write_chrome_trace(std::cout);
        }

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
        (void)std::fputs("Unclassified UI readiness benchmark failure\n", stderr);
        return 1;
    }
}
