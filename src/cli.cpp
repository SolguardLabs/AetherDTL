#include "aether.hpp"

#include <iostream>

namespace aether {
namespace {

void print_help(std::ostream& out) {
    out << "AetherDTL local intent network\n";
    out << "\n";
    out << "Usage:\n";
    out << "  aetherdtl --list\n";
    out << "  aetherdtl scenario <name>\n";
    out << "  aetherdtl <name>\n";
    out << "\n";
    out << "Scenarios:\n";
    for (const auto& name : scenario_names()) {
        out << "  " << name << "\n";
    }
}

bool has_arg(int argc, char** argv, const std::string& expected) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == expected) {
            return true;
        }
    }
    return false;
}

}  // namespace

int run_cli(int argc, char** argv) {
    try {
        if (argc <= 1 || has_arg(argc, argv, "--help") || has_arg(argc, argv, "-h")) {
            print_help(std::cout);
            return 0;
        }

        const std::string first = argv[1];
        if (first == "--list" || first == "list") {
            for (const auto& name : scenario_names()) {
                std::cout << name << "\n";
            }
            return 0;
        }

        std::string scenario;
        if (first == "scenario") {
            if (argc < 3) {
                std::cerr << "scenario name required\n";
                return 2;
            }
            scenario = argv[2];
        } else {
            scenario = first;
        }

        const auto result = run_scenario(scenario);
        JsonWriter writer(std::cout);
        writer.write_scenario(result);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}

}  // namespace aether
