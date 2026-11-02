/** @file @brief SDK-free readiness never reports native presentation as implemented. */
#include <atlas/renderer/renderer.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Portable builds can validate host queues, but cannot stand in for a native GPU soak.
TEST(renderer_readiness, native_gpu_is_explicitly_unavailable)
{
    EXPECT_FALSE(backend_available());
    EXPECT_THROW(device{}, std::runtime_error);
}
} // namespace atlas::renderer::tests
