/**
 * @file
 * @brief Reusable semantic snapshots and bounded asynchronous assistive requests.
 */
#pragma once
#include <atlas/diagnostics/instrumentation.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace atlas::accessibility
{
/// @brief Complete opaque identity; slot reuse requires a different generation.
struct identity
{
    std::uint32_t index = 0;                          ///< Source-local slot.
    std::uint32_t generation = 0;                     ///< Zero is invalid.
    std::uint64_t session = 0;                        ///< Independent owner namespace.
    bool operator==(const identity&) const = default; ///< Exact identity comparison.
};
/// @brief Semantic meaning, independent of geometry, painting and physical input.
enum class role : std::uint8_t
{
    group,           ///< Meaningful container.
    text,            ///< Readable static content.
    image,           ///< Meaningful image; decorative images are omitted.
    button,          ///< Invokable control.
    tab_group,       ///< Selection container.
    tab,             ///< Selectable tab.
    list,            ///< Item container.
    list_item,       ///< Selectable item.
    checkbox,        ///< Checked-state control.
    slider,          ///< Numeric value control.
    scroll_container ///< Scrollable viewport.
};

/// @brief Physical screen-pixel bounds, including negative monitor coordinates.
struct rectangle
{
    double x = 0;                                      ///< Left screen coordinate.
    double y = 0;                                      ///< Top screen coordinate.
    double width = 0;                                  ///< Nonnegative extent.
    double height = 0;                                 ///< Nonnegative extent.
    bool operator==(const rectangle&) const = default; ///< Exact published geometry.
};

/// @brief Immutable publication value; no callbacks or native handles.
struct node
{
    identity id;                                  ///< Stable generation-safe identity.
    identity parent;                              ///< Empty for a semantic root.
    role kind = role::group;                      ///< Exposed meaning.
    std::string name;                             ///< Resolved accessible name in UTF-8.
    std::string description{};                    ///< Additional help, not repeated caption.
    std::string value{};                          ///< Resolved read-only value.
    rectangle bounds{};                           ///< Physical screen bounds.
    bool enabled = true;                          ///< Disabled elements remain discoverable.
    bool offscreen = false;                       ///< Fully clipped or outside viewport.
    bool focusable = false;                       ///< Supports focus requests.
    bool focused = false;                         ///< Actual owner input focus.
    bool selected = false;                        ///< Selection independent of focus.
    bool checked = false;                         ///< Checkbox state.
    bool invoke = false;                          ///< Supports semantic activation.
    bool select = false;                          ///< Supports selection requests.
    bool reveal = false;                          ///< Supports ensure-visible requests.
    double number = 0;                            ///< Read-only numeric value for a slider.
    double minimum = 0;                           ///< Finite lower bound.
    double maximum = 1;                           ///< Finite upper bound.
    rectangle clip{};                             ///< Optional physical clip for point queries.
    bool clipped = false;                         ///< Whether a clipping rectangle is supplied.
    bool operator==(const node&) const = default; ///< Complete change detection.
};
/// @brief Only advertised actions can enter the owner queue.
enum class action : std::uint8_t
{
    focus,  ///< Change input focus without activation.
    invoke, ///< Perform the existing semantic activation.
    select, ///< Select a tab/item through its authored action.
    reveal  ///< Request existing scroll ensure-visible behavior.
};

/// @brief Value-only request; acceptance is asynchronous, never application execution.
struct request
{
    identity target;                  ///< Revalidated again after draining.
    action operation = action::focus; ///< Supported operation.
};

/// @brief Admission budgets; old external snapshots retain independent owned values.
struct limits
{
    std::size_t max_nodes = 65536;          ///< Maximum published semantic entries.
    std::size_t max_text_bytes = 4'194'304; ///< Total UTF-8 payload budget.
    std::size_t max_requests = 256;         ///< Pending action queue capacity.
};

/// @brief Owned immutable version safe for concurrent native queries.
class snapshot
{
  public:
    explicit snapshot(std::vector<node> nodes);   ///< Adopt validated owner values.
    std::span<const node> nodes() const noexcept; ///< Stable semantic order.
    const node* find(identity id) const noexcept; ///< Exact identity or null.
  private:
    std::vector<node> nodes_; ///< Owned values; no borrowed UI resources.
};

/// @brief Thread-safe read/request endpoint; publication/draining belong to its creating thread.
class channel
{
  public:
    explicit channel(limits bounds = {}, diagnostics::services sinks = {}); ///< Validate budgets.
    ~channel();                                  ///< Disconnect native clients; no callbacks.
    channel(const channel&) = delete;            ///< One publication owner.
    channel& operator=(const channel&) = delete; ///< One publication owner.
    void publish(std::vector<node> nodes);       ///< Validate then atomically replace current view.
    std::shared_ptr<const snapshot> read() const; ///< Any-thread immutable observation.
    bool request(accessibility::request value); ///< Any-thread bounded admission; false if invalid.
    std::vector<accessibility::request> take_requests(); ///< Owner drain revalidates latest state.
    void close(); ///< Owner disconnect cancels queued work and rejects future publication.
  private:
    class impl;                  ///< Synchronization, limits and owner identity.
    std::unique_ptr<impl> impl_; ///< No platform dependencies.
};

void validate(std::span<const node> nodes, limits bounds); ///< Hierarchy, numeric and text checks.
bool supports(const node& source, action operation) noexcept; ///< State-aware capability check.
} // namespace atlas::accessibility
