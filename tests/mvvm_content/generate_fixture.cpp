/** @file @brief Build-time fixture generator; filesystem access belongs to this test tool only. */
#include <atlas/mvvm_content/document.hpp>
#include <fstream>
#include <iostream>
#include <iterator>

/// @brief Generate a real compiled test factory from the checked-in XML fixture.
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3 && argc != 4)
        {
            throw std::invalid_argument("expected XML input and generated C++ output");
        }

        std::ifstream input{argv[1], std::ios::binary};
        if (!input)
        {
            throw std::runtime_error("cannot read XML fixture");
        }

        const std::string xml{std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{}};
        const auto source = atlas::mvvm_content::generate_factory_cpp(
            atlas::mvvm_content::parse_xml(xml), argc == 4 ? argv[3] : "make_fixture");
        std::ofstream output{argv[2], std::ios::binary};
        output << source;
        if (!output)
        {
            throw std::runtime_error("cannot write generated fixture");
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        try
        {
            std::cerr << error.what() << '\n';
        }
        catch (...)
        {
            return 1;
        }

        return 1;
    }
    catch (...)
    {
        return 1;
    }
}
