#include <atlas/application/entry_point.hpp>

int main(int argc, char** argv)
{
    return atlas::application::run_tool(atlas::application::tool_kind::cook, argc, argv);
}
