<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&height=220&color=gradient&customColorList=6,11,20&text=TeamSync&fontSize=70&fontAlignY=38&desc=Organize.%20Collaborate.%20Complete.&descAlignY=60&animation=fadeIn&fontColor=ffffff" width="100%" />

<img src="https://readme-typing-svg.demolab.com?font=Orbitron&weight=700&size=22&duration=3000&pause=900&color=7C3AED&center=true&vCenter=true&width=800&lines=A+Local+Team+Collaboration+Platform;Built+with+C%2B%2B17+and+Qt+6;Manage+Teams%2C+Projects%2C+and+Tasks;Collaborate+over+Wi-Fi+or+Hotspot" alt="TeamSync feature animation" />

<p>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/GUI-Qt%206-41CD52?style=for-the-badge&logo=qt&logoColor=white" alt="Qt 6" />
  <a href="https://github.com/SudhanTheDev/TeamSync/releases/latest"><img src="https://img.shields.io/github/v/release/SudhanTheDev/TeamSync?style=for-the-badge&label=Latest%20release" alt="Latest release" /></a>
  <img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D4?style=for-the-badge&logo=windows&logoColor=white" alt="Windows x64" />
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Limited%20Use-7C3AED?style=for-the-badge" alt="TeamSync Limited Use License" /></a>
</p>

<p><b>Team collaboration with a native desktop GUI and a matching console interface.</b></p>

[Download latest](https://github.com/SudhanTheDev/TeamSync/releases/latest) ·
[Source code](https://github.com/SudhanTheDev/TeamSync) ·
[Report an issue](https://github.com/SudhanTheDev/TeamSync/issues)

</div>

---

## What is TeamSync?

TeamSync is a local-first C++17 desktop app for student groups and small teams. Use its Qt 6 interface or console menus to organize teams, projects, tasks, conversations, and shared files. Computers on the same trusted Wi-Fi network or hotspot can synchronize through the built-in LAN connection.

The GUI and console use the same native synchronization engine and workspace format. TeamSync does not require a cloud account or internet service for LAN collaboration; the hosting computer must remain running and reachable.

## Features

- **Two interfaces:** animated Qt Widgets GUI with light/dark themes, plus a full console workflow.
- **Accounts and access:** security-question password recovery, optional remembered sign-in, guest sessions, team roles, and admin-controlled membership.
- **Teamwork:** teams, projects, status-based tasks, comments, notifications, reports, and Tutorial & Credits.
- **Tasks for everyone:** team members can start, complete, reopen, comment on, and attach completion evidence to team tasks; only admins manage membership and admin-only project/team operations.
- **Task board:** switch between table and Pending / In progress / Completed board views; drag tasks between statuses and drop files onto tasks.
- **LAN collaboration:** host or join before or after sign-in, discover nearby workspaces, or enter connection details manually. Works across interfaces on the same trusted network.
- **Large file reliability:** changed-only manifests, chunked and SHA-256-verified transfers, progress, cancellation, retry/resume, and bounded concurrent clients.
- **Offline safety:** unsent local changes remain queued. Conflicts are explicit; TeamSync does not silently merge or discard local work.
- **Recovery:** dated local backups, conflict copies, and guarded whole-workspace restore.
- **External file safety:** shared and completion files are copied into workspace storage; dropping a file never moves or deletes the user's original.

## Download and run

1. Open [Releases](https://github.com/SudhanTheDev/TeamSync/releases/latest) and download the latest published Windows ZIP.
2. Extract the entire ZIP to a normal folder.
3. Double-click **Run TeamSync.bat**.
4. Choose GUI or console mode.

No installer is included. The v1.1.0 source update is prepared and tested, but its Windows binary is not published yet while the Qt distribution license is being verified. Until then, the Releases page continues to provide the last published portable build. See [v1.1.0 release notes](docs/release-notes/v1.1.0.md) for details.

### Before connecting multiple computers

1. Back up any important existing workspace folder.
2. Install the same current TeamSync version on **every** participating computer.
3. Put them on the same trusted Wi-Fi network or hotspot. Allow TeamSync on Windows **Private networks** if the firewall asks.
4. On one computer select **Host**. On each other computer select **Join** and use discovery or the host's displayed connection details.
5. Keep the host app running while connected.

The current wire protocol is `TEAMSYNC/3`; earlier network versions cannot connect to it. The workspace record format is retained. This is LAN collaboration, not internet hosting; traffic is intended for trusted networks and is not protected by TLS.

Shared/completion transfers are capped at 2 GiB per file and 4 GiB per completion batch by application policy. Real limits also depend on free disk space, Wi-Fi quality, firewalls, and the computers involved.

## Build from source

Requirements: C++17 compiler, CMake 3.16+, and (for the GUI) Qt 6 Widgets, Network, and Svg development libraries.

```sh
cmake -S . -B build -DTEAMSYNC_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

To build only the console version without Qt:

```sh
cmake -S . -B build-console -DTEAMSYNC_BUILD_GUI=OFF -DTEAMSYNC_BUILD_TESTS=ON
cmake --build build-console
ctest --test-dir build-console --output-on-failure
```

On Windows, `Run TeamSync.bat` launches the already-built programs. The optional GUI target is skipped when Qt 6 is not found.

## Data and security notes

TeamSync stores workspace records under its selected data directory. Keep backups private: they can contain account, task, chat, and shared-file data. Remembered-password storage is protected by the operating system when supported; guests are not remembered.

LAN pairing is designed for a trusted local network, not adversarial or public networks. The host has write authority over the shared workspace; clients should only join a host they trust. Transfer integrity and path validation do not replace transport encryption.

See [reliability and console notes](docs/RELIABILITY_AND_CONSOLE.md), [testing guide](docs/testing.md), [architecture](docs/architecture.md), and [v1.1.0 release notes](docs/release-notes/v1.1.0.md).

## License and third-party notices

TeamSync's own source and application are governed by the [TeamSync Limited Use License](LICENSE), not the MIT License. It permits personal/educational non-commercial use of an unmodified copy; source visibility does not grant permission to modify or redistribute TeamSync. Third-party components retain their own licenses; see [third-party notices](docs/THIRD_PARTY_NOTICES.md) and the release package.

## Team and credits

- **Sudhan Bhattarai** — project developer · [Portfolio](https://www.sudhanb.com.np) · [GitHub](https://github.com/SudhanTheDev)
- **Severoos Nepali** — project developer · [Portfolio](https://www.severoos-nepali.com.np) · [GitHub](https://github.com/Severoos-ui)
- **Project support:** Senior Sudip Khanal

Academic project · Second-semester Object-Oriented Programming · Instructor: Ojan Adhikari.

---

For contribution, modification, or redistribution requests, contact the copyright holder first; see [LICENSE](LICENSE).
