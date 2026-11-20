/** @file @brief Bounded private state for the owner-phase Flow Manager. */
#pragma once
#include <atlas/ui_content/flow.hpp>
#include <set>
#include <thread>

namespace atlas::ui_content::detail
{
/// @brief One composed instance can be shared with a tentative state on the same owner.
struct flow_session
{
    std::uint64_t generation;       ///< Candidate-specific token for authored callbacks.
    std::unique_ptr<instance> view; ///< Owner-affine composition.
};

struct flow_bookmarks; ///< Immutable lightweight child history, never a retained view.

struct flow_entry
{
    std::size_t screen;   ///< Index into the immutable graph.
    bool overlay = false; ///< Paint over the previous entry without giving it input.
    std::shared_ptr<flow_session> session{}; ///< Null for inactive reconstructible history.
    std::string focus{};       ///< Authored bookmark independent of session identities.
    bool focus_pending = true; ///< The host restores focus after preparing presentation.
    std::shared_ptr<const flow_bookmarks> children{}; ///< Retained submenu identities and focus.
}; ///< A bounded history entry owns live state only while presented.

/// @brief A saved child entry contains no instance, Lua state or input handle.
struct flow_bookmark_entry
{
    std::size_t screen;                             ///< Stable index in the immutable graph.
    std::string focus;                              ///< Durable authored focus ID.
    std::shared_ptr<const flow_bookmarks> children; ///< Deeper remembered regions.
};

/// @brief One region's bounded stack restored with newly composed views.
struct flow_region_bookmark
{
    std::size_t region;                     ///< Stable region index.
    std::vector<flow_bookmark_entry> stack; ///< Lightweight history only.
};

/// @brief Parent-owned navigation state, independent of application/model state.
struct flow_bookmarks
{
    std::vector<flow_region_bookmark> regions; ///< Direct remembered child regions.
};

using flow_stacks = std::vector<std::vector<flow_entry>>; ///< One bounded history per region.

struct flow_position
{
    std::size_t region; ///< Owning stack.
    std::size_t index;  ///< Entry position inside the stack.
    bool input;         ///< Foreground for this branch.
}; ///< A path position within one immutable publication.

struct flow_command
{
    std::string transition;   ///< Authored action, empty only for start.
    std::uint64_t origin = 0; ///< Zero means an explicit host request.
    std::string source{};     ///< Nonempty for destination-free scoped exit actions.
    flow_operation operation = flow_operation::exit_menu; ///< Built-in exit vocabulary.
}; ///< At most one outstanding command per viewport.

struct flow_admission
{
    std::thread::id owner = std::this_thread::get_id(); ///< Never read state from a worker.
    std::set<std::uint64_t> inputs;      ///< Currently published foreground generations.
    std::optional<flow_command> command; ///< First admitted action at the sync point.
    bool busy = false; ///< No competing request while a candidate is loading or publishing.
}; ///< Weakly borrowed by composed controls so retired views cannot navigate.

struct flow_pending
{
    flow_position position;          ///< Candidate destination.
    std::uint64_t generation;        ///< Reserved identity for the new view.
    std::unique_ptr<flow_load> load; ///< Nonblocking source request and owner composition.
}; ///< Dropping the request requests cancellation.

struct flow_candidate
{
    flow_stacks stacks;                ///< Private result until all required views are available.
    std::vector<flow_pending> pending; ///< Candidate loads in stable painter order.
    const flow_transition* edge = nullptr; ///< Guard rechecked before commit.
}; ///< Failed and stale transactions never overwrite the live state.
} // namespace atlas::ui_content::detail

namespace atlas::ui_content
{
/// @brief Own runtime history and candidate state on the viewport coordinator.
class flow_manager::impl
{
  public:
    /// @brief Validate once and retain the owner, graph and loader.
    impl(flow_document graph, flow_loader factory, diagnostics::services sinks);

    void check() const; ///< Reject cross-thread access or reentrant calls at publication.
    [[nodiscard]] std::size_t region(std::string_view name) const; ///< Validated graph index.
    [[nodiscard]] std::size_t screen(std::string_view name) const; ///< Validated graph index.
    [[nodiscard]] bool enabled(const flow_transition& edge) const; ///< Resolve the current guard.
    [[nodiscard]] bool plan(const detail::flow_command& command);  ///< Build a private transaction.
    [[nodiscard]] bool apply_transition(detail::flow_candidate& next,
                                        const detail::flow_command& command); ///< Resolve route.
    /// @brief Apply a destination-free scoped close against foreground ownership.
    [[nodiscard]] bool apply_exit(detail::flow_candidate& next, std::string_view source,
                                  flow_operation operation);
    /// @brief Capture only durable child identities and focus before tentative navigation.
    void capture_history(detail::flow_stacks& state) const;
    /// @brief Recursively snapshot direct remembered regions without keeping view instances.
    [[nodiscard]] std::shared_ptr<const detail::flow_bookmarks>
    bookmark(const detail::flow_stacks& state, const detail::flow_entry& entry) const;
    /// @brief Resolve loaded popup defaults and screen-instance overrides.
    [[nodiscard]] std::optional<popup_policy> popup(const detail::flow_entry& entry) const;
    /// @brief Validate loaded presentation kinds and parent host elements before publication.
    void validate_view(const detail::flow_pending& pending, instance& view) const;
    void load_view(detail::flow_candidate& next, detail::flow_position position); ///< Admit I/O.
    [[nodiscard]] bool poll();         ///< Complete all required loads without blocking on I/O.
    void publish(std::uint64_t frame); ///< Swap complete history, then retire outgoing views.
    void clear_candidate() noexcept;   ///< Drop all unpublished work and reopen admission.
    void check_frame(std::uint64_t frame) const; ///< Verify the owner sync point.
    /// @brief Compute deterministic painter and foreground order.
    [[nodiscard]] std::vector<detail::flow_position>
    visible(const detail::flow_stacks& state) const;
    /// @brief Select initial screens in newly opened child regions.
    void populate(detail::flow_stacks& state, std::size_t root) const;

    flow_document graph;                               ///< Validated immutable authoring.
    flow_loader loader;                                ///< Owner composition service.
    diagnostics::services diagnostics;                 ///< Optional phase profiling.
    std::shared_ptr<detail::flow_admission> admission; ///< Weak control-routing target.
    detail::flow_stacks current;                       ///< Current screen and per-region history.
    std::vector<mvvm::value> parameters;               ///< Current typed guard values.
    std::optional<detail::flow_candidate> candidate;   ///< One transaction at a time.
    std::exception_ptr failure; ///< Last load or post-publication lifecycle failure.
    std::size_t root = 0;       ///< Viewport region index.
    std::uint64_t frame = 0;    ///< Last accepted sync frame.
    std::uint64_t sequence = 0; ///< Never reuse stale screen generations.
    bool busy = false;          ///< Reject owner callbacks reentering publication.
    bool closed = false;        ///< Terminal owner shutdown.
};
} // namespace atlas::ui_content
