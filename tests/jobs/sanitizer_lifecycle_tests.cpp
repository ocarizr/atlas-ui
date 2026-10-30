#include "job_system_fixture.hpp"

#include <array>
#include <limits>
#include <type_traits>

namespace atlas::jobs::tests
{
template <typename Span>
concept writable_region = requires(Span values) { write_access(values); };
static_assert(writable_region<std::span<int>>);
static_assert(!writable_region<std::span<const int>>);
static_assert(!std::is_copy_constructible_v<access_guard>);

TEST_F(job_system_fixture, moved_guards_release_exactly_once_and_sequenced_reuse_is_valid)
{
    std::array<int, 1> value{};
    auto region = write_access(std::span{value});
    auto first = system.dispatcher().run("first", region,
                                         [](job_context& context, memory_access& data)
                                         {
                                             auto original = context.access(data);
                                             auto moved = std::move(original);
                                             access_guard assigned;
                                             assigned = std::move(moved);
                                         });
    system.dispatcher()
        .after(first, "next", region,
               [](job_context& context, memory_access& data) { auto guard = context.access(data); })
        .wait();
    EXPECT_TRUE(system.sanitizer_report().diagnostics.empty());
}

TEST_F(job_system_fixture, overflowing_regions_and_invalid_configuration_are_rejected)
{
    EXPECT_THROW((job_system{1, {0, 128}}), std::invalid_argument);
    EXPECT_THROW((job_system{1, {63, 128}}), std::invalid_argument);
    int value = 0;
    system.dispatcher()
        .run("invalid", &value,
             [](job_context& context, int*& pointer)
             {
                 EXPECT_THROW(
                     (void)context.access(memory_access{
                         pointer, std::numeric_limits<std::size_t>::max(), access_mode::write}),
                     std::invalid_argument);
                 // Deliberately malformed descriptor exercises API boundary validation.
                 // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
                 const auto invalid_mode = static_cast<access_mode>(7);
                 EXPECT_THROW(
                     (void)context.access(memory_access{pointer, sizeof(int), invalid_mode}),
                     std::invalid_argument);
             })
        .wait();
}

TEST_F(job_system_fixture, diagnostic_storage_is_bounded_without_blocking_valid_disjoint_work)
{
    alignas(128) std::array<int, 4> values{};
    job_system limited{2, {64, 1}};
    struct input
    {
        int* values;
        signal_gate* gate;
        std::shared_ptr<std::promise<void>> started;
    };
    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    // This guard must release before the local pool destructor on every exit.
    struct release_on_exit
    {
        signal_gate& gate;
        ~release_on_exit() noexcept
        {
            try
            {
                gate.open();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    } release{gate};
    auto first = limited.dispatcher().run("held", input{values.data(), &gate, started},
                                          [](job_context& context, input& data)
                                          {
                                              auto guard = context.access(
                                                  write_access(std::span{data.values, 1}));
                                              data.started->set_value();
                                              data.gate->wait();
                                          });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    limited.dispatcher()
        .run("warnings", std::span<int>{values},
             [](job_context& context, auto& data)
             {
                 for (std::size_t index = 1; index < data.size(); ++index)
                 {
                     auto guard = context.access(write_access(data.subspan(index, 1)));
                 }
             })
        .wait();
    gate.open();
    first.wait();
    const auto report = limited.sanitizer_report();
    EXPECT_EQ(report.diagnostics.size(), report.enabled ? 1U : 0U);
    EXPECT_EQ(report.dropped, report.enabled ? 2U : 0U);
}
} // namespace atlas::jobs::tests
