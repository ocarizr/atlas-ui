/**
 * @file
 * @brief Application/tool composition roots and bootstrap argument contracts.
 */

#include <atlas/application/entry_point.hpp>
#include <atlas/jobs/job_system.hpp>
#include <atlas/jobs_diagnostics/observer.hpp>
#include <session.pb.h>

#include <exception>
#include <string>
#include <string_view>

namespace atlas::application
{
/// @brief Compose the selected executable's services and return its bootstrap/smoke-test exit
/// status.
int run_application(application_kind kind, int argc, char** argv)
{
    const std::string_view name = kind == application_kind::game ? "atlas_game" : "atlas_editor";
    auto log = std::make_shared<diagnostics::logger>();
    if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--smoke-test"))
    {
        (void)log->write(diagnostics::log_level::error, "application",
                         "Usage: " + std::string{name} + " [--smoke-test]");
        return 2;
    }

    try
    {
        jobs::job_system_options options;
        options.worker_count = 2;
        options.observer = jobs_diagnostics::make_observer(log, {});
        jobs::job_system scheduler{options};
        preview::SessionInfo session;
        scheduler.dispatcher()
            .run("initialize session", &session,
                 [](jobs::job_context&, preview::SessionInfo*& state)
                 {
                     state->set_protocol_version(1);
                 })
            .wait();
        session.set_session_id(std::string{name});
        scheduler.shutdown();
        (void)log->write(diagnostics::log_level::info, "application",
                         std::string{name} + " bootstrap ready (protocol " +
                             std::to_string(session.protocol_version()) +
                             "); UI and preview services are not implemented");
        return 0;
    }
    catch (const std::exception& error)
    {
        (void)log->write(diagnostics::log_level::error, "application",
                         std::string{name} + " startup failed: " + error.what());
        return 1;
    }
}

/// @brief Validate tool arguments and report commands not implemented by the content-tool
/// bootstrap.
int run_tool(tool_kind kind, int argc, char** argv)
{
    const std::string_view name = kind == tool_kind::cook ? "atlas_cook" : "atlas_package";
    diagnostics::logger log;
    if (argc == 2 && std::string_view{argv[1]} == "--help")
    {
        (void)log.write(
            diagnostics::log_level::info, "application",
            std::string{name} +
                ": content processing is not implemented; use CPack for current packaging");
        return 0;
    }

    (void)log.write(diagnostics::log_level::error, "application",
                    std::string{name} + " has no content commands yet; use --help");
    return 2;
}
} // namespace atlas::application
