# Viva Notes

## Why TeamSync was created

Student groups need one local place to divide work, see deadlines, communicate, and preserve basic records. TeamSync turns that real problem into a focused demonstration of C++ object-oriented programming. It works offline and does not hide the concepts behind a framework.

## Main classes

- `Person`: abstract identity (`id`, name, email) and pure `displayProfile` contract.
- `User`: abstract authenticated person with username, hash, skills, role, and creation date.
- `Admin` and `Member`: derived user roles; each overrides virtual behavior.
- `Team`: team identity, creator, members/admins, join code, and invariant that one admin remains.
- `Project`: team-owned work group with dates, status enum, and task IDs.
- `Task`: abstract work item with assignment, priority/status enums, dates, completion content, comments, and weight.
- `TaskAttachment`: safe workspace-relative metadata for a file copied when work is completed.
- Four task subclasses: override `type()` and `calculateWeight()`.
- `Message`/`ChatRoom`: local saved communication and a team-specific message collection.
- `SharedFile`: workspace-owned shared-file metadata with safe existence/executable checks and LAN transfer.
- `Notification` and `ActivityLog`: stored user notices and audit events.
- `Authentication`: account validation, password changes, and three-question hashed recovery.
- `FileManager`: all serialization/deserialization.
- `TeamSyncService`: owns records and implements authorization/use cases.
- `Dashboard` and `ReportGenerator`: read models and formatted output.
- `TeamSyncApplication`: console menus only.
- `LanSyncManager`: Qt TCP host/client snapshot synchronization for a trusted local network.

## Relationships and why they were chosen

Inheritance models **IS-A** relationships: an admin is a user, and a development task is a task. A team has members and projects, so those are aggregation/composition relationships instead of inheritance. Persistent relationships use IDs rather than raw pointers, preventing dangling pointers after vectors move or the program reloads.

## Encapsulation

Data members are private or protected. For example, callers cannot directly append duplicate team members or force an arbitrary task percentage. They must call `Team::addMember`, `Task::start`, or `Task::markCompleted`, where validation protects the object.

## Abstraction

`Person`, `User`, and `Task` define common interfaces without allowing meaningless base objects. Menus use the service rather than knowing storage formats. The service uses `FileManager` rather than scattering stream code across models.

## Inheritance and hierarchical inheritance

```text
Person
`-- User
    |-- Admin
    `-- Member

Task
|-- GeneralTask
|-- DevelopmentTask
|-- ResearchTask
`-- DocumentationTask
```

The virtual destructor in each polymorphic base makes deletion through `unique_ptr<User>` or `unique_ptr<Task>` safe.

## Runtime polymorphism

`TeamSyncService` stores `vector<unique_ptr<Task>>`. `Task::type` and `calculateWeight` are virtual. Calling `calculateWeight()` through a `Task*` selects the derived implementation at runtime. Development work has a 1.20 multiplier, research 1.10, documentation 0.90, and general work the base value. The point is to demonstrate dynamic dispatch, not claim a perfect measurement.

Users are also reconstructed as `Admin` or `Member` objects and accessed through `User` pointers.

## Constructors and destructors

Small record types have default constructors for normal value behavior and parameterized constructors for valid records. Constructors call invariant checks and observe loaded IDs. `Person`, `User`, and `Task` have virtual default destructors. RAII containers and smart pointers release memory automatically; there are no owning raw pointers.

## Function overloading

- `searchTask(int taskId)` finds an exact ID.
- `searchTask(const string& title)` finds a title match.
- `assignTask(...)` has forms with and without an assignment note.

The same operation name is useful because the parameter list clearly describes the variation.

## Operator overloading and friend functions

- `Date` overloads `==`, `<`, `>`, `<=`, and `>=`, making overdue and deadline checks readable.
- `Task::operator<` orders by due date and then priority.
- `Task::operator==` compares stable task IDs.
- Stream insertion (`operator<<`) is a friend of several model classes so formatted display can read private state without exposing writable access.

Operators retain their normal meaning; none performs a surprising side effect.

## Static members and functions

Each important entity has a private static next-ID counter. `generateId()` returns a unique ID. `observeId(id)` moves the counter beyond records loaded from disk, preventing a restart from reusing an old ID.

## STL usage

- `vector`: all persisted collections and member/task ID lists
- `unordered_map`: fast user-ID-to-object lookup in the service
- `queue`: FIFO presentation of stored notifications
- `unique_ptr`: owning polymorphic user/task objects
- `optional<Date>`: a completion date exists only for a completed task
- `set`: duplicate-ID detection and dashboard distinct-member count
- `string`, streams, and `filesystem`: text, formatting, paths, and existence/size checks
- `sort`, `find_if`, `count_if`, `remove_if`, `any_of`: searches, ranking, validation, filtering, and deletion

Notifications are persisted in a vector because read status and saving require iteration, then placed in a `queue` for FIFO presentation.

## File handling

`FileManager` alone uses `ifstream`/`ofstream`. It creates `data`/`reports`, accepts missing or empty files, escapes delimiters, catches parse errors per line, and writes a temporary file before replacement. It reconstructs derived types from stored role/type text. Data remains readable for learning and debugging.

## Password limitation

Plain passwords are not stored. The standard-library-only project applies a username salt and 64-bit FNV-1a hash. FNV is not slow or memory-hard, so it is not suitable for production password storage. A real system should use Argon2id/bcrypt/scrypt from a reviewed security library with random per-user salts. This limitation is stated rather than presenting the educational hash as secure.

## Exception handling

`ValidationError`, `AuthenticationError`, `AuthorizationError`, `NotFoundError`, and `PersistenceError` derive from `TeamSyncError`, which derives from `runtime_error`. Domain/service code throws meaningful errors; menu boundaries catch them and continue instead of terminating.

Examples: duplicate usernames/members, invalid dates or status transitions, wrong login, missing IDs/files, unsafe attachment paths, non-member task assignments, regular-member admin actions, and corrupt file records.

## Contribution score logic

For one member in one team:

```text
score = 50% of completion rate
      + 25% of on-time completion rate
      + up to 20 points for visible activity
      - 5 points per overdue task
      + 5 participation points
```

The result is clamped to 0–100. Reports explicitly label it a TeamSync estimate. Message volume or task counts can be gamed, and difficult work is not fully represented, so the score is not a fair standalone performance judgment.

## Workload recommendation logic

For each member of the task's team:

```text
40 points: required-skill match
25 points: available workload, losing 5 per active task
20 points: previous completion rate
10 points: completed-task experience
 5 points: high-priority fit when active workload is at most two
```

Partial text skill matches receive 70%; exact matches receive 100%. Members are sorted by score and then lower active workload. It is a transparent rule-based recommendation, not artificial intelligence.

## Dashboard and reports

The dashboard presents personal task-status counts, joined projects/teams, unread notices, distinct members, recent messages/activity, and an average personal contribution estimate. No user enters a percentage. Reports use `setw`, `left`, `right`, `fixed`, and `setprecision` for aligned output. Eight categories cover task completion, pending, overdue, activity, contribution, project status, files, and members.

## Limitations

LAN collaboration is GUI-only and requires the host to stay open. The educational TCP pairing protocol is intended only for trusted private Wi-Fi/hotspots and is not internet-grade encrypted. Chat opens from the floating bottom-right button; notices open from the top-right bell. Local password recovery compares three hashed answers and does not send mail. Other limitations include refresh-based messages/notices, educational hashing, day-only dates, and estimated scoring. Non-Windows builds do not launch shared files because constructing a shell command from an arbitrary path would be unsafe.

## Future scope

Internet-grade TLS, real-time push communication, a database repository, a proper password library, cloud/mobile clients, voice input, and an optional assistant can be added later. They should call the existing service interface or replace the persistence adapter.

## Short demonstration plan

1. Register two normal accounts and show the required security questions.
2. Switch between Material dark/light themes and open Tutorial & Credits.
3. Create a team/project and let the second account join.
4. Create a derived task, display its virtual weight, and request recommendations.
5. Start and complete it as the regular team member with a note/file.
6. Show the chat bubble, notification bell, activity, and synchronized shared file.
7. Recover one password, export reports, then demonstrate LAN host/join.
8. Restart and show that all records and the selected theme remain.
