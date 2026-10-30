#pragma once

#include <atlas/jobs/job_memory.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace atlas::jobs::detail
{
class scratch_resource final : public std::pmr::memory_resource
{
  public:
    explicit scratch_resource(scratch_options options);
    scratch_statistics statistics() const;

  private:
    struct block_deleter
    {
        std::pmr::memory_resource* upstream;
        std::size_t size;
        std::size_t alignment;
        void operator()(std::byte* pointer) const noexcept;
    };
    struct block
    {
        std::unique_ptr<std::byte, block_deleter> data;
        std::size_t size;
        std::size_t used{0};
    };
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void*, std::size_t, std::size_t) override {}
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    void add_block(std::size_t bytes, std::align_val_t alignment);
    scratch_options options_;
    mutable std::mutex mutex_;
    std::vector<block> blocks_;
    scratch_statistics statistics_;
};
} // namespace atlas::jobs::detail
