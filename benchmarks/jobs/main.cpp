#include "benchmark.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view{argv[1]} == "--help")
        {
            std::cout << "atlas_jobs_benchmark [--smoke] [--size N] [--grain N] [--rounds N] "
                         "[--workers N] [--seed N] [--repetitions N] [--machine LABEL]\n";
            return 0;
        }
        const auto options = atlas::jobs::workloads::parse_options(argc, argv);
        const auto result = atlas::jobs::workloads::measure(options);
        atlas::jobs::workloads::write_report(std::cout, options, result);
        return std::cout ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        try
        {
            std::cerr << error.what() << '\n';
        }
        catch (...)
        {
            return 3;
        }
        return 1;
    }
    catch (...)
    {
        return 2;
    }
}
