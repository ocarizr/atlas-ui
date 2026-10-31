#include "job_system_fixture.hpp"
#include "phased_workload.hpp"

namespace atlas::jobs::tests
{
struct phased_case
{
    std::size_t workers;
    std::size_t size;
    std::size_t grain;
    std::uint64_t seed;
};
std::ostream& operator<<(std::ostream& output, const phased_case& value)
{
    return output << "workers=" << value.workers << ", size=" << value.size
                  << ", grain=" << value.grain << ", seed=" << value.seed;
}
class phased_workload_tests : public testing::TestWithParam<phased_case>
{
};

TEST_P(phased_workload_tests,
       incremental_phases_and_ordered_reduction_match_the_sequential_reference)
{
    const auto parameters = GetParam();
    job_system system{parameters.workers};
    auto state =
        workloads::prepare_workload({parameters.size, parameters.grain, 8, parameters.seed});
    const auto reference = workloads::sequential_reference(*state);
    std::vector<std::uint64_t> snapshot;
    snapshot.reserve(state->items.size());
    for (const auto& item : state->items)
    {
        snapshot.push_back(item.input);
    }
    const auto run = workloads::submit_workload(system.dispatcher(), state);
    run.merge.wait();
    EXPECT_EQ(state->checksum, reference);
    EXPECT_EQ(state->mapped.load(), parameters.size);
    EXPECT_EQ(state->folded.load(), parameters.size);
    EXPECT_EQ(state->phase.load(), 3);
    EXPECT_EQ(system.statistics().waiting, 0U);
    for (std::size_t index = 0; index < state->items.size(); ++index)
    {
        EXPECT_EQ(state->items[index].input, snapshot[index]);
    }
    for (const auto& diagnostic : system.sanitizer_report().diagnostics)
    {
        EXPECT_NE(diagnostic.severity, diagnostic_severity::error);
    }
}

INSTANTIATE_TEST_SUITE_P(reproducible_batches, phased_workload_tests,
                         testing::Values(phased_case{1, 0, 8, 7}, phased_case{2, 0, 8, 7},
                                         phased_case{4, 0, 8, 7}, phased_case{1, 17, 1, 42},
                                         phased_case{2, 17, 1, 42}, phased_case{4, 17, 1, 42},
                                         phased_case{1, 257, 7, 2026}, phased_case{2, 257, 7, 2026},
                                         phased_case{4, 257, 7, 2026}, phased_case{1, 257, 512, 42},
                                         phased_case{4, 257, 512, 42}));

TEST(phased_failure_tests, a_failed_phase_drains_children_and_never_executes_later_phases)
{
    job_system system{2};
    auto state = workloads::prepare_workload({257, 16, 4, 7, 33});
    const auto run = workloads::submit_workload(system.dispatcher(), state);
    EXPECT_THROW(run.merge.wait(), job_error);
    EXPECT_EQ(run.map.status(), job_status::failed);
    EXPECT_EQ(run.fold.status(), job_status::skipped);
    EXPECT_EQ(run.merge.status(), job_status::skipped);
    EXPECT_EQ(state->phase.load(), 1);
    EXPECT_EQ(state->folded.load(), 0U);
    EXPECT_EQ(state->checksum, 0U);
    EXPECT_EQ(system.statistics().submitted, system.statistics().completed);
}

TEST(phased_failure_tests, a_completed_batch_cannot_be_reopened_by_a_late_submission)
{
    job_system system{2};
    auto state = workloads::prepare_workload({17, 3, 4, 42});
    workloads::submit_workload(system.dispatcher(), state).merge.wait();
    const auto checksum = state->checksum;
    const auto late = workloads::submit_workload(system.dispatcher(), state);
    EXPECT_THROW(late.merge.wait(), job_error);
    EXPECT_EQ(late.fold.status(), job_status::skipped);
    EXPECT_EQ(state->checksum, checksum);
    EXPECT_EQ(state->phase.load(), 3);
}
} // namespace atlas::jobs::tests
