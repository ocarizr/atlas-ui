#include <atlas/application/entry_point.hpp>

int main(int argc, char** argv)
{
    return atlas::application::run_application(atlas::application::application_kind::game, argc,
                                               argv);
}
