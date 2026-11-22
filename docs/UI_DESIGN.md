# TeamSync desktop interface

TeamSync remains a C++17 application built with Qt 6 Widgets and Qt Networking. The interface uses the existing `QMainWindow`, signal/slot connections, service layer, and workspace files. The native Windows title bar is retained.

## Interface structure

`TeamSyncGui` builds the application shell and its pages. `QStackedWidget` provides page switching without rebuilding the workspace or signing the user out.

`TeamSyncGuiWorkPages.cpp` keeps the Projects/Tasks filters, tables, and row actions together. `PremiumWidgets` provides shared painted badges, checkboxes, derived progress, initial avatars, metric icons, the profile menu control, vector backdrop, and animation helpers. Page crossfades use a temporary snapshot overlay; dialog entrances move only top-level windows, never layout-owned pages. Reduced motion disables those transitions.

The main navigation contains Overview, Teams, Projects, Tasks, Shared files, Reports, Activity, and Connection. Settings and Help & Tutorial sit below it; the bottom profile menu opens My profile, Settings, or Sign out. Chat and notifications remain available through their dedicated buttons and windows.

The sign-in page follows the supplied showcase: a blue-white collaboration panel on the left and an elevated sign-in card on the right. It retains username/password sign-in, Windows-protected remembered sign-in, password recovery, registration with security questions, guest access, and Local/Host/Join actions. Connecting a computer and joining a team remain separate operations: the first selects the workspace; the second grants membership of a team inside it.

## Visual system

`ThemeManager` owns the application palette, typography, Qt style sheet, and icon rendering. `apply(QWidget*, bool)` updates the complete application, including dialogs. `icon(name, dark, accent, onAccent)` renders the resource icons in the requested theme; the last two arguments are optional. `applicationIcon()` supplies the taskbar/window identity, and `systemPrefersDark()` reads the supported system appearance. Add shared styling there rather than setting colors in individual page functions.

The light palette uses a cool blue-white background, white elevated surfaces, navy text, delicate borders, and blue accents. The dark palette uses deep charcoal backgrounds and slightly lighter elevated panels. Soft shadows, restrained surface gradients, colored icon containers, and status badges provide the depth shown in the supplied showcase. Theme preferences are stored through `AppSettings`, which preserves the normal `QSettings` location and provides explicit INI files for automated reviews. System appearance is used where supported by Qt and the operating system.

Use Segoe UI Variable where installed, with Segoe UI as a fallback and a consistent distinction between page titles, section headings, normal text, and secondary information. Reserve monospaced text for generated report content. Keep ordinary buttons and inputs compact, with visible keyboard focus and approximately 10–12-pixel radii on elevated cards.

Layouts follow a 4-pixel spacing rhythm: 4, 8, 12, 16, 20, 24, and 32 pixels. Prefer layout margins and stretch factors to absolute positions. Tables should elide long text, expose it through tooltips, and retain horizontal scrolling when their columns cannot fit.

Lucide SVG icons are embedded in Qt resources and rendered using the installed Qt SVG module. `resources/ICON_ATTRIBUTION.md` identifies the source revision and included icons; `resources/LICENSE-Lucide.txt` preserves the ISC and Feather MIT notices. Use the centralized icon helper so icons follow the active theme; do not introduce a second icon family or font-dependent glyphs.

## Page conventions

Use a compact toolbar above a table or list for structured information. Place creation actions near the relevant collection and selection actions near the selected record. Keep destructive actions visually distinct and preserve confirmations.

Overview displays real workspace data. Assigned work comes from the current user's tasks; accessible projects and team activity follow service-layer membership rules. Project completion is derived from task state. No demonstration records are created by opening the application.

The reference overview has a sidebar approximately 17% of window width, four summary cards across the top, and a main content grid whose left and right columns have an approximately 1.8:1 ratio. Task and project information occupy the wider column; activity and team updates occupy the narrower column. At a 1280-pixel window width, the sidebar target is approximately 220 pixels. Match these proportions before refining individual borders or icon alignment.

Empty states should explain what is missing and offer a relevant working action. Distinguish an empty workspace from an empty search result. Avoid large decorative containers containing only zeroes.

Dialogs use standard Qt controls, clear labels, tab navigation, an appropriate default action, and Escape/Cancel behavior. Validation should keep the user's entries intact and identify the field or operation that needs attention. Animate appearance and state rather than moving layout-managed page geometry. Hover, selection, page, and dialog effects should settle quickly and respect the reduced-motion setting.

## Connections and state

Host and Join are available from account access and from Connection after sign-in. Nearby workspace discovery is the ordinary join path, with manual address/port fields as a fallback. The six-digit pairing code is still required. This redesign does not change the LAN protocol, pairing rules, data format, or storage locations.

Keep service objects and the signed-in user consistent when changing workspaces. A `User*` belongs to the current `TeamSyncService`; it must be rebound after a service reload or replacement. A matching account must be authenticated in the destination workspace. Preserve guest behavior and clear the relevant state when a session ends.

Long operations need useful busy feedback and must not allow overlapping workspace mutations. Disable competing actions while an operation is running. Do not let a background refresh invalidate objects referenced by an open dialog.

## Build and review

Build with the existing CMake configuration:

```text
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows, make sure the configured MinGW runtime directory is available on `PATH` before invoking the compiler. The existing post-build deployment copies the Qt runtime beside the GUI executable.

Use isolated workspace and settings directories for GUI automation so verification cannot replace a person's saved sign-in or change their real workspace. Test accounts and fixtures belong in those temporary directories, not in the delivered workspace.

Review light and dark appearances at the minimum window size, normal desktop size, and maximized size. Check all navigation pages, sign-in, registration, recovery, guest access, chat, notifications, and the common editing dialogs. Exercise local, host, and joined modes and verify a change arrives at another client. Inspect screenshots for clipped labels, hidden controls, unreadable focus states, and inconsistent icon contrast.

The core test suite verifies business rules and persistence. GUI automation and visual inspection complement it; a successful build alone does not prove every interface flow or physical Wi-Fi network has been tested. Record the checks actually performed and any remaining hardware/network limitation in the handoff.

## LAN review record — 20 October 2026

The redesigned interface was exercised with separate test host/client workspaces and explicit test INI settings in `build/ui-network-review-20261020`. These fixtures and screenshots are generated review artifacts and are excluded from version control by the existing build-directory rule.

The final isolated runs recorded 20 passing GUI checks across nearby discovery, signed-in hosting, joining before sign-in, joining after sign-in, a task action through the More actions menu, and returning from a joined workspace to local mode. The signed-in identity remained active after the successful matching-account transitions. Re-selecting Host while already hosting safely retained the session.

A client reopened a task. Its saved state changed from In Progress to Pending on the host, and another joined client received the Pending state. The joined and local Connection screenshots were visually inspected, and the four host/client diagnostic streams were empty. Every review process exited normally.

This verification used loopback TCP and discovery through the local machine's network interfaces. It did not use a separate physical computer or prove that every Wi-Fi router, hotspot, or Windows Firewall configuration permits discovery and connection.

## Showcase reference measurements — 21 October 2026

The supplied showcase contains five target scenes: light sign-in, light Overview, light Projects, light Tasks, and dark Overview. It is a collage of differently scaled windows, so compare their composition after normalizing each window rather than treating the collage as one application screenshot.

The large overview window occupies approximately 878 × 643 pixels of the collage. Its 150-pixel sidebar is 17.1% of window width, equivalent to approximately 219 pixels in a 1280-pixel-wide application. The main dashboard columns are approximately 445 and 250 pixels wide, a 1.78:1 ratio. Content margins and gaps normalize to approximately 16 pixels. Four equally sized summary cards precede the two-column content.

The login example assigns approximately 44% of its width to the branded collaboration area and 51% to the sign-in card. The reference card has approximately 28-pixel inner margins at the collage's scale. Its contents include the visibility toggle, remembered sign-in, account creation, guest access, and a distinct workspace connection selector.

Median colors measured from unobstructed reference areas are:

| Element | Light | Dark |
| --- | --- | --- |
| Workspace background | `#F4F8FC` | `#0F151D` |
| Sidebar | `#F0F5FA` | `#151B24` |
| Elevated surface | `#FEFEFE` | `#192029` |
| Selected navigation fill | `#D8E8FC` | `#18457F` |

The light header is approximately `#F8FAFE`; the bright blue selection edge is approximately `#1B7FFE`. These are palette anchors rather than a requirement to flatten the reference's gradients into solid colors.

Automated review fixtures are explicitly created only in an empty, selected test workspace. The expanded fixture contains three projects, five tasks, four members, and three team messages to exercise the reference's information density. Production workspaces are never populated by visual-review startup. Screenshot reports record logical window geometry, device pixel ratio, visible component bounds, and table sizes. Capture timing allows page transitions to settle before visual inspection.

## Reference visual review — 21 October 2026

All five target scenes were captured at a logical size of 1280 × 900 and visually compared with the showcase. The second review set is retained in `build/reference-review-20261021/v2`: light sign-in, light Overview, light Projects, light Tasks, and dark Overview. Each process exited successfully, its automated checks passed, and its diagnostic stream was empty.

The second pass confirmed the wider elevated sign-in card, fully visible connection status, readable two-line profile control, and filled/colored dashboard icon containers. Repeated captures confirmed that the first-pass page-transition ghost and temporarily blank dynamic dashboard sections were no longer present. Both themes showed the real fixture tasks, project completion counts, member initials, activity, and team messages.

Measured implementation geometry at 1280 × 900 includes a 220-pixel sidebar, 99-pixel header, 264-pixel header search field, four approximately 246 × 116-pixel summary cards, and a 530 × 572-pixel sign-in card. The standard body font measured 13 logical pixels. These measurements establish comparable proportions; the screenshots contain the Qt client area, whereas the supplied collage also includes native window frames.

An additional Overview capture at 980 × 720 verified the minimum-size layout and scrolling. A dark Tasks capture with Qt's scale factor set to 1.25 recorded a device pixel ratio of 1.25 and a 1600 × 1125-pixel output image. This is a Qt high-DPI preview, not a test on a second physical monitor or every Windows scaling configuration.

The earlier full-page sweep exercised all eleven application pages in both themes, recording 62 passing checks per theme; the minimum-size guest run recorded four passing checks. Those runs also exercised native maximizing and Qt tab-focus chains. Later source changes require the corresponding final review runs rather than assuming these earlier results cover a new binary.
