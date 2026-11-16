/**
 * @file
 * @brief Authored viewport navigation and owner-phase publication of composed screens.
 */
#pragma once
#include <atlas/ui_content/instance.hpp>

namespace atlas::ui_content
{
/// @brief A region owns a history stack and is active while its parent screen is presented.
struct flow_region
{
    std::string id;      ///< Stable region name.
    std::string initial; ///< Initial screen in this region.
    std::string owner;   ///< Parent screen, empty for the single viewport region.
};

/// @brief Stable menu identity paired with a typed viewport asset.
struct flow_screen
{
    std::string id;          ///< Stable screen name.
    std::string region;      ///< Owning region.
    assets::asset_ref asset; ///< Composed UI asset.
    std::string focus{};     ///< Optional authored default focus element.
};

/// @brief Authored history changes applied at the owner synchronization point.
enum class flow_operation : std::uint8_t
{
    push,    ///< Add a screen to the region's history.
    replace, ///< Replace the selected screen.
    pop,     ///< Close the top entry of its own region.
    back,    ///< Pop this region or the nearest ancestor with history.
    overlay, ///< Present above the previous entry without giving it input.
    reload   ///< Reconstruct the current screen and preserve its history position.
}; ///< Operations execute only at an owner synchronization point.

struct flow_parameter
{
    std::string id;    ///< Stable parameter name.
    mvvm::value value; ///< Typed initial value.
}; ///< Host-owned state can supply updated values before asking for navigation.

struct flow_guard
{
    std::string parameter; ///< Authored parameter identifier.
    mvvm::value equals;    ///< Exact typed equality gate.
}; ///< Declarative guard with no script or worker callbacks.

struct flow_transition
{
    std::string id;   ///< Stable action name.
    std::string from; ///< The screen that must own input in its region.
    std::string to{}; ///< Same-region destination for push/replace/overlay.
    flow_operation operation = flow_operation::push; ///< History operation.
    std::optional<flow_guard> guard{};               ///< Optional typed gate.
    bool enabled = true;    ///< Disabled actions stay authored but do not execute.
    std::string control{};  ///< Optional authored element on the source screen.
    std::uint32_t action{}; ///< Zero means pointer-up; otherwise a performed semantic action.
}; ///< Declaration order breaks ties between bindings on one control.

struct flow_document
{
    std::vector<flow_region> regions;         ///< Parent relationships form a rooted tree.
    std::vector<flow_screen> screens;         ///< Authored screen names and asset references.
    std::vector<flow_parameter> parameters;   ///< Exact typed defaults.
    std::vector<flow_transition> transitions; ///< Stable declaration order.
}; ///< Data contract consumed by XML, tooling and the runtime.

void validate_flow(const flow_document& source); ///< Validate topology and typed references.
[[nodiscard]] flow_document parse_flow_xml(xml_source source); ///< Read strict version 1 XML.
[[nodiscard]] std::string write_flow_xml(const flow_document& source); ///< Emit canonical XML.
/// @brief Read a flow asset through its typed mount at bootstrap.
[[nodiscard]] flow_document load_flow(assets::asset_ref source, const construction& options);

/// @brief Compute a bounded shortest forward path, using declared initial guard values.
[[nodiscard]] std::vector<std::string> plan_flow_path(const flow_document& source,
                                                      std::string_view from, std::string_view to);
/// @brief Report screens without an enabled authored exit, including explicit terminal screens.
[[nodiscard]] std::vector<std::string> flow_dead_ends(const flow_document& source);

struct flow_control
{
    std::string element;    ///< Authored source element ID.
    std::string transition; ///< Stable action name to enqueue.
    std::uint32_t action{}; ///< Zero is pointer-up; otherwise a performed semantic action.
}; ///< Resolved by the regular UI composition path.

using flow_dispatch = std::function<bool(std::string_view)>; ///< Queue a control action on owner.

struct flow_load_request
{
    assets::asset_ref source;           ///< Typed UI asset to prepare.
    std::vector<flow_control> controls; ///< Authored routes for this screen only.
    flow_dispatch dispatch;             ///< Routes attached to this exact candidate generation.
}; ///< A load owns its request until it returns a privately composed instance.

class flow_load
{
  public:
    virtual ~flow_load() = default; ///< Cancel or abandon I/O without entering mutable UI.
    /// @brief On owner, return null while pending; otherwise return a private screen or throw.
    [[nodiscard]] virtual std::unique_ptr<instance> poll() = 0;
}; ///< Host seam for ready assets or controlled deterministic tests.

using flow_loader =
    std::function<std::unique_ptr<flow_load>(flow_load_request)>; ///< Owner factory.
/// @brief Prepare XML screens and bootstrap dependencies through existing streamer jobs.
/// The supplied mount and any borrowed construction tables outlive the loader.
[[nodiscard]] flow_loader make_flow_loader(const construction& options);

/// @brief Outcome of an owner synchronization point.
enum class flow_status : std::uint8_t
{
    idle,      ///< No command pending.
    loading,   ///< Active screens are still usable while candidates load.
    committed, ///< New navigation state published.
    rejected,  ///< Invalid origin, guard, history operation or capacity.
    failed     ///< Candidate failed; previously published screens remain active.
}; ///< Result of an owner synchronization point.

struct flow_presentation
{
    std::string_view region; ///< Borrowed region name.
    std::string_view screen; ///< Borrowed screen name.
    instance* view;          ///< Borrowed live composition until the next publication.
    bool input;              ///< Host should dispatch input only to foreground compositions.
}; ///< Ordered back to front, including child regions.

/// @brief Own independent viewport navigation and publish complete owner-thread transitions.
class flow_manager
{
  public:
    explicit flow_manager(flow_document graph, flow_loader loader,
                          diagnostics::services sinks = {}); ///< One owner per viewport.
    ~flow_manager(); ///< Cancel pending work and release compositions on the owner.
    flow_manager(const flow_manager&) = delete;            ///< Mutable history has a single owner.
    flow_manager& operator=(const flow_manager&) = delete; ///< Own independent menu state.
    void start(); ///< Queue initial screens, or retry initialization after failure.
    [[nodiscard]] bool request(std::string_view transition);      ///< Queue one named action.
    void set_parameter(std::string_view name, mvvm::value value); ///< Update a typed guard value.
    void cancel(); ///< Drop candidate generations and preserve the published state.
    [[nodiscard]] flow_status advance(std::uint64_t frame); ///< Poll and publish between frames.
    [[nodiscard]] std::exception_ptr failure() const;       ///< Last load or lifecycle failure.
    [[nodiscard]] std::vector<flow_presentation> presentation() const; ///< Ordered live views.
    void restore_focus(instance& view); ///< Call after the view's first input begin-frame.
    void shutdown(std::uint64_t frame); ///< Cancel, retire active views and close the manager.
  private:
    class impl;                  ///< Owner-only history, candidates and graph indexes.
    std::unique_ptr<impl> impl_; ///< Retain implementation outside the public header.
};
} // namespace atlas::ui_content
