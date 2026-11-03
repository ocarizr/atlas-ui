/** @file @brief Isolated deterministic allocation failure and retention counters. */
#pragma once
#include <limits>
#include <memory_resource>

namespace atlas::ui::tests
{
/// @brief Deterministic allocation failure injection, used only by one test owner.
class failing_resource final : public std::pmr::memory_resource
{
  public:
    std::size_t remaining =
        std::numeric_limits<std::size_t>::max(); ///< Fail after this many calls.
    std::size_t calls = 0;      ///< Count allocator activity, including debug iterator metadata.
    std::size_t live_bytes = 0; ///< Detect leaked partial preparation after an injected failure.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++calls;
        if (remaining == 0)
        {
            throw std::bad_alloc{};
        }

        --remaining;
        auto* data = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live_bytes += bytes;
        return data;
    } ///< Forward successful allocations without borrowing external storage.

    void do_deallocate(void* data, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(data, bytes, alignment);
        live_bytes -= bytes;
    } ///< Cleanup remains available after an injected failure.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< Resource identity is stable until all owned packets are released.
};

} // namespace atlas::ui::tests
