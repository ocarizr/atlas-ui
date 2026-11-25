/** @file @brief Backend selection never implies universal screen-reader compatibility. */
#include <atlas/accessibility_host/adapter.hpp>
#include <gtest/gtest.h>

namespace atlas::accessibility_host::tests
{
/// @brief Verify reports the compiled native backend.
TEST(accessibility_host_test, ReportsTheCompiledNativeBackend)
{
#if defined(ATLAS_HAS_WINDOWS_ACCESSIBILITY)
    EXPECT_TRUE(available());
#else
    EXPECT_FALSE(available());
#endif
}
} // namespace atlas::accessibility_host::tests
