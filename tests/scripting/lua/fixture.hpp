/** @file @brief Test-only fixture for exercising the public scripting and MVVM boundaries. */
#pragma once
#include <atlas/scripting/domain.hpp>

struct lua_State; ///< Private trusted-runner stack type.

namespace atlas::scripting::tests
{
/// @brief One independent model and interpreter owned by the calling test case.
struct fixture_domain
{
    explicit fixture_domain(const mvvm::schema& schema); ///< Bind the fixture model context.

    std::shared_ptr<mvvm::view_model> model; ///< Committed state and queued model demands.
    domain scripts;                          ///< Restricted production interpreter.
};

/// @brief Test-local references retain full native handles so stale generations stay observable.
struct fixture_instance
{
    fixture_domain* owner;  ///< Borrowed domain within the same fixture scope.
    instance_handle handle; ///< Original handle, including its generation.
};

/// @brief A fresh model, catalog and mailbox for each case, released on every exit path.
struct fixture
{
    fixture(); ///< Set up both coordinator-owned domains with the same schema.

    fixture_domain ui;                       ///< Presentation domain and ViewModel.
    fixture_domain application;              ///< Independent application domain and ViewModel.
    std::vector<fixture_instance> instances; ///< Scoped handle IDs, including retired handles.
    std::vector<instance_work> requests;     ///< Most recent batch drained by the test coordinator.
};

/// @brief Push the native fixture API table onto the privileged test runner stack.
void push_fixture_api(lua_State* state);
} // namespace atlas::scripting::tests
