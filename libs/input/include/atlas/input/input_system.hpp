/**
 * @file
 * @brief Owner-driven action events, prioritized mapping contexts and bounded source processing.
 */
#pragma once
#include <atlas/diagnostics/instrumentation.hpp>
#include <atlas/input/processors.hpp>
#include <string_view>

namespace atlas::input
{
using action_id = std::uint32_t;  ///< Explicit nonzero semantic identity, not a physical key.
using context_id = std::uint32_t; ///< Explicit nonzero map identity.

/// @brief Semantic action shapes independent of device representation.
enum class value_type : std::uint8_t
{
    button, ///< OR of contributions, emitted as zero/one.
    axis1d, ///< Signed finite scalar.
    axis2d  ///< Signed finite vector.
};

/// @brief Interaction transitions delivered synchronously in deterministic order.
enum class action_phase : std::uint8_t
{
    started,   ///< Entered actuation.
    ongoing,   ///< Timed interaction is waiting, at most once per dispatch.
    performed, ///< Trigger fired or an active performed value changed.
    completed, ///< Normal release after performance.
    cancelled  ///< Reset, preemption or failed interaction recognition.
};

struct action_event
{
    action_id action = 0;                         ///< Semantic action identity.
    value_type type = value_type::button;         ///< Value interpretation.
    action_phase phase = action_phase::cancelled; ///< Transition category.
    input_value value{};     ///< Finite typed value; release/cancellation is neutral.
    std::uint64_t frame = 0; ///< Owner-selected increasing dispatch identity.
}; ///< Independent of physical devices and UI elements.

struct action_definition
{
    action_id id;                                     ///< Unique nonzero action identity.
    std::string_view name;                            ///< Copied diagnostic name, at most 64 bytes.
    value_type type = value_type::button;             ///< Declared semantic shape.
    std::shared_ptr<const value_modifier> modifier{}; ///< Optional post-aggregation transformation.
    std::shared_ptr<const input_trigger> trigger{}; ///< Null selects ordinary press/value behavior.
}; ///< Define between dispatch calls; definitions do not borrow name storage.

/// @brief Portable device-family and physical-control selector.
struct control_binding
{
    device_kind device = device_kind::keyboard; ///< Device family selector.
    std::uint32_t code = 0;                     ///< Portable physical control identifier.
};

struct binding
{
    action_id action;                                 ///< Existing action identity.
    control_binding control;                          ///< Device/control match.
    std::shared_ptr<const value_modifier> modifier{}; ///< Optional per-control transformation.
    bool consume = true;      ///< Claim this control against lower-priority contexts.
    std::uint64_t device = 0; ///< Zero matches any device instance in the family.
    std::uint64_t source = 0; ///< Zero matches any source.
}; ///< Several bindings can compose one action without tying it to keyboard/mouse.

/// @brief Policy for controls already held when a map becomes active.
enum class activation_policy : std::uint8_t
{
    require_neutral, ///< Previously held controls must become neutral before activation.
    allow_held       ///< Explicitly allow currently actuated controls.
};

/// @brief Copied priority map configuration and its bindings.
struct context_definition
{
    context_id id;             ///< Unique nonzero map identity.
    std::string_view name;     ///< Copied diagnostic name, at most 64 bytes.
    std::int32_t priority = 0; ///< Larger values win; equal priority uses smaller context ID.
    bool enabled = false;      ///< Initial activation request.
    activation_policy activation = activation_policy::require_neutral; ///< Held-input policy.
    std::span<const binding>
        bindings{}; ///< Copied definitions; no borrowed configuration survives.
};

struct input_limits
{
    std::size_t max_actions = 256;    ///< At most 4096 actions.
    std::size_t max_contexts = 32;    ///< At most 256 contexts.
    std::size_t max_bindings = 1024;  ///< At most 16384 copied bindings.
    std::size_t max_devices = 16;     ///< Source/device instances, at most 256.
    std::size_t max_controls = 1024;  ///< Retained control states, at most 16384.
    std::size_t max_events = 256;     ///< Per dispatch, at most 4096.
    std::size_t max_callbacks = 4096; ///< Raw/action callback attempts, at most 1048576.
}; ///< Capacity bounds are not process-memory or hardware-latency guarantees.

/// @brief Borrowed synchronous observer; handlers must not wait for input or I/O.
class input_listener
{
  public:
    virtual ~input_listener() = default; ///< Borrowed only for synchronous dispatch.

    virtual void on_input(const event&)
    {
    } ///< Optional normalized raw event delivery.

    virtual void on_action(const action_event&)
    {
    } ///< Optional semantic state transition delivery.
};

/// @brief Observations of the most recently admitted dispatch.
struct input_statistics
{
    std::uint64_t frame = 0;   ///< Last admitted dispatch.
    std::size_t events = 0;    ///< Processed normalized packets.
    std::size_t callbacks = 0; ///< Callback attempts in the last dispatch.
    bool failed = false; ///< Dispatch requires explicit reset after callback/processor failure.
};

/// @brief Reusable owner-affine action mapper with explicit capacity and recovery boundaries.
class input_system
{
  public:
    explicit input_system(
        input_limits limits = {}, std::shared_ptr<std::pmr::memory_resource> memory = {},
        diagnostics::services destinations = {}); ///< No window, worker or SDK owner.
    ~input_system(); ///< Destroy on owner outside dispatch; no implicit callbacks.
    input_system(const input_system&) = delete;             ///< Unique mutable mapping/state owner.
    input_system& operator=(const input_system&) = delete;  ///< No shared mutable facade.
    void define_action(const action_definition& action);    ///< Checked unique action admission.
    void define_context(const context_definition& context); ///< Checked copied bindings.
    void rebind(context_id context,
                std::span<const binding> bindings); ///< Replace at next dispatch.
    void request_context(context_id context,
                         bool enabled); ///< Deferred bounded request; allowed in action callbacks.
    void
    dispatch(std::uint64_t frame, double time_seconds, std::span<const event> events,
             input_listener& listener); ///< Ready events plus caller clock; no wait for samples.
    void
    reset(); ///< Explicit failed-dispatch recovery; neutralize state without observer callbacks.
    input_statistics statistics() const; ///< Owner observation outside callbacks.
  private:
    class impl; ///< Bounded mappings, control values and deterministic trigger memory.
    std::unique_ptr<impl> impl_; ///< No window/UI dependency or hidden process-wide singleton.
};
} // namespace atlas::input
