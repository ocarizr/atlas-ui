/**
 * @file
 * @brief Strict workload benchmark command-line parsing.
 */

#include "benchmark.hpp"

#include <charconv>
#include <string_view>

namespace atlas::jobs::workloads
{
namespace
{
/// @brief Parse a positive representable workload size from command-line text.
std::size_t number(std::string_view text)
{
    std::size_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::invalid_argument{"Expected an unsigned decimal argument"};
    }

    return result;
}
} // namespace

/// @brief Parse and validate benchmark command-line sizes, seeds and execution policy.
benchmark_options parse_options(int argc, char** argv)
{
    benchmark_options result;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--smoke")
        {
            result = {{257, 32, 4, 42}, 2, 1};
            continue;
        }

        if (index + 1 == argc)
        {
            throw std::invalid_argument{"A benchmark option needs a value"};
        }

        if (argument == "--machine")
        {
            result.machine = argv[++index];
            continue;
        }

        const auto value = number(argv[++index]);
        if (argument == "--size")
        {
            result.workload.size = value;
        }
        else if (argument == "--grain")
        {
            result.workload.grain = value;
        }
        else if (argument == "--rounds")
        {
            result.workload.rounds = value;
        }
        else if (argument == "--seed")
        {
            result.workload.seed = value;
        }
        else if (argument == "--workers")
        {
            result.workers = value;
        }
        else if (argument == "--repetitions")
        {
            result.repetitions = value;
        }
        else
        {
            throw std::invalid_argument{"Unknown benchmark option"};
        }
    }

    if (!result.workers || result.workers > 256 || !result.workload.grain ||
        !result.workload.rounds || result.workload.rounds > 1048576 ||
        result.workload.size > 4194304 || !result.repetitions || result.repetitions > 100 ||
        result.workload.grain > 4194304)
    {
        throw std::invalid_argument{"Benchmark arguments exceed their documented bounds"};
    }

    return result;
}
} // namespace atlas::jobs::workloads
