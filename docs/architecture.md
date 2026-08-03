# Architecture

## Design goals

The architecture separates input/output, business rules, domain objects, and persistence while avoiding advanced frameworks. A viva explanation can follow one operation from the menu to the service, then to a model and `FileManager`.

```text
Console input/output
        |
TeamSyncApplication + Input
        |
TeamSyncService -------- Dashboard / ReportGenerator
        |
Domain objects (User, Team, Project, Task, Message, ...)
        |
FileManager -> data/*.dat and reports/*.txt
```

## Responsibilities

- `TeamSyncApplication`: menu loops and user-facing confirmations; it does not own business data.
- `Input`: reusable validated lines, integers, dates, lists, and yes/no answers.
- `TeamSyncService`: the application use cases and authorization boundary. It owns record collections and saves after changes.
- `Authentication`: username/password validation, registration, login, profile editing, password changes, and three-question hashed recovery.
- Domain classes: protect their own invariants, such as a non-empty team name, real date, controlled task-status transitions, safe attachment paths, or at least one team admin.
- `FileManager`: the only class that knows data-file formats.
- `Dashboard`: calculates a read-only summary for one user.
- `ReportGenerator`: formats and exports the eight report types.

## Relationships

- A `User` is a `Person`; `Admin` and `Member` are users.
- A derived task is a `Task`; the service owns tasks through `unique_ptr<Task>`.
- A team aggregates user IDs and admin IDs. Users can belong to multiple teams.
- A project belongs to one team and aggregates task IDs.
- Messages, files, and activities refer to a team by ID.
- Notifications refer to one user by ID.
- `ChatRoom` composes messages for a selected team, while the service keeps the persisted master collection.

IDs avoid raw cross-object pointers in persisted records. Lookup methods translate IDs to references and throw `NotFoundError` for invalid references.

## Persistence format

One record is stored per line with `|` fields. `%HH` escapes reserved characters (`|`, `%`, commas, semicolons, and newlines). Integer-ID lists use commas and string lists use semicolons. `FileManager` reconstructs the correct derived user/task type from a stored type field.

Save sequence:

1. Serialize the in-memory collection.
2. Write and flush `name.dat.tmp`.
3. Replace `name.dat` only after the temporary output succeeds.

Load sequence:

1. Treat a missing/empty file as an empty collection.
2. Parse each line inside an exception boundary.
3. Skip and report malformed lines.
4. Observe loaded IDs so future static ID generation cannot collide.
5. Remove duplicate IDs.

## Authorization

Team membership is checked for viewing team projects, tasks, chat, files, activity, and reports. A team admin is required to create/edit/archive projects, create/edit/delete/assign tasks, remove members, or promote members. Any member of the task's team can start, complete, reopen, comment, and attach completion evidence. Users can delete only their own messages and shared-file records.

## Extension boundaries

A future database-backed `FileManager`, network API, or GUI can call the same service methods. Real-time transports can deliver new messages/notifications without changing the `Message` and `Notification` entities. These extensions are intentionally not part of the first version.
