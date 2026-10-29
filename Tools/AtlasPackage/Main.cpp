#include <atlas/application/EntryPoint.hpp>

int main(int argc, char** argv)
{
    return atlas::application::run_tool(atlas::application::ToolKind::Package, argc, argv);
}
