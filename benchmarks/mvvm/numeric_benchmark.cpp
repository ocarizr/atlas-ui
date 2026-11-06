/** @file @brief Descriptive slot/name lookup and numeric-notification workload without timing
 * gates. */
#include "counting_resource.hpp"
#include <atlas/mvvm/view_model.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
/// @brief Shared subscriber counts actual event-driven delivery, not polling iterations.
class counter final : public atlas::mvvm::observer
{
  public:
    std::size_t calls = 0; ///< Completed observer notifications.

    /// @brief Count delivered notifications without adding heavy work.
    void on_notify(const atlas::mvvm::notification&, atlas::mvvm::view_model&) override
    {
        ++calls;
    }

    ///< Lightweight owner callback, excluding arbitrary application work.
};

/// @brief Measure caller-thread elapsed nanoseconds without asserting hardware-specific budgets.
template <class Work> double elapsed(Work work)
{
    const auto start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
        .count();
}

/// @brief Run the measured workload separately from command-line/error handling.
int probe(bool verify)
{
    using namespace atlas::mvvm;
    definition source{"numeric_probe"};
    for (unsigned i = 0; i < 64; ++i)
    {
        source.fields.push_back({"field_" + std::to_string(i), std::int64_t{0}});
    }

    const auto schema = compile(source);
    auto memory = std::make_shared<benchmarks::counting_resource>();
    const auto frames = verify ? 8U : 2000U;
    const auto reads = verify ? 1000U : 200000U;
    double slot_ns = 0, name_ns = 0, dispatch_ns = 0;
    std::size_t extra_allocations = 0, callbacks = 0;
    std::int64_t checksum = 0;
    {
        view_model model{
            schema,
            {.max_commands = 64, .max_arguments = 64, .max_subscriptions = 64, .max_callbacks = 64},
            memory};
        std::vector<field_handle> slots;
        slots.reserve(64);
        auto listener = std::make_shared<counter>();
        for (const auto& field : source.fields)
        {
            slots.push_back(schema.field(field.name));
            model.subscribe(slots.back(), listener);
        }

        const auto setup_allocations = memory->allocations;
        memory->reject = true;
        slot_ns = elapsed(
                      [&]
                      {
                          for (unsigned i = 0; i < reads; ++i)
                          {
                              checksum += model.integer(slots.front());
                          }
                      }) /
                  reads;
        name_ns = elapsed(
                      [&]
                      {
                          for (unsigned i = 0; i < reads; ++i)
                          {
                              checksum += model.integer(schema.field("field_0"));
                          }
                      }) /
                  reads;
        dispatch_ns = elapsed(
                          [&]
                          {
                              for (std::uint64_t frame = 1; frame <= frames; ++frame)
                              {
                                  model.begin_frame(frame);
                                  for (const auto slot : slots)
                                  {
                                      model.set(slot, static_cast<std::int64_t>(frame));
                                  }

                                  model.flush();
                              }
                          }) /
                      (frames * 64.0);
        callbacks = listener->calls;
        extra_allocations = memory->allocations - setup_allocations;
        if (callbacks != frames * std::size_t{64} || model.integer(slots.front()) != frames ||
            extra_allocations)
        {
            throw std::runtime_error("MVVM numeric probe invariant failed");
        }
    }
    if (memory->live_bytes)
    {
        throw std::runtime_error("MVVM numeric probe leaked PMR storage");
    }

    std::cout << "{\"slot_read_ns\":" << slot_ns << ",\"named_read_ns\":" << name_ns
              << ",\"update_notify_ns\":" << dispatch_ns
              << ",\"pmr_peak_bytes\":" << memory->peak_bytes
              << ",\"pmr_allocations_after_setup\":" << extra_allocations
              << ",\"callbacks\":" << callbacks << ",\"checksum\":" << checksum << "}\n";
    return 0;
}
} // namespace

/// @brief Emit descriptive JSON; verification checks semantics/allocation, never timing thresholds.
int main(int argc, char** argv)
{
    try
    {
        const bool verify = argc == 2 && std::string_view{argv[1]} == "--verify";
        if (argc > 1 && !verify)
        {
            throw std::invalid_argument("expected --verify or no arguments");
        }

        return probe(verify);
    }
    catch (const std::exception& error)
    {
        try
        {
            std::cerr << error.what() << '\n';
        }
        catch (...)
        {
            return 1;
        }

        return 1;
    }
    catch (...)
    {
        return 1;
    }
}
