# Testing Guide

## Automated tests

`tests/teamsync_tests.cpp` is a C++ integration executable. It creates a uniquely named temporary directory, exercises the service and persistence layers, reloads the records, appends one corrupt line, verifies recovery, and removes only its own temporary directory.

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Manual test cases

| ID | Area | Steps | Expected result |
|---|---|---|---|
| REG-01 | Registration | Register with a new username, matching password, and all three security answers | Account and user ID are created; login succeeds |
| REG-02 | Registration | Try an existing username with different letter case | Clear duplicate-username error; no duplicate saved |
| REG-03 | Registration | Leave username/password empty or mismatch confirmation | Input/validation repeats or reports a clear error |
| LOG-01 | Login | Enter valid username/password | Dashboard menu opens and login activity is saved |
| LOG-02 | Login | Enter a wrong password | Generic invalid-credentials error; no password shown |
| LOG-03 | Forgot password | Enter the username, all three matching security answers, and a new password | Old password stops working; new password signs in |
| LOG-04 | Recovery rejection | Enter any incorrect answer or use a migrated account without configured questions | Reset is rejected and the existing password remains unchanged |
| LOG-05 | Legacy recovery setup | Sign into an older eight-field account and configure questions from My profile | New hashes persist and recovery becomes available |
| TEAM-01 | Team creation | Login, create a non-empty team | Creator becomes first member and team admin; join code appears |
| TEAM-02 | Joining | Another account joins by displayed code | Member appears once and receives a notification |
| TEAM-03 | Duplicate join | Join the same team again | Duplicate-membership error |
| TEAM-04 | Admin invariant | Sole admin tries to leave | Operation is rejected because a team needs an admin |
| TEAM-05 | Unauthorized removal | Regular member attempts to remove another member | Authorization error; membership unchanged |
| PROJ-01 | Project creation | Team admin creates project with valid dates | Project is listed and persists |
| PROJ-02 | Invalid dates | Due date is before start date | Validation error; project not created |
| PROJ-03 | Archive | Admin archives a project after confirmation | Status becomes Archived |
| TASK-01 | Task assignment | Admin creates task and assigns a team member | Task and project task ID persist; assignee notified |
| TASK-02 | Non-member assignment | Admin enters a user outside the team | Assignment is rejected |
| TASK-03 | Start work | Assignee selects Start task | Status becomes In Progress and activity is logged |
| TASK-04 | Completion | Any member of the task's team adds optional content/files and marks it complete | Completion date, Completed status, content/files, and creator notification persist |
| TASK-05 | Role-independent work | A regular team member starts/completes a task assigned to another member | Status update succeeds because team membership, not account role, controls task work |
| TASK-05 | Reopen | Assignee reopens completed task | Status becomes Pending and current completion content is cleared |
| TASK-06 | Invalid ID | Enter an unknown task/project/team/user ID | Clear not-found error; menu continues |
| TASK-07 | Sorting | Sort accessible tasks by priority, due date, status | Rows appear in selected order |
| REC-01 | Recommendation | Request recommendation for a task with `C++` skill | Team members are ranked with visible rule-based factors |
| CHAT-01 | Message history | Member sends message, restarts, opens team chat | Sender, timestamp, and original message are restored |
| CHAT-02 | Delete ownership | Try to delete another member's message | Authorization error |
| FILE-01 | Share file | Select an existing file | TeamSync copies it into workspace storage; metadata and size persist; original remains untouched |
| FILE-02 | Missing path | Enter a missing path | Clear error; no record added |
| FILE-03 | Move original | Move the original after sharing and inspect record | TeamSync's workspace copy remains available |
| FILE-04 | Executable | Select Open for `.exe`/`.bat`/similar | Explicit danger warning and confirmation appear first |
| NOTE-01 | Notifications | Open unread list then mark all read | State persists across restart |
| REP-01 | Report view | Select each of eight report types | Formatted report appears with correct team records |
| REP-02 | Export | Export default task report | `.txt` file appears under `reports/`; activity is logged |
| PERS-01 | Persistence | Create records, close, reopen | All important record types remain available |
| PERS-02 | Corruption | Append a malformed line to a `.dat` copy | Startup warns and skips that line without crashing |
| LAN-01 | Host/join | Put two PCs on the same trusted Wi-Fi, host on one, and join from the other | Client receives the host workspace and can sign in with a host account |
| LAN-02 | Round trip | Client starts/completes a task or sends a message | Host receives the saved change and both views refresh automatically |
| LAN-03 | Pairing | Join with an incorrect six-digit code | Host rejects the connection without sending workspace data |
| LAN-04 | Completion file | Complete a task with an attached file on one PC | The other PC receives the copied file and can open it from Completion details |
| LAN-05 | Multi-client | Join two or more client PCs and update work on one | Host and every other client receive the updated task/file/content automatically |

## Clean-workspace smoke test

Use an empty working directory:

```powershell
.\build\teamsync.exe --data-dir .\smoke_data
```

Register two ordinary test accounts with security answers. Create a team/project/task, join from the second account, complete the task as that team member, and verify messages, notifications, dashboard, recommendations, reports, password recovery, and both themes.

## Acceptance checklist

- Build and tests return exit code 0.
- No non-C++ runtime is required.
- Every menu returns to the appropriate parent menu.
- Password hashes, not passwords, appear in `users.dat`; reports show neither.
- Invalid inputs do not terminate the program.
- Data and report paths are relative to the chosen application root.
- Sharing/removing a workspace file never modifies or deletes the original selected file.
- Old task percentages migrate to statuses and new saves contain no manually entered percentage.
- LAN host-to-client download and client-to-host commit both succeed.
