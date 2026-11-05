/** @file @brief Bounded private mapping, device state and action interaction memory. */
#pragma once
#include <algorithm>
#include <atlas/input/input_system.hpp>
#include <limits>
#include <memory_resource>
#include <tuple>

namespace atlas::input
{
/// @brief Owned action definition and current interaction memory.
struct action_record
{
    action_id id;                                   ///< Stable configured action identity.
    std::array<char, 64> name{};                    ///< Copied bounded diagnostic label.
    value_type type;                                ///< Semantic shape.
    std::shared_ptr<const value_modifier> modifier; ///< Post-aggregation processor.
    std::shared_ptr<const input_trigger> trigger;   ///< Immutable recognition rule.
    input_value value{}, sum{};      ///< Published state and private current aggregate.
    double pressed_at = 0;           ///< Caller-clock actuation start.
    std::uint64_t ongoing_frame = 0; ///< Bound ongoing delivery to one per dispatch.
    context_id winner = 0;           ///< Highest-priority enabled context mapping this action.
    std::uint64_t selected_epoch = 0,
                  active_epoch = 0; ///< Current mapping versus active interaction.
    bool active = false, performed = false, rejected = false; ///< Per-actuation memory.
};

/// @brief Compiled mapping range and deferred activation generation.
struct context_record
{
    context_id id;                    ///< Stable configured map identity.
    std::array<char, 64> name{};      ///< Copied label.
    std::int32_t priority;            ///< Descending precedence, then ascending identity.
    bool enabled;                     ///< Current admitted activation.
    activation_policy activation;     ///< Previously held input handling.
    std::uint64_t activated = 0;      ///< Neutral epoch required by this map.
    bool rearm = true;                ///< Configuration applies at the next dispatch boundary.
    std::size_t begin = 0, count = 0; ///< Contiguous mapping range; compiled outside dispatch.
};

/// @brief Copied selectors with a stable action-storage index.
struct binding_record
{
    context_id context; ///< Owning map identity.
    std::size_t action; ///< Stable index in reserved action storage.
    binding definition; ///< Retained processor and selectors.
};

/// @brief Sorted source/device/control state and priority claim.
struct control_record
{
    std::uint64_t source;   ///< Stream identity.
    device_identity device; ///< Device instance/family.
    std::uint32_t code;     ///< Physical control.
    input_value value;      ///< Last sample.
    std::uint64_t neutral;  ///< Epoch when created or last neutral.
    context_id claim = 0;   ///< Current priority-consumption owner.
};

/// @brief Bounded connected instance identity.
struct device_record
{
    std::uint64_t source;   ///< Stream identity.
    device_identity device; ///< Instance/family.
};

struct context_request
{
    context_id context; ///< Requested map identity.
    bool enabled;       ///< Requested activation state.
}; ///< Coalesced deferred demand.

/// @brief Reusable owner-affine action mapper with explicit capacity and recovery boundaries.
/// @brief Private bounded mapper state and synchronous dispatch guards.
class input_system::impl
{
  public:
    /// @brief Reserve metadata and preflight scratch before processing begins.
    impl(input_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
         diagnostics::services destinations);
    ///< Reserve hot-path storage before event admission.
    void check_thread() const;                    ///< Identity check before mutable observation.
    void check() const;                           ///< Reject callback/processor/diagnostic reentry.
    std::size_t action_index(action_id id) const; ///< Config-time identity lookup.
    context_record& context(context_id id);       ///< Reject unknown context.
    void validate_bindings(std::span<const binding> values) const; ///< No partial configuration.
    void
    preflight(std::span<const event> values); ///< Simulate metadata admission without callbacks.
    void apply(const event& value);           ///< Update only owned control/device state.
    void
    apply_to(const event& value, std::pmr::vector<control_record>& controls,
             std::pmr::vector<device_record>& devices); ///< Shared admission simulation kernel.
    void recompute(bool cancelled); ///< Aggregate priorities/bindings and advance trigger memory.
    void transition(action_record& action, bool cancelled); ///< Deterministic action phases.
    void emit(action_record& action, action_phase phase, input_value value); ///< Owner callbacks.
    void raw(const event& value); ///< Normalized packet callback before its action transitions.
    void callback_budget();       ///< Shared raw/action delivery bound.
    void fail() noexcept;         ///< Latch, neutralize and preserve original exception.
    void neutralize();            ///< Clear physical/interaction state, retain definitions.

    /// @brief Restore a synchronous reentry flag on every scope exit.
    struct guard
    {
        /// @brief Borrow the owner-only guard flag.
        bool& flag;

        /// @brief Mark the guarded operation active.
        explicit guard(bool& f) : flag(f)
        {
            flag = true;
        }

        /// @brief Restore the guard even after an exception.
        ~guard()
        {
            flag = false;
        }
    };

    ///< Restore synchronous guard flags on every exit.
    input_limits limits;                               ///< Capacity policies.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives all PMR storage.
    diagnostics::services destinations;                ///< Optional shared diagnostics.
    std::pmr::vector<action_record> actions;           ///< Stable action indexes.
    std::pmr::vector<context_record> contexts;         ///< Configuration remains owned.
    std::pmr::vector<std::size_t> order;               ///< Sorted context indexes.
    std::pmr::vector<binding_record> bindings;         ///< Bounded copied definitions.
    std::pmr::vector<control_record> controls,
        scratch_controls;                                     ///< Sorted state/preflight scratch.
    std::pmr::vector<device_record> devices, scratch_devices; ///< Device admission accounting.
    std::pmr::vector<context_request> pending; ///< At most one request per defined context.
    input_listener* listener = nullptr;        ///< Borrowed only inside dispatch.
    input_statistics stats;                    ///< Last admitted dispatch observations.
    double time = 0;                           ///< Monotonic caller clock.
    std::uint64_t epoch = 0;                   ///< Nonwrapping source/control/context sequencing.
    bool busy = false, delivering = false, requesting = false; ///< Reentry boundaries.
    const std::thread::id owner = std::this_thread::get_id();  ///< Sole mutable owner.
};

/// @brief Bounded labels are copied, never retained as caller string views.
std::array<char, 64> checked_name(std::string_view value);
void validate_value(input_value value); ///< Reject nonfinite normalized/processed values.
} // namespace atlas::input
