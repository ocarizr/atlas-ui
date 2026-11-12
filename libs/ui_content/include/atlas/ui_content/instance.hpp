/**
 * @file
 * @brief Owner-thread construction of authored viewport UI.
 */
#pragma once
#include <atlas/scripting/domain.hpp>
#include <atlas/streaming/catalog_streamer.hpp>
#include <atlas/ui_content/document.hpp>

namespace atlas::ui_content
{
/// @brief Exact signature emitted by the existing ViewModel factory generator.
using model_factory = std::unique_ptr<mvvm::view_model> (*)(
    mvvm::runtime_limits, std::shared_ptr<std::pmr::memory_resource>, const diagnostics::services&);

/// @brief Prepared replacement for parsing a ViewModel asset at a specific revision.
struct prepared_model
{
    assets::asset_ref source; ///< Catalog ViewModel identity.
    std::uint64_t revision;   ///< Authored revision compiled into the factory.
    model_factory make;       ///< Construct a fresh instance per named context.
};

/// @brief Explicit startup services borrowed or retained by a composed UI instance.
struct construction
{
    streaming::catalog_streamer& storage; ///< Owner-only bootstrap reads; never call from a job.
    std::span<const prepared_model> factories{};         ///< Optional prepared native models.
    std::shared_ptr<std::pmr::memory_resource> memory{}; ///< Retained host memory resource.
    diagnostics::services diagnostics{};                 ///< Optional logs and CPU profiling.
    streaming::read_options reads{}; ///< Shared priority and cancellation for bootstrap.
    scripting::script_diagnostics_options script_diagnostics{}; ///< Explicit Lua opt-in.
    std::span<const scripting::service_definition> services{};  ///< Host capabilities at startup.
};

/// @brief Acquire an authored UI document through a typed catalog mount.
[[nodiscard]] document load(assets::asset_ref source, const construction& options);

/// @brief Own a tree, input routes, property subscriptions and one shared Lua domain.
class instance
{
  public:
    /// @brief Construct a private candidate; failure leaves any previous instance intact.
    [[nodiscard]] static std::unique_ptr<instance> build(const document& source,
                                                         const construction& options);
    ~instance(); ///< Retire all registrations on the owner without invoking callbacks.
    instance(const instance&) = delete;            ///< A mutable composition has one owner.
    instance& operator=(const instance&) = delete; ///< A mutable composition has one owner.

    [[nodiscard]] ui::element_handle
    find(std::string_view id) const; ///< Resolve a live authored ID.
    [[nodiscard]] std::optional<assets::asset_ref>
    texture(std::string_view id) const;                              ///< Image ref.
    [[nodiscard]] mvvm::view_model& model(std::string_view context); ///< Borrow a live model.
    [[nodiscard]] ui::element_tree& tree();     ///< Borrow owner-affine UI state.
    [[nodiscard]] ui::input_session& input();   ///< Borrow owner-affine routing.
    [[nodiscard]] scripting::domain& scripts(); ///< Borrow the single UI domain.
    void begin_models(std::uint64_t frame); ///< Begin matching model and Lua frames before input.
    void synchronize(); ///< Flush models, apply Lua subscription edits and queue property writes.
    /// @brief Queue the explicit reverse write for an authored two-way binding.
    void write_back(std::string_view id, ui::property property, ui::property_value value);

  private:
    class impl; ///< Private candidate and the owner of all adapter lifetimes.
    explicit instance(std::unique_ptr<impl> state); ///< Publish a completed composition.
    std::unique_ptr<impl> impl_;                    ///< Destroy adapters before tree and models.
};
} // namespace atlas::ui_content
