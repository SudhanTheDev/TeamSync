#include "ConsoleLanSync.h"
#include "LanSyncManager.h"

#include <QCoreApplication>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void write(const fs::path& file, const std::string& value) {
    fs::create_directories(file.parent_path()); std::ofstream out(file, std::ios::binary); out << value;
    require(static_cast<bool>(out), "Cannot write interop fixture");
}
std::string read(const fs::path& file) { std::ifstream in(file, std::ios::binary); return {(std::istreambuf_iterator<char>(in)), {}}; }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto root = fs::temp_directory_path() / ("teamsync-interop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        using namespace teamsync;
        {
            const auto hostRoot = root / "gui-host", clientRoot = root / "console-client";
            write(hostRoot / "data" / "records.dat", "GUI host");
            write(hostRoot / "shared_files_storage" / "large.bin", std::string(1024 * 1024, 'A'));
            LanSyncManager host(hostRoot); QString problem;
            require(host.startHost(0, problem, "654321"), problem.toStdString());
            ConsoleLanSync client(clientRoot); std::string error;
            require(client.join("127.0.0.1", host.port(), "654321", error), error);
            require(read(clientRoot / "data" / "records.dat") == "GUI host", "Console failed to receive GUI host data");
            client.executeLocalMutation([&] { write(clientRoot / "data" / "records.dat", "Console change"); });
            client.noteLocalChange(); bool changed = false;
            require(client.sync(changed, error), error);
            require(read(hostRoot / "data" / "records.dat") == "Console change", "GUI host did not receive console update");
        }
        {
            const auto hostRoot = root / "console-host", clientRoot = root / "gui-client";
            write(hostRoot / "data" / "records.dat", "Console host");
            ConsoleLanSync host(hostRoot); std::string code, error;
            require(host.host(0, code, error), error);
            LanSyncManager client(clientRoot); QString problem;
            require(client.joinHost("127.0.0.1", host.port(), QString::fromStdString(code), problem), problem.toStdString());
            require(read(clientRoot / "data" / "records.dat") == "Console host", "GUI failed to receive console host data");
            client.executeLocalMutation([&] { write(clientRoot / "data" / "records.dat", "GUI change"); });
            client.noteLocalChange(); require(client.push(problem), problem.toStdString());
            require(read(hostRoot / "data" / "records.dat") == "GUI change", "Console host did not receive GUI update");
            host.executeLocalMutation([&] { write(hostRoot / "data" / "records.dat", "Host follow-up"); }); host.noteLocalChange();
            bool changed = false; require(client.sync(changed, problem) && changed, problem.toStdString());
            require(read(clientRoot / "data" / "records.dat") == "Host follow-up", "GUI failed to pull subsequent console changes");
        }
        fs::remove_all(root);
        std::cout << "GUI/console interoperability tests passed in both host directions.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
