/** @file @brief Unsupported platforms report missing accessibility integration explicitly. */
#include <atlas/accessibility_host/adapter.hpp>

namespace atlas::accessibility_host
{
class adapter::impl
{
}; ///< No native endpoint on unsupported platforms.

bool available() noexcept
{
    return false;
} ///< Compile-time backend selection.

adapter::adapter(window::window&, std::shared_ptr<accessibility::channel>, diagnostics::services)
{
    throw std::runtime_error("native accessibility backend unavailable");
} ///< Never pretend support.

adapter::~adapter() = default; ///< Empty platform state.

void adapter::notify()
{
} ///< An unavailable adapter cannot be constructed.

screen_transform adapter::coordinates() const
{
    return {};
} ///< No native coordinates.
} // namespace atlas::accessibility_host
