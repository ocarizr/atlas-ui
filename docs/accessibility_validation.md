# Windows accessibility validation

The supported reference reader is Windows Narrator for the menu-focused accessibility contract.
This is a bounded compatibility statement, not a claim that every editor workflow is accessible.
Editable text, caret/selection, editable numeric ranges, NVDA and generic UIA virtual-item
discovery/realization are outside this increment.

## Repeatable reader fixture

Build `atlas_accessibility_walkthrough` with the Windows integration enabled:

```powershell
cmake --build build/windows-msvc --config Debug --target atlas_accessibility_walkthrough
```

Run `tests/accessibility_host/Debug/atlas_accessibility_walkthrough.exe` from the build directory
with the development dependency DLLs available, as described in
[Windows development](windows_development.md). Start Narrator and open its **Speech recap**
window with Narrator key + Alt + X. The
[Microsoft Narrator guide](https://support.microsoft.com/en-us/accessibility/windows/narrator/complete-guide-to-narrator)
describes this live transcript. It records actual reader output, unlike inspecting a UIA tree.

The fixture uses the public window, channel and platform-selected adapter APIs. Its client area
does not render widgets: it isolates semantic publication, native patterns and reader behavior
from the renderer. It complements an editor walkthrough and the automated UI/layout/scroll tests;
it does not substitute for testing a complete authored menu or virtual-list data source.

The editor component also builds `atlas_editor_accessibility_walkthrough`. Its executable is
in the build's `Debug` directory. This second, reader-only window uses the production
`ui::virtual_list`, `layout_engine`, `input_session` and `accessibility_frame` with 10,000 logical
rows. Home/End request the first/last logical row; Up/Down request an adjacent logical row.
Use the reader's item navigation between currently realized rows. Escape closes it. It uses
real layout, clipping, reveal and physical-slot recycling, without GPU widget painting.

These keyboard shortcuts focus a target without activating it:

| Key | Target |
| --- | --- |
| 1 | Open settings, including its accessible description |
| 2 | Music checkbox |
| 3 | Read-only Volume range |
| 4 | Audio tab |
| 5 | Realized inventory row, revealing its simulated offscreen position |
| 6 | Change language |
| 7 | Recycle the second inventory row |
| 8 | Remove the synthetic second inventory row |
| 9 | Clear the tab selection |

Use the reader's primary action to activate the focused control. **Enter alone has no activation
handler in this fixture.** The `Native request count` semantic text records native provider
requests separately from the last physical input code. This distinguishes native Invoke/Toggle/
SelectionItem delivery from an application keyboard fallback. Escape closes a modal first, or
ends the fixture when no modal is open. Closing the native window also performs ordered shutdown.
The interactive loop is deliberately not registered as an unattended CTest test. The target is
compiled by component CI; a build without a native backend reports that requirement and exits.

The initial tab group has no selected tab. Selecting Audio must announce selection, while
clearing it and refocusing Audio must not retain a stale selected announcement. A tab group
advertises `IsSelectionRequired = false`, since the owner contract permits empty selection.

## Recorded reference session

Validated on November 27, 2026, using Windows desktop x64, DisplayVersion `26H2`, build
`26300.9974`, and Narrator executable version `10.0.26100.8972`. The Windows registry's legacy
`ProductName` field reported `Windows 10 Pro`; the build and executable versions identify the
tested environment precisely. Speech recap was inspected directly; only product-specific
observations are recorded here.

| Scenario | Observation | Surface |
| --- | --- | --- |
| Names and help | Read “Open settings” and “Opens the confirmation dialog”. | Reader fixture |
| Focus and activation | Focusing Music retained its checked state. Reader activation changed it and announced “unchecked”. With Enter fallback removed, native requests increased and the last physical input remained the focus shortcut. | Reader fixture |
| Numeric value | Read Volume as a slider at “25%”. Editing remains unsupported. | Reader fixture |
| Tabs | Audio was first announced without selection, then as “selected” after the reader action. Clearing selection and revisiting it removed that announcement. Native selection was also repeated without Enter fallback. | Reader fixture |
| Modal scope | Native activation exposed only Confirmation dialog and Close dialog as client semantics. Closing restored Open settings. Covered menu controls were absent from the published tree. Native nonclient window controls remain OS-owned. | Reader fixture |
| Realized row reveal | Focusing the already realized offscreen row published visible bounds and read “Inventory row two, 2 of 2”. | Semantic reader fixture, simulated owner reveal |
| Language publication | After owner-side language replacement, read “Ouvrir les options” and “Ouvre la confirmation”. This verifies changed resolved text, not automatic voice/language switching. | Reader fixture |
| Recycled/removed identity | Read “Inventory recycled row, 2 of 2” after the same slot acquired generation 2. Removing it omitted it from the tree. Retained stale-provider rejection is separately covered by automated fixtures. | Reader fixture |
| Production virtual list | Initially exposed rows 1–6 from a 10,000-item source. End revealed and read row 10000, Narrator previous-item navigation focused/read row 9999, and Home returned/read row 1. The first physical slot changed generations 1 → 2 → 3. | Real UI virtual-list walkthrough |
| Cross-panel editor traversal | Read root and element_0 in the hierarchy, then the inspector group and Paint tint control. Reader navigation reached Accessibility. | Atlas Editor |
| Editor action and reconstruction | Reader primary action opened Accessibility; activating Back returned to Accessibility with restored logical focus and a new chrome identity. Speech recap contained both panel names. | Atlas Editor |
| Shutdown | The fixture and editor closed with Narrator attached; the reader remained responsive. The test reader session was then stopped. Provider disconnection and queued-request cancellation also have automated coverage. | Both |

The real virtual-list walkthrough validates bounded realization and reader navigation over
the shared UI implementation. It does not make unrealized rows discoverable or implement
UIA `VirtualizedItem`/`ItemContainer`. Reader traversal can inspect the currently realized
publication; accessing other logical rows requires the application's existing focus/reveal or
scroll operation and a new publication. Narrator reports the position within realized siblings:
row 10000 was announced as “6 of 6”, not “10000 of 10000”. Logical set-position metadata is not
currently published. Do not present these observations as generic virtual-item discovery or a
complete accessibility certification of an authored inventory application.

The editor's single-line draft fields do not expose editable-text UIA patterns. The authoring
canvas remains an inert preview rather than an independently invokable menu. These boundaries
are intentional and must remain visible in integration documentation and compatibility reports.
