/** @file @brief Immutable graph revisions with bounded history and explicit runtime publication. */
#include "owner_scope.hpp"
#include <atlas/editor_flow/session.hpp>
#include <atomic>
#include <limits>

namespace atlas::editor_flow
{
namespace
{
/// @brief Never reuse a session identity, even after every associated document is destroyed.
std::uint64_t identity()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Flow session identities exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1));

    return value;
}
} // namespace

/// @brief Reserve history bookkeeping once; only accepted edits allocate authored snapshots.
session::session(document source, bool saved, std::size_t history, diagnostics::services sinks)
    : current_{nullptr, 0}, identity_{identity()}, limit_{history}, sinks_{std::move(sinks)}
{
    if (limit_ > 128)
    {
        throw std::invalid_argument("Flow history limit exceeds 128 revisions");
    }

    (void)write_xml(source);
    undo_.reserve(limit_);
    redo_.reserve(limit_);
    current_ = {std::make_shared<const document>(std::move(source)), next_++};
    saved_ = saved ? current_.id : 0;
}

/// @brief Read and write ownership follows the editor coordinator, never a job worker.
void session::check() const
{
    if (owner_ != std::this_thread::get_id() || busy_)
    {
        throw std::logic_error("Flow session requires its idle owner");
    }
}

/// @brief History moves shared immutable snapshots, never copies their graph data.
void session::retain(std::vector<revision_state>& history, revision_state state)
{
    if (!limit_)
    {
        return;
    }

    if (history.size() == limit_)
    {
        history.erase(history.begin());
    }

    history.push_back(std::move(state));
}

/// @brief Borrow current immutable authoring for one owner operation.
const document& session::current() const
{
    check();
    return *current_.source;
}

/// @brief Undo/redo restores this identity rather than manufacturing a clean revision.
std::uint64_t session::revision() const
{
    check();
    return current_.id;
}

/// @brief Saving is tied to the authored revision that actually reached disk.
bool session::dirty() const
{
    check();
    return current_.id != saved_;
}

/// @brief No more than the configured number of earlier states is retained.
bool session::can_undo() const
{
    check();
    return !undo_.empty();
}

/// @brief A divergent edit clears every old redo branch.
bool session::can_redo() const
{
    check();
    return !redo_.empty();
}

/// @brief Validate and allocate before changing history, selection or the published preview.
void session::apply(document source)
{
    check();
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.edit", "editor.flow"}};
    if (source.id != current_.source->id)
    {
        throw std::invalid_argument("Flow edits cannot replace document identity");
    }

    if (next_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Flow revision identities exhausted");
    }

    (void)write_xml(source);
    auto candidate = std::make_shared<const document>(std::move(source));
    retain(undo_, current_);
    current_ = {std::move(candidate), next_++};
    redo_.clear();
}

/// @brief Restoring a draft never rebuilds or retires the last valid runtime snapshot.
bool session::undo()
{
    check();
    if (undo_.empty())
    {
        return false;
    }

    retain(redo_, current_);
    current_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

/// @brief Restore metadata and selection along with exact graph declaration identities.
bool session::redo()
{
    check();
    if (redo_.empty())
    {
        return false;
    }

    retain(undo_, current_);
    current_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

/// @brief The persistence owner calls this only after its complete save transaction succeeds.
void session::mark_saved()
{
    check();
    saved_ = current_.id;
}

/// @brief Runtime publication checks exact lifetime and revision, including undo generations.
bool session::publish(const runtime_candidate& candidate)
{
    check();
    if (candidate.owner_ != identity_ || candidate.revision_ != current_.id || !candidate.graph_)
    {
        return false;
    }

    const detail::owner_scope guard{busy_};
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.publish", "editor.flow"}};
    if (candidate.assets_)
    {
        try
        {
            for (const auto& screen : candidate.graph_->screens)
            {
                if (!candidate.assets_(screen.asset))
                {
                    return false;
                }
            }
        }
        catch (...)
        {
            diagnostics::write(
                sinks_, diagnostics::log_level::warning, "editor.flow",
                "Flow publication dependency check failed; previous runtime preserved");
            return false;
        }
    }

    auto ready = std::make_shared<const runtime_candidate>(candidate);
    published_ = std::move(ready);
    return true;
}

/// @brief Runtime consumers can retain this immutable value independently of draft edits.
std::shared_ptr<const runtime_candidate> session::published() const
{
    check();
    return published_;
}
} // namespace atlas::editor_flow
