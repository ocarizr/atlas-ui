/**
 * @file
 * @brief Localized text snapshots and explicit owner-side layout metric publication.
 */
#pragma once
#include <atlas/localization/context.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_text/text_layout.hpp>

namespace atlas::ui_text
{
/// @brief Retained text, provenance and glyph metrics from one successful refresh.
struct localized_layout
{
    localization::resolved_text content; ///< Owned UTF-8 and translation direction.
    text_layout layout;                  ///< Owned shaped glyphs and logical size.
};

/// @brief Refresh only on locale/reload/count changes; no per-frame parsing or subscriptions.
class localized_label
{
  public:
    explicit localized_label(localization::context& source, localization::message_key key,
                             text_request font = {}, std::optional<double> count = {},
                             diagnostics::services sinks = {}); ///< Capture font family ownership.
    ~localized_label(); ///< Disconnect before releasing private label state.
    localized_label(const localized_label&) = delete; ///< One label owns its change subscription.
    localized_label& operator=(const localized_label&) = delete; ///< No aliased mutable output.
    /// @brief Retain the last valid shape independently of source and label lifetimes.
    [[nodiscard]] std::shared_ptr<const localized_layout> snapshot() const;
    /// @brief Shape a candidate count using an explicitly live source context.
    void set_count(localization::context& source, std::optional<double> count);
    void refresh(localization::context& source); ///< Retry a failed automatic shape refresh.
    /// @brief Publish natural dimensions through normal owner-side layout invalidation.
    void apply_metrics(ui::element_tree& tree, ui::element_handle target) const;

  private:
    struct impl;                         ///< Owned font family, current shape and owner guard.
    std::shared_ptr<impl> state_;        ///< Callback temporarily retains state during refresh.
    localization::subscription changes_; ///< Destroyed before state; never retains source context.
};
} // namespace atlas::ui_text
