/** @file @brief Portable image boundaries reject invalid layouts before native allocation. */
#include <atlas/renderer_content/images.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer_content::tests
{
/// @brief Output bytes must describe exactly one bounded tight RGBA image.
TEST(image_contract, validates_exact_storage_and_dimension_limits)
{
    image_data image;
    EXPECT_THROW(image.validate(), std::invalid_argument);
    image.width = 2;
    image.height = 1;
    image.pixels.resize(8);
    EXPECT_NO_THROW(image.validate());
    EXPECT_THROW(image.validate({1, 8}), std::length_error);
    EXPECT_THROW(image.validate({2, 4}), std::length_error);
    image.pixels.push_back(std::byte{});
    EXPECT_THROW(image.validate(), std::invalid_argument);
}

/// @brief Invalid decode policies are rejected even without an installed backend.
TEST(image_contract, rejects_empty_budgets)
{
    EXPECT_THROW((image_limits{0, 4}.validate()), std::invalid_argument);
    EXPECT_THROW((image_limits{1, 0}.validate()), std::invalid_argument);
}

/// @brief Debug container metadata and pixel allocation both propagate PMR failure safely.
TEST(image_contract, pmr_failure_does_not_terminate)
{
    const auto memory = std::shared_ptr<std::pmr::memory_resource>(std::pmr::null_memory_resource(),
                                                                   [](std::pmr::memory_resource*)
                                                                   {
                                                                   });
    bool failed = false;
    try
    {
        image_data image{memory};
        image.pixels.resize(4);
    }
    catch (const std::bad_alloc&)
    {
        failed = true;
    }

    EXPECT_TRUE(failed);
}
} // namespace atlas::renderer_content::tests
