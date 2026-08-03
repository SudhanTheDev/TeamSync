# Development Phases and Verification Log

## Phase 1 — Foundation

Completed the CMake C++17 project, namespace, exception hierarchy, input utilities, abstract `Person`, user hierarchy, validated `Date`, initial executable, and tests.

Key files: `CMakeLists.txt`, `Date.*`, `Utils.*`, `Exceptions.h`, `Person.h`, `User.*`, `Input.*`, `main.cpp`.

Test: configure/build, run `ctest`, then run `teamsync --help`.

## Phase 2 — User system

Completed registration, case-insensitive unique usernames, login/logout, roles, profiles, password changes, required three-question password recovery, non-plain password/answer storage, backward-compatible user persistence, and user activities.

Key files: `Authentication.*`, `FileManager.*`, `TeamSyncService.*`, `TeamSyncApplication.*`.

Test: register, reject a duplicate/mismatch, log in/out, edit profile, restart, and log in again.

## Phase 3 — Teams and projects

Completed create/join/search/view/leave teams, member/admin controls, project create/edit/archive/search/automatic status, permissions, membership notifications, and persistence.

Key files: `Team.*`, `Project.*`, plus service/application methods.

Test: create two accounts, create a team, join by code, create a project as team admin, and verify a regular member is denied the same admin operation.

## Phase 4 — Tasks

Completed the abstract task hierarchy, create/edit/delete/assign, status-based start/completion/reopen, completion notes/files, comments, priorities/statuses, deadlines/overdue detection, filters, three sorts, overloaded search/assign operations, operators, notifications, and rule-based recommendation.

Key files: `Task.*`, `TeamSyncService.*`, `TeamSyncApplication.*`.

Test: assign a derived task, update it as any member of its team, inspect virtual weight/recommendations, complete/reopen it, and restart.

## Phase 5 — Chat and file sharing

Completed saved team messages, sender/time display, search, owner-only deletion, workspace-owned shared-file copies, LAN file transfer, size/existence/executable checks, safe opening confirmation, search, owner-only record removal, and notifications.

Key files: `Message.*`, `SharedFile.*`.

Test: send/search/reload a message; share a harmless text file; verify the workspace copy survives moving the original; verify removal leaves the original file untouched.

## Phase 6 — Dashboard and reports

Completed dashboard statistics, recent messages/activity, contribution estimates, activity logging, eight formatted report categories, and text export.

Key files: `Dashboard.*`, `ReportGenerator.*`, `ActivityLog.*`, `Notification.*`.

Test: open a populated dashboard, compare counts to records, generate each report, and verify exported files under `reports/`.

## Phase 7 — Testing and documentation

Completed the C++ integration suite, clean-workspace smoke flow, corruption recovery test, proposal, architecture, class diagram, test matrix, viva notes, and README.

Verification performed with GCC 13.1 and CMake 3.30:

- Full application and tests compiled and linked.
- `ctest`: 1/1 integration test passed.
- Executable smoke tests register ordinary test accounts, log in, display the dashboard, switch themes, log out, and verify persistence without shipping demonstration accounts.

For repeatable verification, follow `docs/testing.md`.
