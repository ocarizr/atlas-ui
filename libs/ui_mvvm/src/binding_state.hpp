/** @file @brief Owner binding metadata and weak observer links. */
#pragma once
#include <atlas/ui_mvvm/binding_session.hpp>
#include <thread>

namespace atlas::ui_mvvm
{
/// @brief Restore adapter reentry state on all exits.
struct operation_guard
{
    bool& flag; ///< Owner-only operation state.

    explicit operation_guard(bool& value) : flag(value)
    {
        flag = true;
    } ///< Enter serial work.

    ~operation_guard()
    {
        flag = false;
    } ///< Release during success or unwinding.
};

/// @brief One property binding keeps a compiled model slot and stable target generation.
struct binding_record
{
    ui::element_handle target;        ///< Generation-checked UI destination.
    ui::property property;            ///< UI scalar property.
    mvvm::field_handle field;         ///< Compiled source slot.
    binding_mode mode;                ///< Explicit reverse permission.
    std::uint64_t sequence;           ///< Session-local identity.
    bool active = true, dirty = true; ///< Retired bindings never publish again.
};

/// @brief Shared source fields use one model subscription instead of duplicate notifications.
struct field_subscription
{
    mvvm::field_handle field;   ///< Compiled source.
    mvvm::subscription token;   ///< Retained until its last property binding retires.
    std::size_t references = 0; ///< Active property consumers.
};

/// @brief Observer state has no strong model/tree ownership edge.
struct binding_state
{
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives binding metadata.
    std::pmr::vector<binding_record> bindings;         ///< Reserved active/vacant slots.

    /// @brief Keep the allocation resource alive through its PMR vectors.
    explicit binding_state(std::shared_ptr<std::pmr::memory_resource> resource)
        : memory(std::move(resource)), bindings(0, memory.get())
    {
    }

    ///< Allocate through the caller's shared resource.
};

/// @brief MVVM callbacks only mark dirty slots; they never mutate UI directly.
class dirty_observer final : public mvvm::observer
{
  public:
    /// @brief Retain a weak adapter-state target.
    explicit dirty_observer(std::weak_ptr<binding_state> state) : state_(std::move(state))
    {
    }

    ///< A retained model observer cannot keep the adapter alive.
    void on_notify(const mvvm::notification& event, mvvm::view_model&) override
    {
        if (auto state = state_.lock())
        {
            for (auto& binding : state->bindings)
            {
                if (binding.active && binding.field.index == event.index)
                {
                    binding.dirty = true;
                }
            }
        }
    } ///< Serial dirty propagation uses resolved indexes, not field-name lookups.
  private:
    std::weak_ptr<binding_state> state_; ///< No model/adapter ownership cycle.
};

/// @brief Checked model-to-property conversion.
ui::property_value to_ui(const mvvm::view_model& model, mvvm::field_handle field,
                         ui::property property);
///< Checked scalar conversion and UI domain validation.
/// @brief Explicit reverse conversion without silent truncation.
mvvm::value to_model(ui::property_value value, mvvm::value_type type, ui::property property);

///< Reject implicit truncation, invalid automatic sizes and incompatible reverse values.
/// @brief Adapter state remains separate from both mutable model columns and UI internals.
class binding_session::impl
{
  public:
    /// @brief Reserve bounded binding and publication metadata.
    impl(ui::element_tree& tree, std::shared_ptr<mvvm::view_model> model, std::uint64_t producer,
         binding_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
         diagnostics::services destinations);
    ///< Reserve metadata before binding/notification work.
    void check() const;                         ///< Reject foreign/reentrant adapter use.
    binding_record* find(binding_handle token); ///< Validate session before sequence lookup.
    void retire(binding_record& record);        ///< Release the last source observer safely.
    ui::element_tree& tree;                     ///< Borrowed owner tree.
    std::shared_ptr<mvvm::view_model> model;    ///< Source-of-truth lifetime.
    std::uint64_t producer;                     ///< Caller-coordinated command identity.
    binding_limits limits;                      ///< Metadata bounds.
    std::shared_ptr<binding_state> state;       ///< PMR and weak-observer target.
    std::shared_ptr<dirty_observer> listener;   ///< Shared among distinct source subscriptions.
    std::pmr::vector<field_subscription> subscriptions; ///< One per distinct field.
    std::pmr::vector<ui::property_write> prepared;      ///< Reused publication preflight scratch.
    diagnostics::services destinations;                 ///< Optional logs/profiling.
    std::uint64_t sequence = 0;                         ///< Nonreused token sequence.
    bool busy = false; ///< No adapter reentry from diagnostic sinks.
    const std::thread::id owner = std::this_thread::get_id(); ///< Immutable thread identity.
    const std::uint64_t session;                              ///< Distinct adapter lifetime.
};
} // namespace atlas::ui_mvvm
