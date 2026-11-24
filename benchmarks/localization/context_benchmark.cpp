/** @file @brief Change-driven localization latency and separate allocation-request observations. */
#include "../jobs/allocation_counter.hpp"
#include <atlas/localization/context.hpp>
#include <chrono>
#include <cstdio>
#include <iostream>

namespace atlas::localization::benchmarks
{
namespace
{
/// @brief Reproducible language/key tables contain engine-neutral synthetic labels.
definition make_table(unsigned keys)
{
    definition source;
    source.fallback = "en";
    for (const std::string tag : {"en", "pt", "de", "ar"})
    {
        language table;
        table.tag = tag;
        for (unsigned i = 0; i < keys; ++i)
        {
            message entry;
            entry.key = "key" + std::to_string(i);
            entry.text = tag + " translated label " + std::to_string(i);
            table.messages.push_back(std::move(entry));
        }

        source.languages.push_back(std::move(table));
    }

    return source;
}

/// @brief One owner context with pre-bound handles and no-op change listeners.
struct probe
{
    context source;                      ///< Immutable table indices persist across samples.
    std::vector<message_key> keys;       ///< Names resolve only during startup.
    std::vector<subscription> observers; ///< Stable bound listener capacity.
    std::uint64_t notifications = 0;     ///< Scalar observer evidence, never shared with workers.

    explicit probe(definition table, unsigned subscribers) : source(std::move(table))
    {
        for (unsigned i = 0;; ++i)
        {
            try
            {
                keys.push_back(source.key("key" + std::to_string(i)));
            }
            catch (const std::out_of_range&)
            {
                break;
            }
        }

        for (unsigned i = 0; i < subscribers; ++i)
        {
            observers.push_back(source.observe(
                [this](auto)
                {
                    ++notifications;
                }));
        }
    } ///< Startup is excluded from sampled frames.

    void frame(unsigned iteration)
    {
        const bool portuguese = iteration % 2 == 0;
        source.select(portuguese ? "pt-BR" : "en-US");
        for (auto key : keys)
        {
            const auto text = source.resolve(key);
            if (text.locale != (portuguese ? "pt" : "en") || text.text.empty())
            {
                throw std::runtime_error("localization benchmark fallback mismatch");
            }
        }
    } ///< Includes language publication, notification and owned text resolution for every key.
};
} // namespace

/// @brief Means are observations without hardware-specific pass/fail thresholds.
int run(bool smoke)
{
    jobs::workloads::verify_allocation_counter();
    std::cout << "<localization_benchmark profiling_compiled=\""
              << diagnostics::instrumentation_enabled << "\">\n";
    for (unsigned keys : {16U, 1024U})
    {
        probe sample{make_table(keys), 32};
        for (unsigned i = 0; i < 3; ++i)
        {
            sample.frame(i);
        }

        const unsigned rounds = smoke ? 4 : 100;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < rounds; ++i)
        {
            sample.frame(i + 3);
        }

        const double mean =
            std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - start}
                .count() /
            rounds;
        jobs::workloads::begin_allocation_counting();
        sample.frame(rounds + 3);
        const auto allocations = jobs::workloads::end_allocation_counting();
        std::cout << "  <case keys=\"" << keys << "\" languages=\"4\" subscribers=\"32\" mean_us=\""
                  << mean << "\" cpp_calls=\"" << allocations.calls << "\" cpp_requested_bytes=\""
                  << allocations.requested_bytes << "\"/>\n";
        const auto revision = sample.source.revision();
        const auto current_language = sample.source.selected_language();
        jobs::workloads::begin_allocation_counting();
        for (unsigned i = 0; i < 1000; ++i)
        {
            sample.source.select(current_language);
        }

        const auto idle = jobs::workloads::end_allocation_counting();
        if (sample.source.revision() != revision || idle.calls != 0)
        {
            throw std::runtime_error("localization no-op selection allocates or publishes");
        }
    }

    std::cout << "</localization_benchmark>\n";
    return 0;
}
} // namespace atlas::localization::benchmarks

/// @brief Run the measured workload or its bounded correctness smoke without publishing artifacts.
int main(int argc, char** argv)
{
    try
    {
        if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--smoke"))
        {
            throw std::invalid_argument("expected --smoke or no arguments");
        }

        return atlas::localization::benchmarks::run(argc == 2);
    }
    catch (const std::exception& error)
    {
        (void)std::fputs(error.what(), stderr);
        (void)std::fputc('\n', stderr);
        return 1;
    }
    catch (...)
    {
        (void)std::fputs("unclassified localization benchmark failure\n", stderr);
        return 1;
    }
}
