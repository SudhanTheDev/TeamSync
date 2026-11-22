# Collaboration reliability and console parity

Implemented 26 October 2026. This update is prepared as the v1.1.0 source and portable Windows release; it does not add an installer.

## Connecting GUI and console users

Both interfaces now call the same native `ConsoleLanSync` engine. `LanSyncManager` is a Qt type adapter, not a separate network implementation. Host in either interface and join from either interface on the same trusted LAN/Wi-Fi/hotspot. Nearby discovery and manual host details use the same versioned protocol.

Update TeamSync on **every participating computer** before connecting. Earlier JSON/binary network versions are not compatible with the new `TEAMSYNC/3` protocol. Existing workspace record formats remain unchanged. The host must remain open; this is still local-network collaboration, not an internet/cloud service.

## Files and synchronization

- Manifests describe managed files; unchanged files do not retransmit.
- Files stream in 256 KiB chunks and are verified with SHA-256.
- Interrupted chunks are retained locally and reused by Retry, including after restarting a client.
- GUI Transfers & sync shows real bytes, speed, Cancel, Retry, pending edits, and explicit conflict resolution.
- Interactive Windows console transfers show progress and support Esc cancellation. Scripted input is not consumed by progress/cancellation checks.
- Shared/completion files may be up to 2 GiB each; one completion submission may total up to 4 GiB. Disk space and network/router constraints still apply.
- Host requests use bounded concurrent workers. Revision checks reject stale commits instead of silently replacing another computer's work.
- Managed paths are limited to `data/*.dat`, `task_attachments`, and `shared_files_storage`; traversal, device paths, symbolic links, and Windows reparse paths are rejected.

Offline edits stay in the joined cache. Retry tries to upload pending changes before downloading newer host data. For conflicts, local edits remain intact; **keep a recovery copy and use the host version** is an explicit user choice. Recovery copies are whole-workspace versions, not an automatic field-by-field merge. Local data is not overwritten when joining a different workspace.

## Backups and recovery

Settings and Connection expose **Backups & recovery**. A startup automatic backup is deduplicated against matching same-day data. The latest 14 automatic versions are retained; manual backups and conflict copies are retained separately. Recovery snapshots are local-only in `.teamsync-recovery`, outside synchronized managed folders.

Recovery copies include managed account/project/task/message data and managed file copies. Protected saved-sign-in files, settings, reports, network caches, and original external source files are excluded. Recovery validates a complete staged snapshot before replacing live records. A permanent pre-restore backup is made first; failures roll back original files.

Only a registered administrator in **local mode** may restore the whole workspace. Stop hosting/return to local before restoring. The GUI requires sign-in again afterwards. Recovery is capped at 64 GiB and 100,000 managed files per snapshot. Keep sufficient free disk space and protect local backup folders as they contain your workspace data.

## Task board and file drops

Tasks now offers **Table** and **Board** views. Both use the same search, scope and status filters. Board columns represent Pending, In progress and Completed; an overdue badge does not move a task out of its actual workflow column.

Drag a card between columns or use its menu/keyboard actions. Completion opens the existing notes/files confirmation; a drop never silently completes work. Drop local files onto a card or the completion list to add attachments. Shared files also accepts external file drops after confirmation. All drops use copy semantics, leaving originals untouched. Team permissions are checked by the existing service actions.

## Console changes

The original numeric menu choices are retained where possible, with new options appended. Console mode now includes:

- Name-only guest sessions.
- Hidden interactive password/security-answer entry with terminal echo restored after navigation or errors.
- Windows-account-encrypted remembered local sign-in, optional saved-password fill, explicit clear/disable, and graceful exit without signing out.
- Host/join before or after sign-in, nearby discovery, validated ports/codes, isolated joined caches, and authenticated identity rebinding.
- Quiet background synchronization while waiting for input; the next menu/action refreshes the display without interrupting typing.
- Connection status, transfer progress, retry, and preserved-conflict host adoption.
- Task-board overview/actions, comments, completion notes and files, task search, recovery history, and current tutorial/credits.
- Administrator/member role controls and existing membership-based task permissions.

GUI and console preferences/remembered sessions are separate local settings. Passwords are never stored in plaintext as a fallback on unsupported systems. Guest sessions are not remembered.

## Verification and limitations

New isolated test targets cover native incremental/resumable transfers, cancellation, revision conflicts, unsafe paths, concurrent clients, discovery, backups/restoration, remembered console sessions, idle console sync, task-board drag/drop and keyboard behavior, and GUI/console host directions. GUI automation uses explicit disposable data/settings roots; no demonstration records are inserted into ordinary workspaces.

Tests run on the current Windows machine using separate processes and loopback/native interfaces. This does not prove every physical PC, hotspot, firewall configuration or real 2 GiB transfer. Validate a transfer between your actual computers before relying on a new deployment. LAN pairing remains intended for trusted local networks; this work does not add TLS or internet hosting.

### Final local verification — 26 October 2026

- Full CMake build succeeded. All five CTest suites passed in the final run (35.05 seconds): service/persistence, console/recovery, native sync, task board, and GUI/console interoperability.
- Native sync additionally passed four simultaneous isolated repeat runs and a debugger run. Coverage includes 32 consecutive short-lived host workers, checksum vectors, pending edits across restart, conflicts, cancellation/resumption, concurrent clients, and discovery during transfers.
- Existing GUI workflow checks passed in both themes (64 checks each); all-page navigation/layout captures passed in both themes (62 each).
- New GUI reliability checks passed for administrator/light, administrator/dark, and member/dark sessions (56, 56, and 55 checks). These exercise actual task transitions, Unicode attachment drops, transfer/recovery dialogs, and restore permissions.
- Six simultaneous fresh sign-in/workflow runs passed (64 checks each) while startup backups ran. Recovery readers permit Windows read/write/delete sharing, so they do not prevent atomic record saves.
- Final light/dark task-board captures passed six checks each and were visually inspected. Page-transition overlays have bounded cleanup even when background animation ticks are throttled.

Reports and screenshots are retained under the ignored `build/` verification folders. Every automated run used isolated data and settings; ordinary workspace data and remembered sign-in settings were not used for test fixtures. No installer is included.
