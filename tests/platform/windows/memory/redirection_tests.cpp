/** @file @brief Explicit PMR integration must not load a process-wide CRT redirection DLL. */
#include <Windows.h>
#include <atlas/memory/memory_resource.hpp>
#include <gtest/gtest.h>

namespace atlas::memory::tests
{
/// @brief The selected memory backend must coexist with the application's existing CRT heaps.
TEST(native_memory_tests, explicit_resource_does_not_load_global_redirector)
{
    const auto resource = make_resource();
    EXPECT_FALSE(resource->backend_name().empty());
    EXPECT_EQ(GetModuleHandleW(L"mimalloc-redirect.dll"), nullptr);
}
} // namespace atlas::memory::tests
