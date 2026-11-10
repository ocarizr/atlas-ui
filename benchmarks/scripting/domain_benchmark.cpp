/** @file @brief Compare shared-domain and standalone counter dispatch with explicit Lua PMR
 * metrics. */
#include <atlas/scripting/domain.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace
{
/// @brief Count Lua allocator requests; host C++ metadata and model allocations are excluded.
class lua_resource final : public std::pmr::memory_resource
{
  public:
    std::size_t allocations = 0; ///< Successful allocation/reallocation requests.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        auto* value = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocations;
        return value;
    } ///< Preserve the actual allocator's failure behavior.

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    } ///< Matched deallocation.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< Resources have independent accounting.
};

/// @brief One measured scenario, reporting requested Lua bytes rather than total process memory.
struct sample
{
    double nanoseconds = 0; ///< Average owner-thread dispatch cost per behavior.
    std::size_t lua_bytes = 0,
                hot_allocations = 0; ///< Live Lua bytes and allocations after warmup.
};

/// @brief Time only warmed dispatch; initialization and reporting remain outside the interval.
template <class Function>
sample measure(Function&& dispatch, lua_resource& memory, std::size_t count, std::size_t iterations)
{
    for (std::size_t i = 0; i < 64; ++i)
    {
        dispatch();
    }

    const auto before = memory.allocations;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i)
    {
        dispatch();
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    return {std::chrono::duration<double, std::nano>(elapsed).count() /
                static_cast<double>(count * iterations),
            0, memory.allocations - before};
}

/// @brief Identical source/context/workload makes interpreter composition the measured variable.
sample run_case(std::size_t count, bool shared, std::size_t iterations)
{
    auto memory = std::make_shared<lua_resource>();
    auto model = std::make_shared<atlas::mvvm::view_model>(atlas::mvvm::compile({"benchmark"}));
    atlas::scripting::session_limits limits;
    limits.max_calls = 65536;
    const std::string source =
        "local count=0;return {tick=function() count=count+1 end,read=function()return count end}";
    model->begin_frame(1);
    if (shared)
    {
        atlas::scripting::domain runtime{limits, memory};
        runtime.bind_context("state", model);
        runtime.register_source({"counter", 1, "@benchmark/counter.lua", source});
        atlas::scripting::instance_handle first;
        for (std::size_t i = 0; i < count; ++i)
        {
            auto instance = runtime.create({"counter", "state"});
            if (!i)
            {
                first = instance;
            }
        }

        runtime.begin_frame(1);
        auto result = measure(
            [&]
            {
                runtime.dispatch("tick");
            },
            *memory, count, iterations);
        result.lua_bytes = runtime.inspect().lua_bytes;
        if (std::get<std::int64_t>(runtime.call(first, "read").at(0)) !=
            static_cast<std::int64_t>(64 + iterations))
        {
            throw std::runtime_error("domain benchmark did not execute every iteration");
        }

        return result;
    }

    std::vector<std::unique_ptr<atlas::scripting::session>> sessions;
    for (std::size_t i = 0; i < count; ++i)
    {
        auto session = std::make_unique<atlas::scripting::session>(model, limits, memory);
        session->load(source);
        session->begin_frame(1);
        sessions.push_back(std::move(session));
    }

    auto result = measure(
        [&]
        {
            for (auto& session : sessions)
            {
                session->call("tick");
            }
        },
        *memory, count, iterations);
    for (const auto& session : sessions)
    {
        result.lua_bytes += session->inspect().lua_bytes;
    }

    if (std::get<std::int64_t>(sessions.front()->call("read").at(0)) !=
        static_cast<std::int64_t>(64 + iterations))
    {
        throw std::runtime_error("standalone benchmark did not execute every iteration");
    }

    return result;
}

/// @brief Compare the same script callback without a wrapper, with capture off, and with capture
/// on.
enum class trace_mode : std::uint8_t
{
    direct,   ///< Baseline Lua callback.
    disabled, ///< Authored scope with a disabled capture.
    captured  ///< Authored scope with a capture requested by the host.
};

/// @brief Print per-call cost and actual recorded events for each capture policy.
void measure_trace(const char* label, trace_mode mode)
{
    constexpr std::size_t iterations = 2000;
    auto memory = std::make_shared<lua_resource>();
    auto model = std::make_shared<atlas::mvvm::view_model>(atlas::mvvm::compile({"benchmark"}));
    atlas::diagnostics::profiling_options policy;
    policy.enabled = mode == trace_mode::captured;
    auto profile = std::make_shared<atlas::diagnostics::profiler>(policy);
    atlas::scripting::session_limits limits;
    limits.max_calls = 4096;
    atlas::scripting::session runtime{model, limits, memory, {{}, profile}, {true}};
    const std::string_view source =
        mode == trace_mode::direct
            ? "local count=0; local function increment() count=count+1 end; "
              "return {tick=function() increment() end,read=function()return count end}"
            : "local count=0; local function increment() count=count+1 end; "
              "return {tick=function() diagnostic.scope('increment',increment) "
              "end,read=function()return count end}";
    runtime.load(source, "@benchmark/diagnostics.lua");
    model->begin_frame(1);
    runtime.begin_frame(1);
    profile->clear();
    const auto result = measure(
        [&]
        {
            runtime.call("tick");
        },
        *memory, 1, iterations);
    const auto recorded = profile->statistics().recorded;
    const auto expected =
        policy.enabled && atlas::diagnostics::instrumentation_enabled ? 2 * (64 + iterations) : 0;
    if (recorded != expected || std::get<std::int64_t>(runtime.call("read").at(0)) !=
                                    static_cast<std::int64_t>(64 + iterations))
    {
        throw std::runtime_error(
            "diagnostic benchmark did not execute or record the expected calls");
    }

    std::cout << label << ',' << atlas::diagnostics::instrumentation_enabled << ','
              << result.nanoseconds << ',' << recorded << '\n';
}
} // namespace

/// @brief Deterministic workload; elapsed times are observations, never pass/fail thresholds.
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view{argv[1]} == "--diagnostics")
        {
            std::cout << "mode,probes_compiled,ns_per_call,captured_events\n";
            measure_trace("direct", trace_mode::direct);
            measure_trace("disabled", trace_mode::disabled);
            measure_trace("captured", trace_mode::captured);
            return 0;
        }

        const std::size_t iterations =
            argc == 2 && std::string_view{argv[1]} == "--smoke" ? 16 : 2000;
        std::cout << "instances,mode,ns_per_behavior,lua_live_bytes,hot_lua_allocations\n";
        for (const auto count : {std::size_t{1}, std::size_t{16}, std::size_t{64}})
        {
            for (const auto shared : {false, true})
            {
                const auto result = run_case(count, shared, iterations);
                std::cout << count << ',' << (shared ? "shared" : "standalone") << ','
                          << result.nanoseconds << ',' << result.lua_bytes << ','
                          << result.hot_allocations << '\n';
            }
        }

        return 0;
    }
    catch (...)
    {
        std::fputs("Lua domain benchmark failed\n", stderr);
        return 1;
    }
}
