#include <atlas/application/EntryPoint.hpp>

int main(int argc, char** argv)
{
    return atlas::application::run_application(atlas::application::ApplicationKind::Editor, argc,
                                               argv);
}
