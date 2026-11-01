/** @file
 * @brief Exact file-range acquisition through a platform-neutral storage handle.
 */
#include "asset_reader.hpp"
#include "file_reader.hpp"
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Validate exact ranges using subtraction so offset plus size cannot overflow.
void validate_range(std::uint64_t length, const read_request& request)
{
    if (request.offset > length || request.size > length - request.offset)
    {
        throw std::out_of_range{"Asset read exceeds the source range"};
    }
}

/// @brief Return complete owned bytes or the original acquisition error, never a partial buffer.
read_result read_file_asset(const loose_file_asset& source, const read_request& request,
                            const std::shared_ptr<buffer_reservation>& reservation) noexcept
{
    try
    {
        auto file = open_file(source.path);
        validate_range(file->size(), request);
        auto output = allocate_buffer(reservation);
        file->read(request.offset, *output);
        return output;
    }
    catch (...)
    {
        return std::current_exception();
    }
}
} // namespace atlas::streaming::detail
