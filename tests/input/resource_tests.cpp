/** @file @brief Reserved PMR dispatch storage, rollback and owner-thread safety. */
#include "input_test_support.hpp"
#include <future>
#include <memory_resource>

namespace atlas::input::tests
{
/// @brief Count only library PMR metadata; listener and processor allocations remain separate.
class tracking_resource final : public std::pmr::memory_resource
{
  public:
    std::size_t allocations = 0; ///< Successful upstream allocation count.
    std::size_t outstanding = 0; ///< Live metadata bytes requiring release.
    bool reject = false;         ///< Deterministic injected allocation failure.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (reject)
        {
            throw std::bad_alloc();
        }

        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocations;
        outstanding += bytes;
        return result;
    } ///< Track only successful admission.

    void do_deallocate(void* address, std::size_t bytes, std::size_t alignment) override
    {
        outstanding -= bytes;
        std::pmr::new_delete_resource()->deallocate(address, bytes, alignment);
    } ///< Retired/rejected metadata cannot leak through teardown.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< Distinct resources never transfer ownership implicitly.
};

/// @brief Configured action dispatch uses reserved library metadata and releases it on shutdown.
TEST(input_resources, DispatchUsesReservedMetadataAndFailedRebindPreservesTheMap)
{
    auto memory = std::make_shared<tracking_resource>();
    {
        input_system system{{.max_actions = 2,
                             .max_contexts = 2,
                             .max_bindings = 4,
                             .max_devices = 2,
                             .max_controls = 4},
                            memory};
        system.define_action({1, "confirm"});
        const std::array map{binding{1, {device_kind::keyboard, 13}}};
        system.define_context({1, "ui", 0, true, activation_policy::allow_held, map});
        const auto allocations = memory->allocations;
        memory->reject = true;
        const std::array replacement{binding{1, {device_kind::keyboard, 32}}};
        EXPECT_THROW(system.rebind(1, replacement), std::bad_alloc);
        recorder listener;
        const std::array down{key_sample(key::enter, true)}, up{key_sample(key::enter, false)};
        system.dispatch(1, 0, down, listener);
        system.dispatch(2, 0.1, up, listener);
        ASSERT_EQ(listener.actions.size(), 3);
        EXPECT_EQ(listener.actions.back().phase, action_phase::completed);
        EXPECT_EQ(memory->allocations, allocations);
    }
    EXPECT_EQ(memory->outstanding, 0);
}

/// @brief Foreign-thread access rejects before touching mutable state or delivering a callback.
TEST(input_resources, ForeignDispatchAndQueueDrainFailWithoutOwnerMutation)
{
    input_system system;
    auto queue = std::make_unique<event_queue>();
    recorder listener;
    auto result =
        std::async(std::launch::async,
                   [&]
                   {
                       EXPECT_THROW(system.dispatch(1, 0, {}, listener), std::logic_error);
                       EXPECT_THROW(queue->drain(), std::logic_error);
                   });
    result.get();
    EXPECT_EQ(system.statistics().frame, 0);
    EXPECT_TRUE(listener.raw.empty());
    EXPECT_TRUE(queue->drain().values().empty());
}
} // namespace atlas::input::tests
