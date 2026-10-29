#include <atlas/application/EntryPoint.hpp>
#include <atlas/jobs/JobSystem.hpp>
#include <session.pb.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <string>
#include <string_view>

namespace atlas::application
{
int run_application(ApplicationKind kind, int argc, char** argv)
{
    const std::string_view name = kind == ApplicationKind::Game ? "AtlasGame" : "AtlasEditor";
    if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--smoke-test"))
    {
        spdlog::error("Usage: {} [--smoke-test]", name);
        return 2;
    }
    try
    {
        jobs::JobSystem scheduler{2};
        preview::SessionInfo session;
        scheduler.wait(scheduler.submit(
            [&]
            {
                session.set_protocol_version(1);
                session.set_session_id(std::string{name});
            }));
        scheduler.shutdown();
        spdlog::info(
            "{} bootstrap ready (protocol {}); UI and preview services are not implemented", name,
            session.protocol_version());
        return 0;
    }
    catch (const std::exception& error)
    {
        spdlog::error("{} startup failed: {}", name, error.what());
        return 1;
    }
}

int run_tool(ToolKind kind, int argc, char** argv)
{
    const std::string_view name = kind == ToolKind::Cook ? "AtlasCook" : "AtlasPackage";
    if (argc == 2 && std::string_view{argv[1]} == "--help")
    {
        spdlog::info("{}: content processing is not implemented; use CPack for current packaging",
                     name);
        return 0;
    }
    spdlog::error("{} has no content commands yet; use --help", name);
    return 2;
}
} // namespace atlas::application
