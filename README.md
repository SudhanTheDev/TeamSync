# TeamSync

**A Local Team Collaboration and Task Management System**

- Developers: Sudhan Bhattarai and Severoos Nepali
- Project Support: Senior Sudip Khanal
- Semester: Second Semester
- Subject: Object-Oriented Programming
- Instructor: Ojan Adhikari
- Language: Standard C++17

## Overview

TeamSync is a local-first C++ collaboration application with both a native Qt desktop interface and a classic console interface. The GUI can also connect computers over the same trusted Wi-Fi network or phone hotspot. Both modes manage team membership, projects, tasks, messages, shared-file metadata, notifications, activity history, workload recommendations, dashboards, and text reports.

All important changes are written to local delimited data files. Closing and reopening the program preserves users, teams, projects, tasks, messages, file records, notifications, and activities.

## Implemented features

- Register, login, logout, view/edit profile, change password, and recover a forgotten password with three personal security questions
- `Person -> User -> Admin/Member` hierarchical inheritance
- Create teams, join by ID or join code, list members, promote/remove members, and leave
- Create, edit, archive, search, and sort projects with automatic Not Started/In Progress/Completed status
- Start, assign, edit, delete, complete, reopen, comment on, search, filter, and sort tasks without manual percentages
- Add written completion content and copied completion-file evidence when finishing a task
- Polymorphic `GeneralTask`, `DevelopmentTask`, `ResearchTask`, and `DocumentationTask`
- Rule-based Smart Member Recommendation using skills, workload, completion history, experience, and priority fit
- Floating team-chat panel with saved messages, search, and owner-only deletion
- Workspace-owned shared files with path/size/existence checks, executable-file confirmation, and LAN transfer
- Dashboard with task counts, team statistics, recent activity/messages, and contribution estimate
- Top-right notification panel with unread count, plus complete activity logging
- In-app quick-start tutorial, credits, and acknowledgement available before and after sign-in
- Material-inspired Android-style interface with persistent dark and light themes
- Every member of a team can start, complete, or reopen that team's tasks; administrative task setup stays admin-only
- Eight formatted report types, viewable in the console and exportable to `reports/`
- Clean-workspace registration flow and C++ integration test suite
- Defensive input, authorization checks, exceptions, corrupted-record recovery, and temporary-file saves
- Host or join a workspace over trusted local Wi-Fi/hotspot using an IP address and six-digit pairing code

## OOP concepts demonstrated

| Concept | TeamSync example |
|---|---|
| Classes and objects | Every domain record is a focused class |
| Encapsulation | Private/protected data with controlled methods and getters |
| Abstraction | Abstract `Person`, `User`, and `Task` types |
| Constructors/destructors | Default/parameterized constructors and virtual destructors |
| Hierarchical inheritance | `Admin`/`Member` and the four task types |
| Runtime polymorphism | `vector<unique_ptr<User>>` and `vector<unique_ptr<Task>>` |
| Pure virtual functions | `Person::displayProfile`, `User::role`, and `Task::type` |
| Function overloading | `searchTask(int/string)` and `assignTask` with/without a note |
| Operator overloading | Date comparisons, task comparison/equality, and stream operators |
| Friend functions | Read-only formatted `operator<<` access |
| Static members | Unique ID generation and ID observation after loading |
| Composition | Service owns records; projects own task IDs; chat rooms own messages |
| STL | `vector`, `unordered_map`, `queue`, `set`, `optional`, algorithms, strings, smart pointers |
| File handling | Central `FileManager` with `ifstream`, `ofstream`, and `filesystem` |
| Exception handling | Domain-specific validation/auth/persistence exceptions |

## Project structure

```text
TeamSync/
|-- CMakeLists.txt
|-- README.md
|-- include/                 Public class declarations
|-- src/                     C++ implementations and console application
|-- tests/teamsync_tests.cpp C++ integration tests
|-- docs/
|   |-- project_proposal.md
|   |-- architecture.md
|   |-- class_diagram.md
|   |-- testing.md
|   `-- viva_notes.md
|-- data/                    Runtime .dat files (created automatically)
`-- reports/                 Exported .txt reports
```

## Requirements

- A C++17 compiler (GCC 8+, Clang 7+, or Visual Studio 2019+)
- CMake 3.16 or newer
- Qt 6 Widgets and Qt Network for the GUI (the console/core remains standard C++17)

## Build

From the `TeamSync` directory:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

For a MinGW Makefiles build on Windows:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## Run

On Windows, double-click **`Run TeamSync.bat`** in the project folder. The launcher asks whether to use:

1. **GUI application** — native windows, buttons, tables, dialogs, a sidebar dashboard, and page animations.
2. **Console mode** — the original keyboard-driven interface with `BACK` and `DASHBOARD` shortcuts.

Both modes use the same accounts, `data/`, and `reports/` folders automatically.

### Work together over Wi-Fi or hotspot

LAN collaboration is available in GUI mode and does not require internet access:

1. Connect both computers to the same Wi-Fi network, or connect both to the same phone hotspot.
2. On the computer that owns the workspace, double-click `Run TeamSync.bat` and select **GUI application**. On the sign-in screen, click **Host**.
3. If Windows Firewall asks, allow TeamSync on **Private networks** only.
4. The host sign-in screen shows one or more IPv4 addresses, port `45454`, and a six-digit pairing code.
5. On every other computer, open the GUI and click **Join** on the sign-in screen. Enter the host IPv4 address, port, and pairing code.
6. A short confirmation popup appears after connection. Sign in with an account stored in the host workspace. Tasks, completion status/content/files, chat updates, notifications, and shared files synchronize automatically for all connected computers.

The host is authoritative and must stay open. The pairing code changes each time hosting starts. Use LAN mode only on a network you trust; this educational LAN protocol is paired but is not internet-grade end-to-end encrypted. If a simultaneous edit conflicts with a newer host change, TeamSync refreshes the client and asks the user to try that action again.

### GUI highlights

- Polished local-first sign-in and registration experience
- Animated dashboard with task and collaboration metrics
- Team, project, task, shared-file, report, activity, connection, tutorial/credits, and profile pages
- Floating chat bubble in the bottom-right and notification bell with unread count in the top-right
- Forgot-password recovery using three required security questions; existing accounts can configure them in My profile
- Persistent dark/light appearance switch with Material cards, navigation pills, inputs, dialogs, and motion
- Dedicated Connection page showing mode, host details, revision, last sync, and connected computer addresses
- Native create/edit forms, tables, confirmations, file chooser, report preview, and status feedback
- Direct reuse of the existing validated service and persistence layer

Run from the project directory so the default `data/` and `reports/` folders remain beside the source:

```powershell
.\build\teamsync.exe
```

Optional commands:

```powershell
.\build\teamsync.exe --data-dir .\my_local_workspace
.\build\teamsync.exe --help
```

## Navigation shortcuts

Every interactive prompt shows the available navigation controls:

- Enter **`B`** or **`BACK`** to cancel the current action or return one menu level.
- Enter **`D`** or **`DASHBOARD`** anywhere after login to return directly to the dashboard.

Navigation cancels an unfinished form before its service action runs, so partially entered
details are not saved.

## Data storage

`FileManager` creates these files only as data is saved:

- `data/users.dat`
- `data/teams.dat`
- `data/projects.dat`
- `data/tasks.dat`
- `data/task_attachments.dat`
- `data/messages.dat`
- `data/shared_files.dat`
- `data/notifications.dat`
- `data/activity_logs.dat`
- `task_attachments/` — copied task-completion evidence
- `shared_files_storage/` — copied team files synchronized to LAN clients
- `network_cache/` — per-host client cache used after joining

Text fields use percent escaping, so delimiters and line breaks do not corrupt the record layout. Each save is written to a `.tmp` file first. Missing/empty files are valid. Bad records are skipped with a warning rather than crashing the whole application. Loaded duplicate IDs are discarded.

Passwords are never printed or stored as plain text. TeamSync uses a username-salted 64-bit FNV-1a educational hash because the project permits only the C++ standard library. **This is not a modern password hash.** A production application should use a reviewed library implementing Argon2id, scrypt, or bcrypt with random salts.

Forgot-password recovery is local: every new account must answer three personal questions. TeamSync normalizes capitalization and surrounding spaces and stores only salted answer hashes. Existing accounts created by an older version continue loading and can configure their questions from **My profile** before using recovery.

When a user shares a file, TeamSync copies it into `shared_files_storage/`. The original selected file is never modified or deleted. The workspace copy synchronizes to every connected LAN client; removing the shared record removes only TeamSync's workspace copy.

Task completion files are different: TeamSync copies them into `task_attachments/<task-id>/` so they remain part of the workspace and synchronize to connected LAN clients. Each completion file is limited to 100 MB and one completion action is limited to 150 MB total.

## Known limitations

- LAN collaboration is available in GUI mode; console mode uses the local workspace
- LAN hosting requires the host application to remain open and the firewall/private network to allow the selected TCP port
- Chat and notifications synchronize about every two seconds rather than using instant push notifications
- Text files rather than a transactional database
- Educational password hashing is weaker than production authentication
- Opening a shared file uses Windows Shell APIs; non-Windows builds still validate and display paths but conservatively do not launch files
- Contribution and recommendation scores are explainable estimates, not objective judgments
- Dates have day precision; no timezone-aware deadlines

## Future improvements

A database server, internet-grade TLS and password libraries, cloud synchronization, mobile clients, instant push notifications, an AI assistant, and voice commands can be added behind the existing service/persistence boundaries without replacing the domain classes.
