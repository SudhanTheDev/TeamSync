#include "TeamSyncApplication.h"
#include "TeamSyncService.h"
#include "WorkspaceRecovery.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace {
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc); output << value;
    if (!output) throw std::runtime_error("Could not write fixture");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()};
}
template <typename Function> void rejects(Function&& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}
std::string run(const fs::path& path, const std::string& script) {
    std::istringstream input(script); std::ostringstream output;
    teamsync::TeamSyncApplication application(path, input, output);
    assert(application.run() == 0);
    return output.str();
}

class WaitingScriptBuffer : public std::streambuf {
public:
    void feed(const std::string& value) {
        { std::lock_guard<std::mutex> lock(mutex_); pending_.insert(pending_.end(), value.begin(), value.end()); }
        changed_.notify_all();
    }
    void close() {
        { std::lock_guard<std::mutex> lock(mutex_); closed_ = true; }
        changed_.notify_all();
    }
protected:
    int_type underflow() override {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closed_ || !pending_.empty(); });
        return pending_.empty() ? traits_type::eof() : traits_type::to_int_type(pending_.front());
    }
    int_type uflow() override {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closed_ || !pending_.empty(); });
        if (pending_.empty()) return traits_type::eof();
        const auto value = pending_.front(); pending_.pop_front();
        return traits_type::to_int_type(value);
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<char> pending_;
    bool closed_ = false;
};

template <typename Function> bool waitUntil(Function&& ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < until) {
        try { if (ready()) return true; } catch (const std::exception&) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}
}

int main() {
    {
        std::istringstream script("private answer\nback\n");
        std::ostringstream captured;
        teamsync::Input input(script, captured);
        assert(input.secretLine("Secret: ") == "private answer");
        bool navigated = false;
        try { input.secretLine("Secret: "); }
        catch (const teamsync::NavigationRequest&) { navigated = true; }
        assert(navigated && captured.str().find("private answer") == std::string::npos);
    }
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("teamsync-recovery-tests-" + std::to_string(serial));
    fs::create_directories(root);
    const auto recoveryRoot = root / "recovery";
    write(recoveryRoot / "data" / "users.dat", "original\n");
    write(recoveryRoot / "data" / "tasks.dat", "tasks\n");
    write(recoveryRoot / "data" / "notes.txt", "unmanaged content\n");
    write(recoveryRoot / "task_attachments" / "nested" / "report.txt", "attachment\n");
    write(recoveryRoot / "shared_files_storage" / "file.bin", std::string(200000, 'A'));
    write(recoveryRoot / ".teamsync-console-session", "SECRET MUST NOT ENTER BACKUP");
    write(recoveryRoot / "joined_cache" / "data" / "ignored.dat", "not managed");
    teamsync::WorkspaceRecovery recovery(recoveryRoot);
    const auto first = recovery.createBackup("Initial state");
    auto backups = recovery.listBackups();
    assert(backups.size() == 1 && backups.front().fileCount == 4 && !backups.front().automatic);
    assert(backups.front().reason == "Initial state");
    const auto snapshot = recovery.recoveryDirectory() / "backups" / first / "workspace";
    assert(!fs::exists(snapshot / ".teamsync-console-session"));
    assert(!fs::exists(snapshot / "data" / "notes.txt"));
    assert(!fs::exists(snapshot / "joined_cache"));
    assert(read(snapshot / "shared_files_storage" / "file.bin") == std::string(200000, 'A'));
    const auto daily = recovery.createBackup("Daily", true);
    assert(recovery.createBackup("Daily duplicate", true) == daily);
    write(recoveryRoot / "data" / "users.dat", "changed\n");
    write(recoveryRoot / "data" / "new.dat", "new record\n");
    write(recoveryRoot / "task_attachments" / "new.txt", "new attachment");
    write(recoveryRoot / "shared_files_storage" / "file.bin", "changed bytes");
    const auto conflict = recovery.saveConflict("Two people changed this workspace");
    assert(recovery.listConflicts().size() == 1);
    recovery.restoreBackup(first);
    assert(read(recoveryRoot / "data" / "users.dat") == "original\n");
    assert(!fs::exists(recoveryRoot / "data" / "new.dat"));
    assert(!fs::exists(recoveryRoot / "task_attachments" / "new.txt"));
    assert(read(recoveryRoot / "data" / "notes.txt") == "unmanaged content\n");
    assert(read(recoveryRoot / ".teamsync-console-session") == "SECRET MUST NOT ENTER BACKUP");
    assert(read(recoveryRoot / "shared_files_storage" / "file.bin") == std::string(200000, 'A'));
    assert(recovery.listBackups().size() >= 3); // includes a retained pre-restore version
    recovery.restoreConflict(conflict);
    assert(read(recoveryRoot / "data" / "users.dat") == "changed\n");
    assert(fs::exists(recoveryRoot / "task_attachments" / "new.txt"));
    rejects([&] { recovery.restoreBackup("../data"); });
    rejects([&] { recovery.restoreConflict("../../secrets"); });
    // Corruption is detected before replacing a single live file.
    write(snapshot / "data" / "users.dat", "corrupted snapshot");
    rejects([&] { recovery.restoreBackup(first); });
    assert(read(recoveryRoot / "data" / "users.dat") == "changed\n");

#ifdef _WIN32
    // Match FileManager's Windows save sequence: rename the live record out
    // of the way, then install a fresh temporary record. Reusing hard links in
    // a replacement test is invalid: Windows can deny relinking an inode whose
    // prior directory entry is delete-pending, even with DELETE sharing.
    {
        const auto raceRoot = root / "windows-sharing";
        const auto record = raceRoot / "data" / "activity_logs.dat";
        const auto pending = raceRoot / "data" / "activity_logs.pending";
        write(record, std::string(16 * 1024 * 1024, 'R'));
        std::atomic_bool copying{true};
        std::thread snapshot([&] {
            try { teamsync::WorkspaceRecovery(raceRoot).createBackup("Read-sharing regression"); }
            catch (const std::exception&) { /* Concurrent changes may invalidate an optimistic copy. */ }
            copying = false;
        });
        // Give inventory hashing a head start. Do not use exclusive-open
        // probes: those probes can themselves stop the optimistic reader.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        unsigned sharingFailures = 0, renameFailures = 0, replaced = 0;
        DWORD lastRenameError = ERROR_SUCCESS;
        while (copying && replaced + renameFailures < 100) {
            const auto previous = raceRoot / "data" / ("activity_logs.previous-" + std::to_string(replaced + renameFailures));
            write(pending, "new activity record " + std::to_string(replaced) + "\n");
            if (!MoveFileExW(record.c_str(), previous.c_str(), MOVEFILE_WRITE_THROUGH)) {
                lastRenameError = GetLastError();
                if (lastRenameError == ERROR_SHARING_VIOLATION) ++sharingFailures;
                ++renameFailures;
            } else if (!MoveFileExW(pending.c_str(), record.c_str(), MOVEFILE_WRITE_THROUGH)) {
                lastRenameError = GetLastError();
                if (lastRenameError == ERROR_SHARING_VIOLATION) ++sharingFailures;
                ++renameFailures;
                MoveFileExW(previous.c_str(), record.c_str(), MOVEFILE_WRITE_THROUGH);
            } else {
                ++replaced;
                std::error_code cleanup; fs::remove(previous, cleanup);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        snapshot.join();
        if (replaced == 0 || renameFailures || sharingFailures)
            std::cerr << "Read-sharing regression counts: replaced=" << replaced << ", renameFailures=" << renameFailures
                      << ", sharingFailures=" << sharingFailures << ", lastWindowsError=" << lastRenameError << '\n';
        assert(replaced > 0 && renameFailures == 0 && sharingFailures == 0);
    }
#endif

    const auto consoleRoot = root / "console";
    int adminId = 0, memberId = 0, teamId = 0, taskId = 0;
    {
        teamsync::TeamSyncService service(consoleRoot);
        auto& admin = service.registerUser("Review Admin", "admin", "review-pass", "review-pass", "",
            teamsync::UserRole::Admin, {}, {"school", "pet", "city"});
        adminId = admin.id();
        auto& member = service.registerUser("Review Member", "member", "member-pass", "member-pass", "",
            teamsync::UserRole::TeamMember, {}, {"school", "pet", "city"});
        memberId = member.id();
        auto& team = service.createTeam(adminId, "Test team", "Console parity fixture");
        teamId = team.id(); service.joinTeam(memberId, team.joinCode());
        auto& project = service.createProject(adminId, teamId, "Console project", "Fixture", teamsync::Date::today(),
            teamsync::Date(31, 12, teamsync::Date::today().year() + 1));
        taskId = service.createTask(adminId, project.id(), teamsync::TaskType::General, "Member task", "A normal task",
            memberId, teamsync::TaskPriority::Medium, project.dueDate(), "").id();
    }
    auto output = run(consoleRoot, "3\nunknown-account\n6\n");
    assert(output.find("No account exists with that username") != std::string::npos);
    assert(output.find("What was the name of your first school?") == std::string::npos);
    output = run(consoleRoot, "4\n2\n65536\n6\n6\n");
    assert(output.find("Port must be a whole number from 1 to 65535") != std::string::npos);
    output = run(consoleRoot, "7\nQuick Guest\n15\n5\n18\n");
    assert(output.find("Guest session started") != std::string::npos);
    assert(output.find("TASK BOARD") != std::string::npos);
    assert(!fs::exists(consoleRoot / ".teamsync-console-session"));
    output = run(consoleRoot, "2\nmember\nmember-pass\n15\n2\n" + std::to_string(taskId) +
        "\n3\nFinished from console\nn\n5\n12\n6\n");
    assert(output.find("Task moved") != std::string::npos);
    assert(!fs::exists(consoleRoot / ".teamsync-console-session"));
    {
        teamsync::TeamSyncService service(consoleRoot);
        assert(service.task(taskId).status() == teamsync::TaskStatus::Completed);
        assert(service.task(taskId).completionNote() == "Finished from console");
        assert(service.team(teamId).isAdmin(adminId));
        assert(!service.team(teamId).isAdmin(memberId));
    }
#ifdef _WIN32
    output = run(consoleRoot, "2\nadmin\nreview-pass\n18\n");
    const auto sealed = read(consoleRoot / ".teamsync-console-session");
    assert(!sealed.empty() && sealed.find("review-pass") == std::string::npos);
    output = run(consoleRoot, "12\n6\n");
    assert(output.find("Signed in securely as Review Admin") != std::string::npos);
    assert(!fs::exists(consoleRoot / ".teamsync-console-session"));
#endif
    output = run(consoleRoot, "2\nmember\nmember-pass\n13\n3\n7\n12\n6\n");
    assert(output.find("Only an administrator account") != std::string::npos);
    output = run(consoleRoot, "2\nadmin\nreview-pass\n11\n2\n47576\n6\n12\n6\n");
    assert(output.find("Hosting started") != std::string::npos);
    assert(output.find("Sign out before") == std::string::npos);
    // EOF exits safely instead of spinning forever after a scripted stream ends.
    output = run(consoleRoot, "");
    assert(output.find("closed safely") != std::string::npos);

    // A console joined before sign-in must sync even while std::getline waits
    // for the next menu input. Its original local workspace stays untouched.
    {
        const auto hostRoot = root / "idle-host";
        const auto clientRoot = root / "idle-client";
        write(hostRoot / "data" / "workspace.dat", "initial host data\n");
        write(clientRoot / "data" / "local-only.dat", "my local workspace\n");
        teamsync::ConsoleLanSync host(hostRoot);
        std::string code, error;
        assert(host.host(0, code, error));
        WaitingScriptBuffer buffer;
        std::istream input(&buffer); std::ostringstream captured;
        buffer.feed("4\n3\n127.0.0.1\n" + std::to_string(host.port()) + "\n" + code + "\n6\n");
        int result = -1;
        std::exception_ptr failure;
        std::thread app([&] {
            try { teamsync::TeamSyncApplication application(clientRoot, input, captured); result = application.run(); }
            catch (...) { failure = std::current_exception(); }
        });
        fs::path cache;
        const bool initiallySynced = waitUntil([&] {
            const auto folder = clientRoot / ".teamsync-console-cache";
            if (!fs::exists(folder)) return false;
            for (const auto& item : fs::directory_iterator(folder)) {
                if (read(item.path() / "data" / "workspace.dat") == "initial host data\n") { cache = item.path(); return true; }
            }
            return false;
        });
        bool idleSynced = false;
        if (initiallySynced) {
            write(hostRoot / "data" / "workspace.dat", "host changed while user was idle\n");
            host.noteLocalChange();
            idleSynced = waitUntil([&] { return read(cache / "data" / "workspace.dat") == "host changed while user was idle\n"; });
        }
        buffer.feed("6\n"); buffer.close();
        app.join();
        if (failure) std::rethrow_exception(failure);
        assert(result == 0 && initiallySynced && idleSynced);
        assert(read(clientRoot / "data" / "local-only.dat") == "my local workspace\n");
        assert(!fs::exists(clientRoot / "data" / "workspace.dat"));
        assert(captured.str().find("Connected successfully") != std::string::npos);
    }
    std::cout << "Console parity and recovery tests passed. Fixtures retained at " << root.string() << '\n';
    return 0;
}
