#include "TeamSyncApplication.h"
#include "TeamSyncService.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        std::filesystem::path root = ".";
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--data-dir" && i + 1 < argc) root = argv[++i];
            else if (argument == "--help") {
                std::cout << "TeamSync options:\n"
                          << "  --data-dir PATH  Store data and reports below PATH\n"
                          << "  --help           Show this help\n";
                return 0;
            } else {
                std::cerr << "Unknown argument: " << argument << '\n';
                return 2;
            }
        }
        teamsync::TeamSyncApplication application(root, std::cin, std::cout);
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "TeamSync could not continue: " << error.what() << '\n';
        return 1;
    }
}
