/** @file @brief Private immutable tables and bounded owner notification storage. */
#pragma once
#include <atlas/localization/context.hpp>
#include <atomic>
#include <map>
#include <thread>

namespace atlas::localization::detail
{
/// @brief Compact key order is independent of XML declaration order.
struct catalog
{
    definition data;                                  ///< Validated canonical tags and messages.
    std::vector<std::string> keys;                    ///< Sorted fallback schema.
    std::vector<std::vector<const message*>> entries; ///< Language/key index; null means fallback.
};

/// @brief Construct all indices before a publication can replace the active catalog.
std::shared_ptr<const catalog> prepare(definition source);

/// @brief Shared cancellation survives token destruction and queued notification capture.
struct observer
{
    std::atomic<bool> active{true};              ///< Token reset may cancel from another thread.
    std::function<void(std::uint64_t)> callback; ///< Called only on the owner.
};

/// @brief One mutable context, with no UI, storage or native SDK dependency.
struct translation
{
    const language* table; ///< Immutable provenance.
    const message* entry;  ///< Pre-resolved fallback message.
};

/// @brief One mutable context with pre-resolved key slots.
struct context_state
{
    std::thread::id owner = std::this_thread::get_id(); ///< Creator affinity.
    std::uint64_t identity = 0;                         ///< Unique key ownership domain.
    std::uint64_t revision = 1;                         ///< Checked monotonic publication counter.
    std::shared_ptr<const catalog> tables;              ///< Immutable current catalog.
    std::vector<translation> resolved;              ///< O(1) translation lookup after publication.
    std::string selected;                           ///< Requested canonical tag.
    plural_selector selector;                       ///< Native or injected cardinal selector.
    diagnostics::services sinks;                    ///< Optional diagnostics.
    std::vector<std::weak_ptr<observer>> observers; ///< At most 1,024 live registrations.
    bool notifying = false;                         ///< Reject recursive mutation without blocking.
    void check(bool mutation = false) const;        ///< Validate owner and notification phase.
    /// @brief Prepare a complete observer batch and fallback map before publication.
    void publish(std::shared_ptr<const catalog> candidate, std::string tag);
    ///< Prepare observer batch before changing any committed state.
};

/// @brief Validate a bounded UTF-8 field, excluding NUL, surrogates and overlong encoding.
void validate_text(std::string_view value, std::size_t limit);
} // namespace atlas::localization::detail
