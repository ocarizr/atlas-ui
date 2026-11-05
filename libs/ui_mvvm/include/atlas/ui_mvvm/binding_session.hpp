/**
 * @file
 * @brief UI adapter for independent ViewModels and explicit command publication boundaries.
 */
#pragma once
#include <atlas/mvvm/view_model.hpp>
#include <atlas/ui/element_tree.hpp>

namespace atlas::ui_mvvm
{
/// @brief Reverse updates are explicit UI event-handler demands, never hidden state polling.
enum class binding_mode : std::uint8_t
{
    one_way, ///< Model to UI only.
    two_way  ///< Allow explicit write_back to the model.
};

/// @brief Data-oriented binding descriptor for future UI XML/editor authoring.
struct binding_definition
{
    std::string source;  ///< Field name resolved once against the selected schema.
    ui::property target; ///< Built-in property on the separately resolved UI element.
    binding_mode mode = binding_mode::one_way; ///< Direction contract.
};

/// @brief Owner-local binding identity; removed targets and recycled slots cannot inherit it.
struct binding_handle
{
    std::uint64_t session = 0;  ///< Binding-session identity.
    std::uint64_t sequence = 0; ///< Never reused within this session.
};

/// @brief Bounded per-adapter state; no scheduler or private worker pool.
struct binding_limits
{
    std::size_t max_bindings = 256; ///< At most 65536 distinct active property bindings.
};

/// @brief Own subscriptions, borrow a live owner tree and publish into its command stream.
class binding_session
{
  public:
    /// @brief Borrow a live tree and retain an independent model.
    binding_session(ui::element_tree& tree, std::shared_ptr<mvvm::view_model> model,
                    std::uint64_t producer, binding_limits limits = {},
                    std::shared_ptr<std::pmr::memory_resource> memory = {},
                    diagnostics::services destinations = {});
    ///< Producer is a caller-reserved nonzero command identity; tree must outlive this adapter.
    ~binding_session(); ///< Owner-only cleanup releases model subscriptions without callbacks.
    /// @brief Borrow a live tree and retain an independent model.
    binding_session(const binding_session&) = delete; ///< One serial adapter owner.
    binding_session&
    operator=(const binding_session&) = delete; ///< Never alias mutable subscriptions.
    /// @brief Compile a data-authored field/property binding once.
    binding_handle bind(ui::element_handle target, const binding_definition& definition);
    ///< Validate type/defaults and compile the name once; duplicate target properties are errors.
    void unbind(binding_handle token); ///< Idempotent token retirement on the owner.
    void publish(); ///< Preflight dirty live targets, then enqueue commands in an open UI frame.
    void write_back(binding_handle token, ui::property_value value); ///< Explicit reverse demand.
  private:
    class impl;                  ///< Weak observers prevent a model/adapter ownership cycle.
    std::unique_ptr<impl> impl_; ///< Retains model and shared PMR, borrows tree.
};
} // namespace atlas::ui_mvvm
