/**
 * @file
 * @brief Behavioral coverage for incremental workload with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"
#include "phased_workload.hpp"

namespace atlas::jobs::tests
{
/// @brief Scenario state for incremental workload tests, isolated per test and released through
/// RAII.
class incremental_workload_tests : public testing::TestWithParam<std::size_t>
{
};

/// @brief Verify sparse updates match full recomputation.
TEST_P(incremental_workload_tests, sparse_updates_match_full_recomputation)
{
    job_system system{GetParam()};
    workloads::workload_config config{257, 7, 8, 42};
    config.dirty_stride = 3;
    auto state = workloads::prepare_workload(config);
    const auto reference = workloads::sequential_reference(*state);
    workloads::submit_workload(system.dispatcher(), state).merge.wait();
    EXPECT_EQ(state->checksum, reference);
    EXPECT_EQ(state->computed.load(), 86U);
}

/// @brief Verify a clean snapshot does not recompute cached values.
TEST_P(incremental_workload_tests, a_clean_snapshot_does_not_recompute_cached_values)
{
    job_system system{GetParam()};
    workloads::workload_config config{257, 7, 8, 42};
    config.dirty_stride = 0;
    auto state = workloads::prepare_workload(config);
    const auto reference = workloads::sequential_reference(*state);
    std::vector<std::uint64_t> cached;
    cached.reserve(state->items.size());
    for (const auto& item : state->items)
    {
        cached.push_back(item.output);
    }

    workloads::submit_workload(system.dispatcher(), state).merge.wait();
    EXPECT_EQ(state->checksum, reference);
    EXPECT_EQ(state->computed.load(), 0U);
    for (std::size_t index = 0; index < cached.size(); ++index)
    {
        EXPECT_EQ(state->items[index].output, cached[index]);
    }
}

INSTANTIATE_TEST_SUITE_P(worker_counts, incremental_workload_tests, testing::Values(1U, 2U, 4U));
} // namespace atlas::jobs::tests
