/**
 * @file
 * @brief Owner-affine locale publication and cleanup-safe change subscriptions.
 */
#pragma once
#include <atlas/localization/catalog.hpp>
#include <stop_token>

namespace atlas::localization
{
namespace detail
{
struct context_state; ///< Private owner state.
struct observer;      ///< Shared cancellation record.
} // namespace detail

/// @brief A name resolved once against one context's stable fallback-key schema.
struct message_key
{
    std::uint64_t context = 0; ///< Foreign contexts are rejected.
    std::uint32_t index = 0;   ///< Compact immutable key index.
};

/// @brief Owned resolution remains usable after reload, language change or context destruction.
struct resolved_text
{
    std::string text;   ///< UTF-8 message; plural selection does not interpolate parameters.
    std::string locale; ///< Translation locale, including when fallback supplied the text.
    text_direction direction{}; ///< Direction of the actual translation.
    std::uint64_t revision = 0; ///< Published context generation.
};

/// @brief Move-only cancellation token; destruction never invokes user code.
class subscription
{
  public:
    subscription() = default; ///< An empty token owns no registration.
    ~subscription(); ///< Disable delivery even if a notification batch is already captured.
    subscription(subscription&&) noexcept;                 ///< Transfer cancellation ownership.
    subscription& operator=(subscription&&) noexcept;      ///< Cancel the previous registration.
    subscription(const subscription&) = delete;            ///< Registrations have one owner.
    subscription& operator=(const subscription&) = delete; ///< No aliasing cancellation tokens.
    void reset() noexcept; ///< Safe after the context has been destroyed.
  private:
    friend class context; ///< Only the context creates registrations.
    explicit subscription(std::shared_ptr<detail::observer> observer); ///< Retain cancellation.
    std::shared_ptr<detail::observer> observer_; ///< Does not retain its context.
};

/// @brief One owner thread selects a language and publishes a validated replacement table.
class context
{
  public:
    explicit context(
        definition source, plural_selector selector = select_plural,
        diagnostics::services sinks = {}); ///< Validate before publishing revision one.
    ~context();                            ///< Disable registrations without calling subscribers.
    context(const context&) = delete;      ///< No process-global mutable locale state.
    context& operator=(const context&) = delete; ///< Use separate editor and preview contexts.
    [[nodiscard]] message_key
    key(std::string_view name) const; ///< Resolve once; unknown keys fail.
    [[nodiscard]] resolved_text
    resolve(message_key key,
            std::optional<double> count = {}) const; ///< Plurals require an explicit finite count.
    [[nodiscard]] std::string
    selected_language() const;                    ///< Requested tag, not fallback provenance.
    [[nodiscard]] std::uint64_t revision() const; ///< No-op selection does not increment it.
    void select(std::string_view language); ///< Publish on the owner at a caller's frame boundary.
    void reload(definition source,
                const std::stop_token& stop = {}); ///< Same key schema; atomic failure.
    /// @brief Subscribe to revisions; callbacks may resolve text but cannot mutate this context.
    [[nodiscard]] subscription observe(std::function<void(std::uint64_t)> callback);

  private:
    std::shared_ptr<detail::context_state> state_; ///< Private validated tables and owner guards.
};
} // namespace atlas::localization
