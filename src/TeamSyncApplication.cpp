#include "TeamSyncApplication.h"

#include "Dashboard.h"
#include "Exceptions.h"
#include "ReportGenerator.h"
#include "Utils.h"
#include "ConsoleLanSync.h"
#include "WorkspaceRecovery.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace teamsync {
namespace {
std::uint16_t connectionPort(const std::string& text) {
    if (text.empty()) return 45454;
    if (!std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        throw ValidationError("Port must be a whole number from 1 to 65535.");
    try {
        const auto port = std::stoul(text);
        if (port >= 1 && port <= 65535) return static_cast<std::uint16_t>(port);
    } catch (const std::exception&) {}
    throw ValidationError("Port must be a whole number from 1 to 65535.");
}

std::string randomToken() {
    std::random_device source;
    std::ostringstream text;
    text << std::hex << source() << source() << source() << source();
    return text.str();
}

std::filesystem::path joinedRoot(const std::filesystem::path& localRoot, const std::string& address,
                                  const std::uint16_t port) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : address + ':' + std::to_string(port)) { hash ^= c; hash *= 1099511628211ULL; }
    return localRoot / ".teamsync-console-cache" / std::to_string(hash);
}

std::string protectedSession(const std::string& value, const bool protect) {
#ifdef _WIN32
    if (value.empty() || value.size() > 1024 * 1024) return {};
    DATA_BLOB source{static_cast<DWORD>(value.size()), reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))};
    DATA_BLOB target{};
    const BOOL ok = protect ? CryptProtectData(&source, L"TeamSync console saved sign-in", nullptr, nullptr, nullptr,
                                                CRYPTPROTECT_UI_FORBIDDEN, &target) :
                              CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
                                                  CRYPTPROTECT_UI_FORBIDDEN, &target);
    if (!ok) return {};
    const std::string result(reinterpret_cast<char*>(target.pbData), target.cbData);
    SecureZeroMemory(target.pbData, target.cbData);
    LocalFree(target.pbData);
    return result;
#else
    (void)value; (void)protect;
    return {}; // Never fall back to plaintext credentials on unsupported systems.
#endif
}
} // namespace

TeamSyncApplication::TeamSyncApplication(const std::filesystem::path& applicationRoot,
                                         std::istream& in, std::ostream& out)
    : localRoot_(std::filesystem::absolute(applicationRoot).lexically_normal()), activeRoot_(localRoot_),
      service_(std::make_unique<TeamSyncService>(activeRoot_)), input_(in, out), in_(in), out_(out),
      lan_(std::make_unique<ConsoleLanSync>(activeRoot_)) {
    configureTransport(*lan_);
    rememberLogin_ = !std::filesystem::exists(localRoot_ / ".teamsync-console-no-remember");
    startIdleSync();
}

TeamSyncApplication::~TeamSyncApplication() {
    stopIdleSync();
    if (lan_) lan_->disconnect(); // Stop callbacks while all captured members still exist.
}

void TeamSyncApplication::configureTransport(ConsoleLanSync& transport) {
    transport.setRemoteChangeHandler([this] { remoteWorkspaceChanged_ = true; });
    // Scripts never consume keyboard input or emit transient progress lines.
    // Background synchronization updates the cache quietly; only a foreground
    // transfer may write to the interactive console or check the Escape key.
    if (&in_ != &std::cin || &out_ != &std::cout) return;
    struct DisplayState {
        std::chrono::steady_clock::time_point last{};
        std::string phase;
        bool active = false;
    };
    auto display = std::make_shared<DisplayState>();
    auto* source = &transport;
    auto* output = &out_;
    const auto mainThread = mainThreadId_;
    transport.setProgressHandler([source, output, display, mainThread](const ConsoleLanSync::TransferProgress& progress) {
        if (std::this_thread::get_id() != mainThread) return;
#ifdef _WIN32
        if (progress.active && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) source->cancelTransfer();
#endif
        const auto now = std::chrono::steady_clock::now();
        if (progress.active && display->active && display->phase == progress.phase &&
            now - display->last < std::chrono::milliseconds(250)) return;
        if (!progress.active && !display->active) return;
        if (progress.active && !display->active) *output << "\nTransfer started. Press Esc to cancel; Retry sync resumes retained partial files.\n";
        std::ostringstream line;
        line << "\r" << progress.phase;
        if (!progress.file.empty()) line << " | " << progress.file;
        if (progress.total) line << " | " << progress.transferred << '/' << progress.total << " bytes ("
            << std::min<std::uint64_t>(100, progress.transferred * 100 / progress.total) << "%)";
        if (progress.bytesPerSecond > 0) line << " | " << std::fixed << std::setprecision(1)
            << progress.bytesPerSecond / (1024.0 * 1024.0) << " MiB/s";
        line << "                 ";
        if (!progress.active) line << '\n';
        *output << line.str() << std::flush;
        display->last = now; display->phase = progress.phase; display->active = progress.active;
    });
}

void TeamSyncApplication::startIdleSync() {
    if (idleThread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(idleStateMutex_);
        idleStopping_ = false; idleBusy_ = false;
    }
    idleThread_ = std::thread([this] {
        while (true) {
            std::unique_lock<std::mutex> state(idleStateMutex_);
            if (idleCondition_.wait_for(state, std::chrono::seconds(1), [this] { return idleStopping_; })) return;
            state.unlock();
            std::unique_lock<std::mutex> operation(networkMutex_, std::try_to_lock);
            if (!operation.owns_lock()) continue;
            state.lock();
            if (idleStopping_) return;
            if (!lan_->isActive() || lan_->isHost()) { state.unlock(); continue; }
            idleBusy_ = true;
            state.unlock();
            try {
                bool changed = false; std::string error;
                if (lan_->sync(changed, error) && changed) remoteWorkspaceChanged_ = true;
            } catch (const std::exception&) { /* Local work stays queued; foreground Retry explains errors. */ }
            state.lock(); idleBusy_ = false; state.unlock();
            idleCondition_.notify_all();
        }
    });
}

void TeamSyncApplication::stopIdleSync() {
    bool busy = false;
    {
        std::lock_guard<std::mutex> lock(idleStateMutex_);
        idleStopping_ = true; busy = idleBusy_;
    }
    idleCondition_.notify_all();
    if (busy && lan_) lan_->cancelTransfer();
    if (idleThread_.joinable()) idleThread_.join();
}

void TeamSyncApplication::showBanner() const {
    out_ << "\n==================================================\n"
         << "                     TEAMSYNC\n"
         << "     Local Collaboration and Task Management\n"
         << "==================================================\n"
         << "Sudhan Bhattarai & Severoos Nepali | OOP Semester 2\n";
}

int TeamSyncApplication::run() {
    showBanner();
    if (!service_->fileManager().warnings().empty()) {
        out_ << "\nData recovery warnings:\n";
        for (const auto& warning : service_->fileManager().warnings()) out_ << "  - " << warning << '\n';
    }
    safely([this] { WorkspaceRecovery(activeRoot_).createBackup("Automatic daily backup", true); });
    rememberedLogin();
    if (currentUser_) sessionMenu();
    while (true) {
        if (exiting_ || !in_) { out_ << "TeamSync closed safely.\n"; return 0; }
        try {
            out_ << "\n1. Register\n2. Login\n3. Forgot password\n4. Connection mode\n5. Tutorial & Credits\n6. Exit\n"
                 << "7. Continue as guest\n8. Backups & recovery\n9. Saved sign-in settings\n";
            const int choice = input_.integer("Choose: ", 1, 9);
            if (choice == 6) {
                out_ << "TeamSync closed safely.\n";
                return 0;
            }
            if (choice == 1) safely([this] { registerAccount(); });
            if (choice == 2 && login()) sessionMenu();
            if (choice == 3) safely([this] { recoverPassword(); });
            if (choice == 4) safely([this] { connectionMenu(); });
            if (choice == 5) tutorialAndCredits();
            if (choice == 7) safely([this] { guestLogin(); sessionMenu(); });
            if (choice == 8) safely([this] { recoveryMenu(); });
            if (choice == 9) safely([this] { rememberSettings(); });
        } catch (const NavigationRequest&) {
            out_ << "\nYou are already at the welcome menu.\n";
        } catch (const std::exception& error) {
            if (!in_) { out_ << "TeamSync closed safely.\n"; return 0; }
            out_ << "Error: " << error.what() << '\n';
        }
    }
}

void TeamSyncApplication::registerAccount() {
    out_ << "\n--- Register Account ---\n";
    const auto name = input_.line("Full name: ");
    const auto username = input_.line("Username: ");
    const auto email = input_.line("Email (optional): ", true);
    const auto skills = input_.commaSeparated("Skills, comma separated (optional): ");
    const int roleChoice = input_.integer("Role (1 Admin, 2 Team Member): ", 1, 2);
    const auto password = input_.secretLine("Password: ");
    const auto confirmation = input_.secretLine("Confirm password: ");
    std::vector<std::string> securityAnswers;
    out_ << "\nSecurity questions (required for password recovery):\n";
    for (const auto& question : securityQuestions()) {
        securityAnswers.push_back(input_.secretLine(question + " "));
    }
    auto& created = localMutation([&]() -> decltype(auto) { return service_->registerUser(name, username, password, confirmation, email,
        roleChoice == 1 ? UserRole::Admin : UserRole::TeamMember, skills, securityAnswers); });
    out_ << "Account created successfully. Your user ID is " << created.id() << ".\n";
}

void TeamSyncApplication::recoverPassword() {
    out_ << "\n--- Forgot Password ---\n";
    const auto username = input_.line("Username: ");
    if (!service_->findUserByUsername(username)) {
        out_ << "No account exists with that username. Please check the username or register first.\n";
        return;
    }
    if (!service_->findUserByUsername(username)->securityQuestionsConfigured()) {
        out_ << "This account has not configured security questions. Ask the workspace administrator for help.\n";
        return;
    }
    std::vector<std::string> securityAnswers;
    for (const auto& question : securityQuestions()) {
        securityAnswers.push_back(input_.secretLine(question + " "));
    }
    const auto replacement = input_.secretLine("New password: ");
    const auto confirmation = input_.secretLine("Confirm new password: ");
    localMutation([&]() -> decltype(auto) { return service_->resetPassword(username, securityAnswers, replacement, confirmation); });
    if (savedUsername_ == username) clearRememberedLogin();
    out_ << "Password reset successfully. You can sign in now.\n";
}

bool TeamSyncApplication::login() {
    try {
        out_ << "\n--- Login ---\n";
        auto username = input_.line(savedUsername_.empty() ? "Username: " : "Username (Enter uses saved account): ", true);
        if (username.empty()) username = savedUsername_;
        auto password = input_.secretLine("Password (Enter uses saved password, when available): ", true);
        if (password.empty() && username == savedUsername_) password = savedPassword_;
        currentUser_ = &localMutation([&]() -> decltype(auto) { return service_->login(username, password); });
        sessionPassword_ = password;
        guestSession_ = false;
        if (rememberLogin_) {
            try { saveRememberedLogin(); }
            catch (const std::exception& error) { out_ << "Could not remember sign-in: " << error.what() << '\n'; }
        }
        input_.setDashboardAvailable(true);
        out_ << "Login successful. Welcome, " << currentUser_->fullName() << ".\n";
        return true;
    } catch (const NavigationRequest&) {
        out_ << "Returned to the welcome menu.\n";
        currentUser_ = nullptr;
        input_.setDashboardAvailable(false);
        return false;
    } catch (const std::exception& error) {
        out_ << "Login failed: " << error.what() << '\n';
        currentUser_ = nullptr;
        input_.setDashboardAvailable(false);
        return false;
    }
}

void TeamSyncApplication::guestLogin() {
    const auto name = input_.line("Guest display name: ");
    const auto token = randomToken();
    const auto password = "guest-" + token;
    currentUser_ = &localMutation([&]() -> decltype(auto) { return service_->registerUser(name, "guest_" + token.substr(0, 12), password, password, "",
        UserRole::TeamMember, {}, {token + "-a", token + "-b", token + "-c"}); });
    sessionPassword_ = password;
    guestSession_ = true;
    input_.setDashboardAvailable(true);
    out_ << "Guest session started. Guest accounts are not remembered; keep using this session to retain access.\n";
}

void TeamSyncApplication::saveRememberedLogin() {
    if (!currentUser_ || guestSession_ || activeRoot_ != localRoot_) return;
    const auto record = util::encodeField(activeRoot_.generic_u8string()) + '\n' +
        util::encodeField(currentUser_->username()) + '\n' + util::encodeField(sessionPassword_);
    const auto encrypted = protectedSession(record, true);
    if (encrypted.empty()) return;
    const auto path = localRoot_ / ".teamsync-console-session";
    const auto staging = localRoot_ / ".teamsync-console-session.pending";
    for (const auto& target : {path, staging}) {
        std::error_code error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(target, error)))
            throw PersistenceError("Saved sign-in path is a symbolic link; refusing to modify it.");
    }
    std::ofstream output(staging, std::ios::binary | std::ios::trunc);
    output.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
    output.close();
    if (!output) throw PersistenceError("Could not securely save sign-in.");
#ifdef _WIN32
    if (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw PersistenceError("Could not replace saved sign-in.");
#else
    std::filesystem::rename(staging, path);
#endif
    savedUsername_ = currentUser_->username(); savedPassword_ = sessionPassword_;
}

void TeamSyncApplication::clearRememberedLogin() {
    const auto path = localRoot_ / ".teamsync-console-session";
    std::error_code error;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error)))
        throw PersistenceError("Saved sign-in path is a symbolic link; refusing to modify it.");
    error.clear();
    std::filesystem::remove(path, error);
    if (error) throw PersistenceError("Could not clear saved sign-in: " + error.message());
    savedUsername_.clear(); savedPassword_.clear();
}

void TeamSyncApplication::rememberedLogin() {
    if (!rememberLogin_) return;
    const auto path = localRoot_ / ".teamsync-console-session";
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || std::filesystem::is_symlink(path, error) ||
        std::filesystem::file_size(path, error) > 1024 * 1024) return;
    std::ifstream input(path, std::ios::binary);
    const std::string encrypted((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::istringstream record(protectedSession(encrypted, false));
    std::string workspace, username, password;
    if (!std::getline(record, workspace) || !std::getline(record, username) || !std::getline(record, password) ||
        util::decodeField(workspace) != activeRoot_.generic_u8string()) return;
    savedUsername_ = util::decodeField(username); savedPassword_ = util::decodeField(password);
    try {
        currentUser_ = &localMutation([&]() -> decltype(auto) { return service_->login(savedUsername_, savedPassword_); });
        sessionPassword_ = savedPassword_;
        input_.setDashboardAvailable(true);
        out_ << "Signed in securely as " << currentUser_->fullName() << ".\n";
    } catch (const std::exception&) {
        currentUser_ = nullptr;
        safely([this] { clearRememberedLogin(); });
        out_ << "Saved sign-in is no longer valid. Please sign in again.\n";
    }
}

void TeamSyncApplication::rememberSettings() {
    out_ << "\n--- Saved sign-in ---\nWindows account encryption protects saved credentials. Guests are never remembered.\n"
         << "1. Enable and save this account\n2. Disable and clear saved sign-in\n3. Back\n";
    const int choice = input_.integer("Choose: ", 1, 3);
    const auto marker = localRoot_ / ".teamsync-console-no-remember";
    if (choice != 3) {
        std::error_code error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(marker, error)))
            throw PersistenceError("Saved sign-in preference is a symbolic link; refusing to modify it.");
    }
    if (choice == 1) {
        std::filesystem::remove(marker);
        rememberLogin_ = true; saveRememberedLogin();
        out_ << "Remembered sign-in enabled. Only registered local-workspace accounts can be saved.\n";
    }
    if (choice == 2) {
        std::ofstream preference(marker, std::ios::trunc); preference << '0'; preference.close();
        if (!preference) throw PersistenceError("Could not save sign-in preference.");
        rememberLogin_ = false; clearRememberedLogin(); out_ << "Saved sign-in disabled and cleared.\n";
    }
}

void TeamSyncApplication::reloadAuthenticated(const std::string& username, const std::string& password) {
    currentUser_ = nullptr;
    lan_->withWorkspaceLock([&] { service_ = std::make_unique<TeamSyncService>(activeRoot_); });
    if (!username.empty()) {
        try {
            auto* candidate = service_->findUserByUsername(username);
            if (!candidate || candidate->passwordHash() != util::hashPassword(password, candidate->username()))
                throw AuthenticationError("The saved identity is no longer authenticated.");
            currentUser_ = candidate;
        }
        catch (const std::exception&) {
            guestSession_ = false; sessionPassword_.clear();
            out_ << "This workspace has no matching sign-in. Sign in or create an account in this workspace.\n";
        }
    }
    input_.setDashboardAvailable(currentUser_ != nullptr);
}

void TeamSyncApplication::prepareLocalMutation() {
    if (!remoteWorkspaceChanged_.exchange(false)) return;
    const auto username = currentUser_ ? currentUser_->username() : std::string{};
    const auto password = sessionPassword_;
    reloadAuthenticated(username, password);
}

void TeamSyncApplication::syncNetwork(const bool localAction) {
    (void)localAction;
    if (!lan_->isActive() || syncInProgress_) return;
    std::lock_guard<std::mutex> operation(networkMutex_);
    syncInProgress_ = true;
    const std::string username = currentUser_ ? currentUser_->username() : std::string{};
    const auto password = sessionPassword_;
    bool changed = false;
    std::string error;
    bool okay = false;
    try {
        okay = lan_->sync(changed, error);
        const bool remoteChanged = remoteWorkspaceChanged_.exchange(false);
        if (changed || remoteChanged) {
            reloadAuthenticated(username, password);
            out_ << (okay ? "\nWorkspace synchronized.\n" : "\nPreviously received workspace changes loaded from the local cache.\n");
        }
    } catch (const std::exception& exception) { error = exception.what(); }
    syncInProgress_ = false;
    if (!okay) {
        out_ << "\nConnection warning: " << error
             << "\nYour local work is kept. Use Connection > Retry sync when the host is available.\n";
    }
}

void TeamSyncApplication::switchToLocal() {
    const auto username = currentUser_ ? currentUser_->username() : std::string{};
    const auto password = sessionPassword_;
    stopIdleSync();
    if (lan_->isActive() && lan_->hasPendingChanges()) out_ << "Unsynced work is kept in " << activeRoot_.u8string() << ".\n";
    lan_->disconnect();
    activeRoot_ = localRoot_;
    lan_ = std::make_unique<ConsoleLanSync>(activeRoot_);
    configureTransport(*lan_);
    reloadAuthenticated(username, password);
    startIdleSync();
    out_ << "Using this computer's local workspace.\n";
}

void TeamSyncApplication::connectionMenu() {
    while (in_) {
        out_ << "\n--- Connection Mode ---\n"
             << "1. Work locally\n2. Host this workspace\n3. Join a workspace (manual)\n"
             << "4. Show connection status\n5. Disconnect / return to local\n6. Back\n"
             << "7. Find nearby workspaces and join\n8. Retry sync\n9. Use host version (keep local conflict copy)\n";
        const int choice = input_.integer("Choose: ", 1, 9);
        if (choice == 6) return;
        safely([this, choice] {
            if (choice == 1 || choice == 5) {
                switchToLocal();
            } else if (choice == 2) {
                if (activeRoot_ != localRoot_) throw ValidationError("Return to the local workspace before hosting.");
                const auto port = connectionPort(input_.line("TCP port (Enter uses 45454): ", true));
                std::string code, error;
                if (!lan_->host(port, code, error)) throw TeamSyncError(error);
                out_ << "Hosting started. Share the six-digit code; other PCs can find this workspace nearby.\n"
                     << "Host address(es): " << lan_->localAddresses() << "\nPort: " << port << "\nPairing code: " << code
                     << "\nThe host must stay open. GUI and console users can join the same workspace.\n";
            } else if (choice == 3 || choice == 7) {
                std::string address;
                std::uint16_t port = 45454;
                if (choice == 7) {
                    out_ << "Finding nearby workspaces on this Wi-Fi / hotspot...\n";
                    const auto nearby = ConsoleLanSync::discoverNearby();
                    if (nearby.empty()) { out_ << "No nearby host found. Check the host is running and allow TeamSync on your private-network firewall. Manual join is still available.\n"; return; }
                    for (std::size_t index = 0; index < nearby.size(); ++index)
                        out_ << index + 1 << ". " << nearby[index].name << " (" << nearby[index].address << ")\n";
                    const auto& selected = nearby[static_cast<std::size_t>(input_.integer("Select host: ", 1, static_cast<int>(nearby.size())) - 1)];
                    address = selected.address; port = selected.port;
                } else {
                    address = input_.line("Host address: ");
                    port = connectionPort(input_.line("TCP port (Enter uses 45454): ", true));
                }
                const auto code = input_.line("Six-digit pairing code: ");
                if (code.size() != 6 || !std::all_of(code.begin(), code.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
                    throw ValidationError("The pairing code must contain exactly six digits.");
                const auto cache = joinedRoot(localRoot_, address, port);
                auto candidate = std::make_unique<ConsoleLanSync>(cache);
                configureTransport(*candidate);
                std::string error;
                if (!candidate->join(address, port, code, error)) throw TeamSyncError(error);
                stopIdleSync();
                const auto username = currentUser_ ? currentUser_->username() : std::string{};
                const auto password = sessionPassword_;
                if (lan_->hasPendingChanges()) out_ << "Previous unsynced work is retained at " << activeRoot_.u8string() << ".\n";
                lan_->disconnect();
                activeRoot_ = cache;
                lan_ = std::move(candidate);
                configureTransport(*lan_);
                reloadAuthenticated(username, password);
                startIdleSync();
                out_ << "Connected successfully. Shared data is synchronized; your original local workspace is unchanged.\n";
            } else if (choice == 4) {
                out_ << lan_->status() << "\nWorkspace: " << activeRoot_.u8string()
                     << "\nLast successful sync: " << lan_->lastSyncText()
                     << "\nPending local work: " << (lan_->hasPendingChanges() ? "Yes" : "No") << '\n';
                if (lan_->isHost()) out_ << "Host address(es): " << lan_->localAddresses() << "\nPairing code: " << lan_->pairingCode() << '\n';
                for (const auto& peer : lan_->peerAddresses()) out_ << "Connected peer: " << peer << '\n';
                const auto progress = lan_->progress();
                if (progress.active || progress.resumable) out_ << "Transfer: " << progress.phase << " | " << progress.file << " | "
                    << progress.transferred << '/' << progress.total << " bytes | " << progress.bytesPerSecond << " bytes/sec\n";
                input_.pause();
            } else if (choice == 8) {
                syncNetwork(true);
            } else if (choice == 9) {
                if (!lan_->isActive() || lan_->isHost()) throw ValidationError("This option is only for a joined workspace.");
                out_ << "Your current local version will first be preserved in Backups & recovery > Conflicts.\n";
                if (!input_.yesNo("Use the host's version in this joined cache?")) return;
                const auto username = currentUser_ ? currentUser_->username() : std::string{};
                bool changed = false; std::string error;
                if (!lan_->adoptHostAfterPreservingLocal(changed, error)) throw TeamSyncError(error);
                reloadAuthenticated(username, sessionPassword_);
                out_ << "Host version adopted. The previous local conflict copy is retained.\n";
            }
        });
    }
}

void TeamSyncApplication::tutorialAndCredits() const {
    out_ << "\n--- Tutorial & Credits ---\n"
         << "1. Register an account and answer all three security questions.\n"
         << "2. Sign in, create or join a team, then create projects and tasks.\n"
         << "3. Any team member can start, complete, or reopen team tasks. Add notes and files when completing work.\n"
         << "4. Use Team Chat, Shared Files, Notifications, Reports, and Activity Log to coordinate work.\n"
         << "5. For LAN mode, host on one computer and join from other computers on the same trusted Wi-Fi or hotspot.\n"
         << "   Choose Find nearby workspaces and enter the host's six-digit code. Manual address entry is optional.\n"
         << "   GUI and console users can work together. Host or join before or after signing in. The host must stay open.\n"
         << "6. Forgot password: enter an existing username first, then answer its three registered security questions.\n\n"
         << "7. Continue as guest needs only a name. Normal accounts can keep a Windows-encrypted sign-in for next launch.\n"
         << "8. Task board groups Pending, In Progress and Completed. Completion supports notes and multiple attachments.\n"
         << "9. Backups & recovery lists dated automatic/manual backups and preserved local conflict copies.\n"
         << "   Restore only when disconnected, with an administrator account. A pre-restore backup is always retained.\n"
         << "10. If Wi-Fi drops, local work stays saved. Reconnect / Retry sync; interrupted transfers can resume.\n"
         << "    In a conflict, inspect the retained local copy before choosing Use host version.\n"
         << "Navigation: type B for Back or D for Dashboard at any prompt.\n\n"
         << "Developers: Sudhan Bhattarai and Severoos Nepali\n"
         << "Project support: Senior Sudip Khanal\n"
         << "Sudhan portfolio: https://www.sudhanb.com.np\n"
         << "Severoos portfolio: https://www.severoos-nepali.com.np/\n"
         << "Severoos GitHub: https://github.com/Severoos-ui\n";
}

void TeamSyncApplication::recoveryMenu() {
    while (in_) {
        out_ << "\n--- Backups & recovery ---\nWorkspace: " << activeRoot_.u8string()
             << "\n1. Create manual backup\n2. List backups\n3. Restore backup\n4. List conflict copies\n"
             << "5. Restore conflict copy\n6. Show recovery folder\n7. Back\n";
        const int choice = input_.integer("Choose: ", 1, 7);
        if (choice == 7) return;
        safely([this, choice] {
            WorkspaceRecovery recovery(activeRoot_);
            auto print = [this](const std::vector<BackupInfo>& backups) {
                for (const auto& backup : backups) out_ << backup.id << " | " << backup.createdAt << " | "
                    << backup.reason << " | " << backup.fileCount << " files, " << backup.bytes << " bytes"
                    << (backup.automatic ? " | Automatic" : "") << '\n';
                if (backups.empty()) out_ << "No recovery snapshots yet.\n";
            };
            if (choice == 1) {
                const auto reason = input_.line("Backup label (optional): ", true);
                std::string id;
                lan_->withWorkspaceLock([&] { id = recovery.createBackup(reason.empty() ? "Manual backup" : reason); });
                out_ << "Backup saved: " << id << '\n';
            } else if (choice == 2) { print(recovery.listBackups()); input_.pause(); }
            else if (choice == 4) { print(recovery.listConflicts()); input_.pause(); }
            else if (choice == 6) { out_ << recovery.recoveryDirectory().u8string() << '\n'; input_.pause(); }
            else {
                if (lan_->isActive()) throw ValidationError("Disconnect sharing before restoring an entire workspace.");
                if (!currentUser_ || currentUser_->role() != UserRole::Admin)
                    throw AuthorizationError("Only an administrator account may restore an entire workspace.");
                print(choice == 3 ? recovery.listBackups() : recovery.listConflicts());
                const auto id = input_.line("Exact recovery snapshot ID: ");
                if (!input_.yesNo("Replace this workspace with the snapshot? A pre-restore backup will be kept.")) return;
                const auto username = currentUser_->username();
                const auto password = sessionPassword_;
                lan_->withWorkspaceLock([&] {
                    if (choice == 3) recovery.restoreBackup(id); else recovery.restoreConflict(id);
                });
                reloadAuthenticated(username, password);
                out_ << "Workspace restored. The previous version remains in backup history.\n";
                if (!currentUser_) return;
            }
        });
    }
}

void TeamSyncApplication::taskBoardMenu() {
    while (currentUser_ && in_) {
        syncNetwork();
        if (!currentUser_) return;
        const auto tasks = service_->searchTasks(currentUserId(), "");
        out_ << "\n================ TASK BOARD ================\n";
        for (const auto state : {TaskStatus::Pending, TaskStatus::InProgress, TaskStatus::Completed}) {
            out_ << "\n--- " << toString(state) << " ---\n";
            bool empty = true;
            for (const auto* task : tasks) {
                if (task->status() != state && !(state == TaskStatus::Pending && task->status() == TaskStatus::Overdue)) continue;
                empty = false;
                out_ << '#' << task->id() << ' ' << task->title() << " | " << service_->project(task->projectId()).title()
                     << " | " << toString(task->priority()) << " | Due " << task->dueDate().toString()
                     << (task->status() == TaskStatus::Overdue ? " | OVERDUE" : "") << '\n';
            }
            if (empty) out_ << "No tasks in this column.\n";
        }
        out_ << "\n1. Refresh board\n2. Move task to another status\n3. Add comment\n4. View task notes and attachments\n5. Back\n";
        const int choice = input_.integer("Choose: ", 1, 5);
        if (choice == 5) return;
        if (choice == 1) continue;
        safely([this, choice] {
            const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
            // Validate membership before exposing task details, even for a guessed ID.
            const auto files = service_->attachmentsForTask(currentUserId(), id);
            if (choice == 2) {
                const int status = input_.integer("Move to (1 Pending, 2 In Progress, 3 Completed): ", 1, 3);
                if (status == 1) localMutation([&]() -> decltype(auto) { return service_->reopenTask(currentUserId(), id); });
                if (status == 2) localMutation([&]() -> decltype(auto) { return service_->startTask(currentUserId(), id); });
                if (status == 3) {
                    const auto note = input_.line("Completion note/content (optional): ", true);
                    std::vector<std::filesystem::path> paths;
                    while (input_.yesNo("Add a completion file?")) paths.emplace_back(std::filesystem::u8path(input_.line("File path: ")));
                    localMutation([&]() -> decltype(auto) { return service_->markTaskComplete(currentUserId(), id, note, paths); });
                }
                out_ << "Task moved. Project status and progress are calculated automatically.\n";
            } else if (choice == 3) {
                const auto comment = input_.line("Comment: ");
                localMutation([&]() -> decltype(auto) { return service_->addTaskComment(currentUserId(), id, comment); });
                out_ << "Comment added.\n";
            } else {
                const auto& task = service_->task(id);
                out_ << task << "\nDescription: " << task.description() << "\nCompletion content: " << task.completionNote() << '\n';
                for (const auto& comment : task.comments()) out_ << "Comment: " << comment << '\n';
                for (const auto* file : files) out_ << "Attachment: " << file->fileName() << " | "
                    << service_->attachmentPath(currentUserId(), file->id()).u8string() << '\n';
                input_.pause();
            }
        });
    }
}

void TeamSyncApplication::searchMenu() {
    const auto query = input_.line("Search projects, tasks, teams and shared files: ");
    out_ << "\nProjects:\n";
    for (const auto* project : service_->searchProjects(currentUserId(), query)) out_ << "  " << *project << '\n';
    out_ << "\nTasks:\n";
    displayTasks(service_->searchTasks(currentUserId(), query));
    out_ << "\nMy teams:\n";
    for (const auto* team : service_->teamsForUser(currentUserId())) if (util::containsIgnoreCase(team->name(), query) ||
        util::containsIgnoreCase(team->description(), query)) out_ << "  " << *team << '\n';
    out_ << "\nShared files:\n";
    for (const auto* team : service_->teamsForUser(currentUserId())) {
        for (const auto* file : service_->filesForTeam(currentUserId(), team->id(), query)) out_ << "  " << *file << '\n';
    }
    input_.pause();
}

void TeamSyncApplication::sessionMenu() {
    while (currentUser_) {
        if (exiting_ || !in_) { exiting_ = true; return; }
        try {
            syncNetwork();
            if (!currentUser_) return;
            out_ << "\n================ MAIN MENU ================\n"
                 << "1. Dashboard\n2. My Profile\n3. Teams\n4. Projects\n5. Tasks\n"
                 << "6. Team Chat\n7. Shared Files\n8. Notifications\n9. Reports\n"
                 << "10. Activity Log\n11. Connection mode\n12. Logout\n"
                 << "13. Backups & recovery\n14. Tutorial & Credits\n15. Task board\n16. Search workspace\n"
                 << "17. Saved sign-in settings\n18. Exit (keep saved sign-in)\n";
            const int choice = input_.integer("Choose: ", 1, 18);
            switch (choice) {
                case 1: safely([this] { Dashboard::display(Dashboard::calculate(*service_, currentUserId()), out_); input_.pause(); }); break;
                case 2: profileMenu(); break;
                case 3: teamsMenu(); break;
                case 4: projectsMenu(); break;
                case 5: tasksMenu(); break;
                case 6: chatMenu(); break;
                case 7: filesMenu(); break;
                case 8: notificationsMenu(); break;
                case 9: reportsMenu(); break;
                case 10: activitiesMenu(); break;
                case 11: connectionMenu(); break;
                case 12:
                    safely([this] { localMutation([&]() -> decltype(auto) { return service_->logout(currentUserId()); }); });
                    safely([this] { clearRememberedLogin(); });
                    currentUser_ = nullptr;
                    sessionPassword_.clear(); guestSession_ = false;
                    input_.setDashboardAvailable(false);
                    out_ << "Logged out.\n";
                    break;
                case 13: safely([this] { recoveryMenu(); }); break;
                case 14: tutorialAndCredits(); break;
                case 15: taskBoardMenu(); break;
                case 16: safely([this] { searchMenu(); }); break;
                case 17: safely([this] { rememberSettings(); }); break;
                case 18: exiting_ = true; return;
            }
            syncNetwork(true);
        } catch (const NavigationRequest& navigation) {
            if (navigation.target() == NavigationTarget::Dashboard) {
                out_ << "\n--- Dashboard ---\n";
                safely([this] {
                    Dashboard::display(Dashboard::calculate(*service_, currentUserId()), out_);
                });
            } else {
                out_ << "\nReturned to the dashboard.\n";
            }
        } catch (const std::exception& error) {
            if (!in_) { exiting_ = true; return; }
            out_ << "Error: " << error.what() << '\n';
        }
    }
}

int TeamSyncApplication::currentUserId() const {
    if (!currentUser_) throw AuthenticationError("No user is logged in.");
    return currentUser_->id();
}

int TeamSyncApplication::chooseTeam(const std::string& prompt) {
    listMyTeams();
    return input_.integer(prompt, 1, std::numeric_limits<int>::max());
}

void TeamSyncApplication::listMyTeams() const {
    const auto values = service_->teamsForUser(currentUserId());
    out_ << "\nMy teams:\n";
    if (values.empty()) out_ << "  None. Create a team or join with a code.\n";
    for (const auto* value : values) out_ << "  " << *value
        << (value->isAdmin(currentUserId()) ? " | You are team admin" : "") << '\n';
}

void TeamSyncApplication::displayTeamDetails(const int teamId) const {
    const auto& value = service_->team(teamId);
    if (!value.hasMember(currentUserId())) throw AuthorizationError("You do not belong to this team.");
    out_ << "\n" << value << "\nDescription: " << value.description()
         << "\nCreator: " << service_->user(value.creatorId()).fullName()
         << "\nCreated: " << value.dateCreated() << "\nMembers:\n";
    for (const int memberId : value.memberIds()) {
        const auto& member = service_->user(memberId);
        out_ << "  [" << member.id() << "] " << member.fullName() << " (" << member.username()
             << ") - " << (value.isAdmin(memberId) ? "Team Admin" : "Member") << '\n';
    }
}

void TeamSyncApplication::profileMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- My Profile ---\n1. View profile\n2. Edit profile\n3. Change password\n4. Configure security questions\n5. Back\n";
        switch (input_.integer("Choose: ", 1, 5)) {
            case 1: safely([this] {
                out_ << "\nName: " << currentUser_->fullName() << "\nUsername: " << currentUser_->username()
                     << "\nEmail: " << currentUser_->email() << "\nAccount role: " << toString(currentUser_->role())
                     << "\nSession: " << (guestSession_ ? "Guest" : "Registered account") << "\nSkills: ";
                for (const auto& skill : currentUser_->skills()) out_ << skill << ' ';
                out_ << '\n'; input_.pause();
            }); break;
            case 2: safely([this] {
                const auto name = input_.line("Full name: ");
                const auto email = input_.line("Email (optional): ", true);
                const auto skills = input_.commaSeparated("Skills, comma separated: ");
                localMutation([&]() -> decltype(auto) { return service_->editProfile(currentUserId(), name, email, skills); });
                out_ << "Profile updated.\n";
            }); break;
            case 3: safely([this] {
                const auto current = input_.secretLine("Current password: ");
                const auto replacement = input_.secretLine("New password: ");
                const auto confirmation = input_.secretLine("Confirm new password: ");
                localMutation([&]() -> decltype(auto) { return service_->changePassword(currentUserId(), current, replacement, confirmation); });
                sessionPassword_ = replacement;
                if (rememberLogin_) saveRememberedLogin();
                out_ << "Password changed.\n";
            }); break;
            case 4: safely([this] {
                const auto current = input_.secretLine("Current password: ");
                std::vector<std::string> answers;
                out_ << "Answer all three recovery questions:\n";
                for (const auto& question : securityQuestions()) answers.push_back(input_.secretLine(question + " "));
                localMutation([&]() -> decltype(auto) { return service_->setSecurityAnswers(currentUserId(), current, answers); });
                out_ << "Security questions saved.\n";
            }); break;
            case 5: done = true; break;
        }
    }
}

void TeamSyncApplication::teamsMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- Teams ---\n1. List my teams\n2. Create team\n3. Join team\n"
             << "4. View team details and members\n5. Search teams\n6. Search users\n"
             << "7. Leave team\n8. Remove member (team admin)\n"
             << "9. Promote member (team admin)\n10. Change admin to member (team admin)\n11. Back\n";
        const int choice = input_.integer("Choose: ", 1, 11);
        if (choice == 11) { done = true; continue; }
        safely([this, choice] {
            if (choice == 1) { listMyTeams(); input_.pause(); }
            if (choice == 2) {
                const auto name = input_.line("Team name: ");
                const auto description = input_.line("Description: ", true);
                auto& value = localMutation([&]() -> decltype(auto) { return service_->createTeam(currentUserId(), name, description); });
                out_ << "Team created. ID: " << value.id() << ", join code: " << value.joinCode() << "\n";
            }
            if (choice == 3) {
                const auto code = input_.line("Team ID or join code: ");
                auto& value = localMutation([&]() -> decltype(auto) { return service_->joinTeam(currentUserId(), code); });
                out_ << "Joined " << value.name() << ".\n";
            }
            if (choice == 4) { displayTeamDetails(chooseTeam()); input_.pause(); }
            if (choice == 5) {
                const auto results = service_->searchTeams(input_.line("Search text: "));
                for (const auto* value : results) out_ << "  " << *value << '\n';
                if (results.empty()) out_ << "No matching teams.\n";
                input_.pause();
            }
            if (choice == 6) {
                const auto results = service_->searchUsers(input_.line("Name or username: "));
                for (const auto* value : results) out_ << "  [" << value->id() << "] "
                    << value->fullName() << " (" << value->username() << ") | "
                    << toString(value->role()) << '\n';
                if (results.empty()) out_ << "No matching users.\n";
                input_.pause();
            }
            if (choice == 7) {
                const int id = chooseTeam();
                if (input_.yesNo("Leave this team?")) { localMutation([&]() -> decltype(auto) { return service_->leaveTeam(currentUserId(), id); }); out_ << "Team left.\n"; }
            }
            if (choice == 8) {
                const int id = chooseTeam();
                displayTeamDetails(id);
                const int memberId = input_.integer("Member user ID to remove: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Remove this member?")) { localMutation([&]() -> decltype(auto) { return service_->removeMember(currentUserId(), id, memberId); }); out_ << "Member removed.\n"; }
            }
            if (choice == 9) {
                const int id = chooseTeam();
                displayTeamDetails(id);
                const auto member = input_.integer("Member user ID to promote: ", 1, std::numeric_limits<int>::max());
                localMutation([&]() -> decltype(auto) { return service_->promoteMember(currentUserId(), id, member); });
                out_ << "Member promoted.\n";
            }
            if (choice == 10) {
                const int id = chooseTeam();
                displayTeamDetails(id);
                const auto member = input_.integer("Admin user ID to change to member: ", 1, std::numeric_limits<int>::max());
                localMutation([&]() -> decltype(auto) { return service_->demoteMember(currentUserId(), id, member); });
                out_ << "Member role updated.\n";
            }
        });
    }
}

void TeamSyncApplication::listAccessibleProjects() const {
    auto values = service_->projectsForUser(currentUserId());
    std::sort(values.begin(), values.end(), [](const Project* left, const Project* right) {
        return left->dueDate() < right->dueDate();
    });
    out_ << "\nAccessible projects:\n";
    if (values.empty()) out_ << "  None.\n";
    for (const auto* value : values) out_ << "  " << *value << '\n';
}

void TeamSyncApplication::projectsMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- Projects ---\n1. List projects\n2. Create project\n3. Edit project\n"
             << "4. Archive project\n5. Search projects\n6. Back\n";
        const int choice = input_.integer("Choose: ", 1, 6);
        if (choice == 6) { done = true; continue; }
        safely([this, choice] {
            if (choice == 1) { listAccessibleProjects(); input_.pause(); }
            if (choice == 2) {
                const int teamId = chooseTeam();
                const auto title = input_.line("Project title: ");
                const auto description = input_.line("Description: ", true);
                const auto start = input_.date("Start date");
                const auto due = input_.date("Due date");
                auto& value = localMutation([&]() -> decltype(auto) { return service_->createProject(currentUserId(), teamId, title, description,
                                                      start, due); });
                out_ << "Project created with ID " << value.id() << ".\n";
            }
            if (choice == 3) {
                listAccessibleProjects();
                const int id = input_.integer("Project ID: ", 1, std::numeric_limits<int>::max());
                const auto title = input_.line("Title: ");
                const auto description = input_.line("Description: ", true);
                const auto start = input_.date("Start date");
                const auto due = input_.date("Due date");
                localMutation([&]() -> decltype(auto) { return service_->editProject(currentUserId(), id, title, description, start, due); });
                out_ << "Project updated.\n";
            }
            if (choice == 4) {
                listAccessibleProjects();
                const int id = input_.integer("Project ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Archive this project?")) { localMutation([&]() -> decltype(auto) { return service_->archiveProject(currentUserId(), id); }); out_ << "Project archived.\n"; }
            }
            if (choice == 5) {
                const auto values = service_->searchProjects(currentUserId(), input_.line("Search text: "));
                for (const auto* value : values) out_ << "  " << *value << '\n';
                if (values.empty()) out_ << "No matching projects.\n";
                input_.pause();
            }
        });
    }
}

void TeamSyncApplication::displayTasks(const std::vector<const Task*>& tasks) const {
    out_ << "\n" << std::left << std::setw(8) << "ID" << std::setw(28) << "Title"
         << std::setw(16) << "Priority" << std::setw(16) << "Status"
         << std::setw(14) << "Due" << "Assignee\n"
         << std::string(104, '-') << '\n';
    for (const auto* value : tasks) {
        out_ << std::setw(8) << value->id() << std::setw(28) << value->title().substr(0, 27)
             << std::setw(16) << toString(value->priority()) << std::setw(16) << toString(value->status())
             << std::setw(14) << value->dueDate().toString()
             << service_->user(value->assignedUserId()).fullName() << '\n';
    }
    if (tasks.empty()) out_ << "No tasks found.\n";
}

void TeamSyncApplication::displayTasks(const std::vector<Task*>& tasks) const {
    std::vector<const Task*> values(tasks.begin(), tasks.end());
    displayTasks(values);
}

TaskPriority TeamSyncApplication::choosePriority() {
    return static_cast<TaskPriority>(input_.integer("Priority (1 Low, 2 Medium, 3 High, 4 Urgent): ", 1, 4));
}

TaskStatus TeamSyncApplication::chooseStatus() {
    return static_cast<TaskStatus>(input_.integer("Status (1 Pending, 2 In Progress, 3 Completed, 4 Overdue): ", 1, 4) - 1);
}

TaskType TeamSyncApplication::chooseTaskType() {
    return static_cast<TaskType>(input_.integer("Type (1 General, 2 Development, 3 Research, 4 Documentation): ", 1, 4) - 1);
}

void TeamSyncApplication::tasksMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- Tasks ---\n1. View my assigned tasks\n2. View team tasks\n3. Create task\n"
             << "4. Edit task\n5. Assign/reassign task\n6. Start task\n7. Mark complete\n"
             << "8. Reopen task\n9. Add/view comments\n10. Search tasks\n11. Filter tasks\n"
             << "12. Sort tasks\n13. Smart member recommendation\n14. Delete task\n15. Back\n16. Task board\n";
        const int choice = input_.integer("Choose: ", 1, 16);
        if (choice == 15) { done = true; continue; }
        if (choice == 16) { taskBoardMenu(); continue; }
        safely([this, choice] {
            if (choice == 1) { displayTasks(service_->tasksForUser(currentUserId())); input_.pause(); }
            if (choice == 2) {
                const int teamId = chooseTeam();
                displayTasks(service_->tasksForTeam(teamId));
                input_.pause();
            }
            if (choice == 3) {
                listAccessibleProjects();
                const int projectId = input_.integer("Project ID: ", 1, std::numeric_limits<int>::max());
                displayTeamDetails(service_->project(projectId).teamId());
                const auto type = chooseTaskType();
                const auto title = input_.line("Task title: ");
                const auto description = input_.line("Description: ", true);
                const int assignee = input_.integer("Assign to user ID: ", 1, std::numeric_limits<int>::max());
                const auto priority = choosePriority();
                const auto due = input_.date("Due date");
                const auto skill = input_.line("Required skill (optional): ", true);
                auto& value = localMutation([&]() -> decltype(auto) { return service_->createTask(currentUserId(), projectId, type, title,
                    description, assignee, priority, due, skill); });
                out_ << "Task created with ID " << value.id() << ".\n";
            }
            if (choice == 4) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto title = input_.line("Title: ");
                const auto description = input_.line("Description: ", true);
                const auto priority = choosePriority();
                const auto due = input_.date("Due date");
                const auto skill = input_.line("Required skill (optional): ", true);
                localMutation([&]() -> decltype(auto) { return service_->editTask(currentUserId(), id, title, description, priority, due, skill); });
                out_ << "Task updated.\n";
            }
            if (choice == 5) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                displayTeamDetails(service_->task(id).teamId());
                const int memberId = input_.integer("Member user ID: ", 1, std::numeric_limits<int>::max());
                const auto note = input_.line("Assignment note (optional): ", true);
                localMutation([&]() -> decltype(auto) { return service_->assignTask(currentUserId(), id, memberId, note); });
                out_ << "Task assigned.\n";
            }
            if (choice == 6) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                localMutation([&]() -> decltype(auto) { return service_->startTask(currentUserId(), id); });
                out_ << "Task status changed to In Progress.\n";
            }
            if (choice == 7) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto note = input_.line("Completion note/content (optional): ", true);
                std::vector<std::filesystem::path> files;
                while (input_.yesNo("Add a completion file?")) {
                    files.emplace_back(std::filesystem::u8path(input_.line("File path: ")));
                }
                localMutation([&]() -> decltype(auto) { return service_->markTaskComplete(currentUserId(), id, note, files); });
                out_ << "Task completed.\n";
            }
            if (choice == 8) {
                const auto id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                localMutation([&]() -> decltype(auto) { return service_->reopenTask(currentUserId(), id); });
                out_ << "Task reopened.\n";
            }
            if (choice == 9) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto attachments = service_->attachmentsForTask(currentUserId(), id);
                const auto& value = service_->task(id);
                out_ << value;
                out_ << "Comments/updates:\n";
                for (const auto& comment : value.comments()) out_ << "  - " << comment << '\n';
                if (value.comments().empty()) out_ << "  None.\n";
                if (!value.completionNote().empty()) out_ << "Completion content: " << value.completionNote() << '\n';
                out_ << "Completion files:\n";
                for (const auto* attachment : attachments) out_ << "  - " << attachment->fileName()
                    << " (" << service_->attachmentPath(currentUserId(), attachment->id()).u8string() << ")\n";
                if (attachments.empty()) out_ << "  None.\n";
                if (input_.yesNo("Add a comment?")) {
                    const auto comment = input_.line("Comment: ");
                    localMutation([&]() -> decltype(auto) { return service_->addTaskComment(currentUserId(), id, comment); });
                }
            }
            if (choice == 10) {
                displayTasks(service_->searchTasks(currentUserId(), input_.line("Search text: ")));
                input_.pause();
            }
            if (choice == 11) {
                auto values = service_->searchTasks(currentUserId(), "");
                const auto status = chooseStatus();
                values.erase(std::remove_if(values.begin(), values.end(), [status](const Task* value) {
                    return value->status() != status;
                }), values.end());
                displayTasks(values);
                input_.pause();
            }
            if (choice == 12) {
                auto values = service_->searchTasks(currentUserId(), "");
                const int sort = input_.integer("Sort by (1 Due date, 2 Priority, 3 Status): ", 1, 3);
                std::sort(values.begin(), values.end(), [sort](const Task* left, const Task* right) {
                    if (sort == 1) return left->dueDate() < right->dueDate();
                    if (sort == 2) return static_cast<int>(left->priority()) > static_cast<int>(right->priority());
                    return static_cast<int>(left->status()) < static_cast<int>(right->status());
                });
                displayTasks(values);
                input_.pause();
            }
            if (choice == 13) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                service_->attachmentsForTask(currentUserId(), id); // Validate team membership before showing task details.
                const auto& target = service_->task(id);
                const auto values = service_->recommendMembers(id);
                out_ << "\nTask: " << target.title() << "\nRequired skill: "
                     << (target.requiredSkill().empty() ? "None" : target.requiredSkill())
                     << "\nPriority: " << toString(target.priority()) << "\n\nRecommendations:\n";
                for (const auto& value : values) out_ << "  " << value.memberName << " (#" << value.userId
                    << ") | Skill match: " << value.skillMatchPercent << "% | Active: "
                    << value.activeTasks << " | Completed: " << value.completedTasks
                    << " | Score: " << value.score << "/100\n";
                out_ << "Rule-based score: 40% skill, 25 workload, 20 completion rate, 10 experience, 5 priority fit.\n";
                input_.pause();
            }
            if (choice == 14) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Delete this task permanently?")) { localMutation([&]() -> decltype(auto) { return service_->deleteTask(currentUserId(), id); }); out_ << "Task deleted.\n"; }
            }
        });
    }
}

void TeamSyncApplication::displayMessages(const std::vector<Message>& messages) const {
    if (messages.empty()) out_ << "No messages found.\n";
    for (const auto& value : messages) out_ << '[' << value.dateTime() << "] "
        << service_->user(value.senderId()).fullName() << ": " << value.text()
        << " (#" << value.id() << ")\n";
}

void TeamSyncApplication::chatMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- Team Chat (synchronized when connected) ---\n1. View message history\n"
             << "2. Send message\n3. Search messages\n4. Delete my message\n5. Back\n";
        const int choice = input_.integer("Choose: ", 1, 5);
        if (choice == 5) { done = true; continue; }
        safely([this, choice] {
            const int teamId = chooseTeam();
            if (choice == 1) { displayMessages(service_->messagesForTeam(currentUserId(), teamId)); input_.pause(); }
            if (choice == 2) {
                const auto message = input_.line("Message: ");
                localMutation([&]() -> decltype(auto) { return service_->sendMessage(currentUserId(), teamId, message); }); out_ << "Message saved.\n";
            }
            if (choice == 3) { displayMessages(service_->messagesForTeam(currentUserId(), teamId, input_.line("Search text: "))); input_.pause(); }
            if (choice == 4) {
                displayMessages(service_->messagesForTeam(currentUserId(), teamId));
                const int id = input_.integer("Message ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Delete this message?")) { localMutation([&]() -> decltype(auto) { return service_->deleteMessage(currentUserId(), id); }); out_ << "Message deleted.\n"; }
            }
        });
    }
}

void TeamSyncApplication::filesMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- Shared Files ---\n1. View team files\n"
             << "2. Share file\n3. Search files\n4. Open file\n5. Remove my file\n6. Back\n";
        const int choice = input_.integer("Choose: ", 1, 6);
        if (choice == 6) { done = true; continue; }
        safely([this, choice] {
            const int teamId = chooseTeam();
            auto showFiles = [this](const std::vector<const SharedFile*>& values) {
                for (const auto* value : values) out_ << "  " << *value << "\n    Description: "
                    << value->description() << "\n    Shared: " << value->dateShared() << '\n';
                if (values.empty()) out_ << "No shared files found.\n";
            };
            if (choice == 1) { showFiles(service_->filesForTeam(currentUserId(), teamId)); input_.pause(); }
            if (choice == 2) {
                const auto path = std::filesystem::u8path(input_.line("Existing local file path: "));
                const auto name = input_.line("Display name (blank uses actual name): ", true);
                const auto description = input_.line("Description: ", true);
                auto& value = localMutation([&]() -> decltype(auto) { return service_->shareFile(currentUserId(), teamId, path, name, description); });
                out_ << "File copied into TeamSync and shared as #" << value.id() << ". The original was not changed.\n";
            }
            if (choice == 3) {
                showFiles(service_->filesForTeam(currentUserId(), teamId, input_.line("Search text: ")));
                input_.pause();
            }
            if (choice == 4) {
                showFiles(service_->filesForTeam(currentUserId(), teamId));
                auto& value = service_->sharedFile(input_.integer("File record ID: ", 1, std::numeric_limits<int>::max()));
                if (value.teamId() != teamId) throw AuthorizationError("That file belongs to another team.");
                if (!value.exists()) throw ValidationError("The file has been moved or deleted from its recorded path.");
                if (value.isExecutable()) {
                    out_ << "Warning: this appears to be an executable or script. Opening it may run code.\n";
                    if (!input_.yesNo("Open it anyway?")) return;
                } else if (!input_.yesNo("Open with the system's default application?")) return;
                out_ << (value.openWithDefaultApplication() ? "Open request sent.\n" : "The file could not be opened on this system.\n");
            }
            if (choice == 5) {
                showFiles(service_->filesForTeam(currentUserId(), teamId));
                const int id = input_.integer("File record ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Remove only this metadata record? The original file will not be deleted.")) {
                    localMutation([&]() -> decltype(auto) { return service_->removeSharedFile(currentUserId(), id); });
                    out_ << "Shared-file record removed; original file unchanged.\n";
                }
            }
        });
    }
}

void TeamSyncApplication::notificationsMenu() {
    bool done = false;
    while (!done) {
        if (!currentUser_) return;
        out_ << "\n--- Notifications ---\n1. View all\n2. View unread\n3. Mark all read\n4. Back\n";
        const int choice = input_.integer("Choose: ", 1, 4);
        if (choice == 4) { done = true; continue; }
        safely([this, choice] {
            if (choice == 3) { localMutation([&]() -> decltype(auto) { return service_->markAllNotificationsRead(currentUserId()); }); out_ << "Notifications marked read.\n"; return; }
            const auto values = service_->notificationsForUser(currentUserId(), choice == 2);
            std::queue<Notification*> ordered;
            for (auto* value : values) ordered.push(value);
            while (!ordered.empty()) {
                out_ << "  " << *ordered.front() << '\n';
                ordered.pop();
            }
            if (values.empty()) out_ << "No notifications.\n";
            input_.pause();
        });
    }
}

void TeamSyncApplication::reportsMenu() {
    while (true) {
        if (!currentUser_) return;
        out_ << "\n--- Reports ---\n1. Task completion\n2. Pending tasks\n3. Overdue tasks\n"
             << "4. Team activity\n5. Member contribution\n6. Project status\n"
             << "7. Shared files\n8. Team member list\n9. Back\n";
        const int choice = input_.integer("Choose: ", 1, 9);
        if (choice == 9) return;
        safely([this, choice] {
            const int teamId = chooseTeam();
            const auto type = static_cast<ReportType>(choice);
            ReportGenerator generator(*service_);
            out_ << '\n' << generator.generate(type, currentUserId(), teamId);
            localMutation([&]() -> decltype(auto) { return service_->recordReportGenerated(currentUserId(), teamId, toString(type)); });
            if (input_.yesNo("Export this report to a text file?")) {
                const auto path = generator.exportText(type, currentUserId(), teamId,
                    input_.line("Optional file name (blank for automatic): ", true));
                out_ << "Report exported to: " << path.u8string() << '\n';
            }
            input_.pause();
        });
    }
}

void TeamSyncApplication::activitiesMenu() {
    safely([this] {
        const int teamId = input_.yesNo("Filter activity by one team?") ? chooseTeam() : 0;
        const auto values = service_->activitiesForUser(currentUserId(), teamId);
        out_ << "\n--- Visible Activity Log ---\n";
        for (const auto* value : values) out_ << "  " << *value << '\n';
        if (values.empty()) out_ << "No visible activities.\n";
        input_.pause();
    });
}

} // namespace teamsync
