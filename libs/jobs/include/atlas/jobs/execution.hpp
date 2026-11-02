/**
 * @file
 * @brief Preflight validation for adapters that coordinate joined borrowed work.
 */
#pragma once

namespace atlas::jobs
{
/// @brief Throw from workers, executing callbacks or terminal observers before admitting work that
/// requires a coordinator wait.
void require_coordinator_thread();
} // namespace atlas::jobs
