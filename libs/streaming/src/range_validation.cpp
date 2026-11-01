/** @file
 * @brief Shared exact-range validation used before payload allocation and transfer.
 */
#include "asset_reader.hpp"
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Use subtraction so offset plus size cannot overflow.
void validate_range(std::uint64_t length, const read_request& request)
{
    if (request.offset > length || request.size > length - request.offset)
    {
        throw std::out_of_range{"Asset read exceeds the source range"};
    }
}
} // namespace atlas::streaming::detail
