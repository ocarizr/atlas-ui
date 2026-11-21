/**
 * @file
 * @brief Owner-thread construction of authored viewport UI.
 */
#pragma once
#include <atlas/scripting/domain.hpp>
#include <atlas/streaming/catalog_streamer.hpp>
#include <atlas/ui_content/animation.hpp>
#include <atlas/ui_content/document.hpp>
#include <atlas/ui_content/material.hpp>

namespace atlas::ui_content
{
struct flow_control; ///< Control route supplied by an authored Flow graph.

/// @brief Immutable buffer acquired before a private UI composition.
struct staged_asset
{
    streaming::read_stamp stamp;                         ///< Exact mount and revision.
    std::shared_ptr<const streaming::asset_bytes> bytes; ///< Owned delivery buffer.
};

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

/// @brief Revision-checked schema borrowed only during construction.
struct prepared_model_schema
{
    assets::asset_ref source;            ///< Catalog ViewModel identity.
    std::uint64_t revision = 0;          ///< Authored revision represented by the schema.
    const mvvm::schema* value = nullptr; ///< Immutable schema used for independent instances.
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
    std::span<const prepared_material> materials{}; ///< Optional prepared authored materials.
    std::span<const prepared_model_schema> model_schemas{}; ///< Optional prepared schemas.
    std::optional<std::span<const staged_asset>> staged{};  ///< When set, all reads must be staged.
    std::span<const flow_control> navigation{};       ///< Optional graph-authored control routes.
    std::function<bool(std::string_view)> navigate{}; ///< Owner-only navigation admission.
    std::function<void(const event_action&)>
        dispatch_event{}; ///< Optional owner Flow/registry bridge.
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

    /// @brief Return immutable authored presentation defaults.
    [[nodiscard]] presentation_options presentation() const;
    [[nodiscard]] ui::element_handle
    find(std::string_view id) const; ///< Resolve a live authored ID.
    /// @brief Resolve a live focus handle to a durable ID for reconstructing a view.
    [[nodiscard]] std::string authored_id(ui::element_handle element) const;
    [[nodiscard]] std::optional<assets::asset_ref>
    texture(std::string_view id) const; ///< Image ref.
    [[nodiscard]] std::span<const material_shader_source>
    material_shaders() const; ///< Borrow custom source for graphics-coordinator registration.
    /// @brief Test material, template, shader and image dependency stamps against the active mount.
    [[nodiscard]] bool materials_current(const streaming::catalog_streamer& storage) const noexcept;
    [[nodiscard]] mvvm::view_model& model(std::string_view context); ///< Borrow a live model.
    [[nodiscard]] ui::element_tree& tree();     ///< Borrow owner-affine UI state.
    [[nodiscard]] ui::input_session& input();   ///< Borrow owner-affine routing.
    [[nodiscard]] scripting::domain& scripts(); ///< Borrow the single UI domain.
    /// @brief Immutable semantic bindings retained for one-time host validation and resolution.
    [[nodiscard]] std::span<const event_action> event_bindings() const;
    void begin_models(std::uint64_t frame); ///< Begin matching model and Lua frames before input.
    void synchronize(); ///< Flush models, apply Lua subscription edits and queue property writes.
    /// @brief Replace playback after validating a clip against live targets and binding ownership.
    void play_animation(animation_clip clip, animation_repeat repeat = animation_repeat::once);
    void stop_animation(); ///< Retire playback and completion, holding already applied values.
    void advance_animation(animation_time elapsed); ///< Advance time without opening a UI frame.
    void
    seek_animation(animation_time position); ///< Position the clock without signaling completion.
    [[nodiscard]] std::optional<animation_time> animation_position() const; ///< Current clip time.
    [[nodiscard]] bool take_animation_completion(); ///< Consume the pending one-shot completion.
    /// @brief Queue a dirty sample after other frame producers and before sealing UI commands.
    void publish_animation();
    /// @brief Start a bounded concurrent clip with optional delay, subrange and composition.
    [[nodiscard]] animation_handle start_animation(animation_clip clip,
                                                   animation_options options = {});
    void register_animation(std::string name, animation_clip clip); ///< Store a reusable clip.
    [[nodiscard]] animation_handle
    start_named_animation(std::string_view name,
                          animation_options options = {}); ///< Start a registered clip.
    /// @brief Start a group transactionally. Sequential entries play once at normal rate.
    [[nodiscard]] std::vector<animation_handle>
    start_animation_group(std::span<const animation_group_entry> group,
                          animation_group_order order = animation_group_order::parallel);
    /// @brief Retarget an exclusive scalar from its last committed pose.
    [[nodiscard]] animation_handle tween_animation(std::string_view element, ui::property field,
                                                   float value, animation_time duration);
    void pause_animation(animation_handle player, bool paused); ///< Freeze or resume its clock.
    void reverse_animation(animation_handle player); ///< Change direction without changing pose.
    void seek_animation(animation_handle player, animation_time position); ///< Scrub silently.
    void cancel_animation(animation_handle player); ///< Retire the handle and publish hold/restore.
    void set_animation_weight(animation_handle player, float weight); ///< Adjust a layer blend.
    /// @brief Subscribe a reusable clip to an MVVM action. Resolve names during registration.
    void bind_animation_action(animation_action_route route, animation_clip clip,
                               animation_options options = {});
    /// @brief Resolve a group of exclusive numeric channels once. Registration leaves the pose
    /// intact.
    void register_animation_states(std::string name, std::vector<animation_channel> channels,
                                   std::vector<animation_pose> poses,
                                   animation_transition transition);
    /// @brief Cross-fade to a named pose from the currently committed values.
    void set_animation_state(animation_state_target target);
    /// @brief Use an authored input or Lua-driven ViewModel action to select a named pose.
    void bind_animation_state(animation_action_route route, animation_state_target target);
    void set_reduced_motion(bool enabled); ///< Finish finite clips early; freeze decorative loops.
    [[nodiscard]] std::vector<animation_event>
    take_animation_events(); ///< Drain owner notifications.
    /// @brief Queue the explicit reverse write for an authored two-way binding.
    void write_back(std::string_view id, ui::property property, ui::property_value value);

  private:
    class impl; ///< Private candidate and the owner of all adapter lifetimes.
    explicit instance(std::unique_ptr<impl> state); ///< Publish a completed composition.
    std::unique_ptr<impl> impl_;                    ///< Destroy adapters before tree and models.
};
} // namespace atlas::ui_content
