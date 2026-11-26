/**
 * @file
 * @brief Bounded committed-text editing independent of windows, widgets and navigation.
 */
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace atlas::input
{
/// @brief Single-line UTF-8 edit buffer; commit/cancel and caret policy belong to its consumer.
class text_entry
{
  public:
    explicit text_entry(std::size_t max_bytes = 1024); ///< Positive encoded-byte capacity.
    void begin(std::string_view value); ///< Validate and copy atomically before starting an edit.
    bool append(char32_t scalar); ///< Reject controls, invalid Unicode and overflow unchanged.
    void backspace();             ///< Remove one whole scalar; an empty edit is unchanged.
    std::string_view value() const noexcept; ///< Borrow while no edit operation runs.
  private:
    std::size_t max_bytes_; ///< Hard bound, including complete multibyte sequences.
    std::string text_;      ///< Owned local draft; never aliases authored source values.
};
} // namespace atlas::input
