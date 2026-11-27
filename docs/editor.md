# Atlas Editor

Run `atlas_editor --document path/to/view.xml` to open an authored viewport UI
document. A missing file starts a generic document, and Save creates it at that
path. The default path is `atlas-ui.xml` in the launch directory. Use `--warp`
to select software graphics.

## Flow graph tool

**Flow** opens the Atlas graph workspace; **UI view** returns to element authoring.
`--flow path/to/flow.xml` selects its independent source, defaulting to
`atlas-flow.xml` beside the startup UI document. Graph and UI save/undo/recovery
state remain separate.

- **Screen +** creates a stable-ID node in the selected region. **Choose typed
  asset** cycles compatible catalog references. Unassigned nodes remain saveable.
  Captions, region and initial focus are editable in the inspector.
- Drag nodes to move them. Hold **M** while selecting for multi-selection. Select
  source then destination and press **Connect**. Background drag, or **Space** plus
  drag, pans. **Zoom + / -** zooms around the view center. View changes do not dirty
  the document; a completed multi-node drag creates one undo revision.
- **Region +** creates an owned region when a screen is selected. Its inspector
  defines owner, Submenu Area element, entry screen and remembered tabs. Region
  buttons choose the destination region for new nodes.
- Route rows open the transition inspector: endpoints, history operation,
  enabled state and nested outgoing AND/NOT predicate groups with typed arguments.
- Select a UI element in **UI view**, then its Flow context in the graph. The event
  inspector enables interaction/focus and binds supported events to permitted
  navigation or registered actions. Tab bindings declare their submenu region.
  Existing bindings can be edited/removed. These changes dirty the UI document;
  **Save Flow** saves only the graph.
- Field editing accepts letters/digits, **Backspace** (or **Left**) to erase,
  **Space** for underscore, **Enter** to apply and **Esc** to cancel. It is a bounded
  ASCII editor, not an IME/text editor. Inspector pagination retains access to long
  condition and argument lists.
  **Up/Down** steps a numeric field by one, or by 0.1 when it contains a decimal.
- **Save Flow** accepts complete or incomplete drafts. **Recover** explicitly
  restores a checkpoint; **Discard** removes it. Autosave/window close checkpoints
  both document types. `--recover-flow` restores a checkpoint at startup even when
  the graph source is malformed.

Optional `--flow-actions <script-asset-id>` discovers a project callable entrypoint
and its declared script dependency closure through typed streaming. Imports use
canonical catalog asset IDs. Discovery registers bounded sources in an isolated
`project` registry and does not invoke registered actions or predicates. Native
contributors use the same [descriptor contract](scripting_callables.md).

The graph tool authors [documents and bindings](editor_flow.md). **Apply preview**
validates an independent graph snapshot and its streamed screen compositions before
installing the shared Flow runtime. The isolated Flow view supports nested Submenu
Areas, Tab Group routes, popup policies and real Atlas input. It is separate from
the ordinary single-document authoring preview. Save UI content and refresh changed
catalog assets before applying; unsaved UI buffers are not overlaid onto the catalog.

**Apply** uses the current graph draft and latest explicitly discovered callable
sources. **Restart** reconstructs the last applied snapshot. **Cancel load** drops
pending application/navigation; **Stop** retires the preview without changing
source or recovery files. **Graph** returns to editing. Changes never apply
implicitly. Invalid bindings, condition schemas or streamed sources preserve the
last valid preview and report a diagnostic.
**Resume preview** returns from the graph to that running snapshot without applying
the current draft. Each explicit apply acquires a fresh catalog mount, while the
running preview retains its prior mount independently.

The Flow sidebar shows presented screen/region identities, focus ownership,
history, the requested route and bounded condition results. **Next route** and
**Traverse route** send diagnostic requests through actual runtime guards; they
are not input replay and cannot prove arbitrary Lua reachability. Popup Back uses
Escape; portable hosts can supply their own performed semantic Back action.

The hierarchy lists authored element IDs and highlights the selection. The
inspector changes width and visibility, adds a generic painted leaf to a panel,
and removes nonroot subtrees. Undo and Redo restore up to 32 retained revisions.
New creates a generic document in another session. Reopen captures unsaved
edits in recovery and loads the saved file after successful validation.
Save writes canonical [UI XML](ui_authoring.md)
through a flushed sibling temporary file and replaces the destination.

The editor owns the editable document and its history. Each successful edit
constructs a separate runtime instance before publishing the new preview. The
main owner builds and routes the Atlas UI panels and submits immutable paint
packets. The render coordinator uploads [glyph pages](ui_text.md), presents
the panels and preview together, and retires graphics through GPU fences.

The bootstrap supports viewport documents of up to 256 elements. Preview
construction retains immutable typed catalog mounts from the asset workspace.
Unavailable asset references retain their authored values and show a failure
placeholder until a valid source mount can be prepared.
File bytes are read through the streamer. The Windows editor persistence
boundary handles authoring writes. The game executable remains a separate
consumer of the runtime libraries.

## Documents and recovery

Repeat `--document` to open several files in one editor window. Each of up
to 32 documents owns its selection, dirty state, undo/redo history and runtime
preview. The bottom bar shows the current filename. **Next document** cycles
through open sessions. **New** creates an `untitled-N.xml` target beside
the active document and rejects occupied source or recovery filenames.
**Close** checkpoints dirty edits before retiring the current session.
The window retains one document until application shutdown.

Autosave checkpoints changed dirty revisions every 30 seconds and before
normal window close. A document `view.xml` has an independent checkpoint
`view.xml.atlas-recovery.xml`. Source and checkpoint writes use the same
flushed sibling-temp replacement. Autosave preserves the document's dirty
state. Repeated intervals skip unchanged revisions. Undoing to the saved
revision retires a checkpoint created by the current session.

**Recover** restores a checkpoint as an undoable dirty revision. Use
`--recover --document view.xml` to start from recovery even when the source
is malformed. With repeated document arguments, this option restores each
selected file from its checkpoint. Save accepts the active content into
the source file and removes its checkpoint. A checkpoint discovered on
ordinary open is protected until Recover or Save resolves it. Closing an
untouched session preserves that existing checkpoint.

A checkpoint error keeps live sessions available and displays a message.
The window retries pending close once per second, allowing storage to be
fixed or a successful Save before shutdown. The workspace's explicit
discard policy removes a checkpoint before retiring an owner. All these
operations run on the editor owner after event callbacks return. Source
reads use the streamer and join their delivery before returning. Persistence
is synchronous in this implementation, so slow storage can pause the editor
during autosave. Immutable render packets retain their own values through
document switches and shutdown.

## Authored editor tools

The editor loads its shared shell and tools from `content/editor_tools` beside
the executable. The repository sources are in `assets/editor_tools`. They are
staged with the editor, so changes in a fork are available by default to every
project using that build. The shell declares the panels and controls in
[UI XML](ui_authoring.md). Its Lua behavior forwards authored action bindings
to native editor commands. Hierarchy rows and selection details come from the
editor's live authoring state.

Project tools go in `editor_tools` next to the document selected by
`--document`. With several documents, the last argument selects the tool
origin for the window's lifetime. Give that directory a [typed catalog](assets.md#xml-catalog-v1)
named `catalog.xml`, with logical root `tools` and exact byte lengths. Every
UI entry is a selectable tool. Register referenced ViewModel XML and Lua files
in the same catalog. The editor mounts the two origins independently and
labels the entries **Editor** and **Project**. The **Tools** button cycles
through their panels and the document preview. Restart the editor after
changing catalog entries or source files.

Start a panel by authoring and saving its geometry as UI XML. Add optional
`text` captions, bindings and behaviors in the XML, then register that file.
The shipped `width.xml`, `commands.xml` and `commands.lua` show a complete
tool. Each opened panel gets its own UI and Lua state. Lua can queue
`add_child`, `remove`, `narrower`, `wider` and `toggle_visible` through
`native.call("editor.command", command)` during input dispatch. The installed
shell also has the document lifecycle commands New, Save, Reopen, Undo and
Redo, and can queue `next_document`, `close_document`, and `recover`.
These commands run on the editor owner after event routing finishes.

Shipped controls bind both pointer release and semantic `activate`, so keyboard and assistive
activation enter the same ViewModel/Lua command path. Tool panels keep explicit authored
navigation mappings; panels without one use the standard keyboard navigation defaults.
The shared shell exposes an `inspector_controls` region for its ordinary inspector controls.
The host hides that region while the dedicated accessibility inspector is open, suppressing
both pixels and semantic/input exposure. A custom shell without that region remains usable
normally; opening the dedicated inspector falls back to the recovery chrome rather than
exposing overlapping controls. See [accessibility](accessibility.md) for staged semantic edits.

Tool panels use solid geometry and authored captions. Each origin can
register up to 16 contributed panels, and each panel can contain up to 256
elements. An invalid catalog is logged independently of the other origin.
An invalid shell leaves the native recovery interface available, and an
invalid contribution is skipped when selected.

## Asset workspace

The **Assets** button opens the type-oriented Atlas UI browser. Set
`--asset-root path/to/content` to select a writable editor workspace; the
default is `editor_assets` beside the last `--document` path. A fresh workspace
gets its own persistent namespace and `catalog.xml`, with directories named
`ui`, `view_model`, `flow`, `script`, `texture`, `shader`, `material`, `video`,
`audio`, `localization`, `menu`, `submenu`, `popup` and `widget`. Logical
subdirectories refine each type. Installed
editor tools keep their separate namespace and mount; this browser writes only
to the selected content workspace. External project descriptors and add-on
registration remain separate integration work.

Click the type button to cycle categories. Search matches case-insensitive
relative paths, including folders. While the browser is active, letters and
digits extend the search, Left removes a character, and Escape clears it.
**Next** pages through seven rows. Selecting a row displays its persistent ID,
revision, importer settings and dependency/reference counts. Missing files are
marked in the list.

**Import** opens a native file picker. A file already inside the selected type
root is registered in place; an external file is copied into that root using
its filename. Import dependencies first: UI, material and flow XML are parsed
on a ready job to extract typed references, and ViewModel XML is validated.
The full catalog must validate before publication. Explicit menu/submenu/popup/widget
sources also validate their XML presentation kind against the selected category. Other types are registered
as raw sources; registration does not decode images, compile shaders or execute
scripts. Source acquisition and hashing use the streamer and the application's
shared job pool, with one accepted import at a time. **Cancel** invalidates its
publication generation before requesting I/O cancellation.

**Move** selects a new filename inside the same type root, preserving the asset
ID. It also performs renames. **Relink** selects replacement bytes for that ID.
**Refresh** re-reads an existing source and compares its SHA-256 fingerprint,
byte length and dependencies; an unchanged source does not add a revision.
Changed sources and **Settings +** advance revisions and invalidate transitive
dependents and derived-product links. Settings + advances the raw importer
settings version; this browser does not yet expose type-specific importer forms
or generate derived previews. Source changes are checked explicitly by Refresh,
not by a background filesystem watcher.

**Assign** records a selected texture or material reference on the selected UI
element and participates in document undo. **Open UI** opens a registered UI
source against the current catalog snapshot. Referenced textures remain typed
authoring references; the bootstrap preview does not upload those textures into
GPU residency. **Remove** unregisters an asset only when catalog dependencies
and open document histories do not reference it. **Undo** and **Redo** retain
16 catalog snapshots each, separately from document history. History restoration
also rejects removal of identities referenced by open documents or their undo
and redo stacks.

Source writes are copy-on-write and never overwrite an existing destination.
Move, relink, unregister and undo preserve old source files so catalog recovery
cannot destroy their bytes. The catalog is atomically replaced only after a
complete source write. A failed catalog replacement rolls back the newly owned
source. A process interruption can leave an unregistered source or temporary
file; reopening uses only the committed catalog. No automatic disk garbage
collection runs. Undo history is session-local; committed metadata survives
restart. Do not edit sources concurrently with import or undo operations.

The workspace accepts at most 1,024 records, 4 MiB of catalog XML, and nonempty
source reads up to 16 MiB. These limits bound import buffers and retained
metadata. Large media acquisition and thumbnail caches require their own
incremental import implementation. Catalog writes are synchronous on the
owner, outside event callbacks; slow storage can pause publication. Loading
and acquisition errors appear in the panel, with diagnostics for failures and
profile scopes for opening, preparation, inspection and publication.

See [Flow graph documents](editor_flow.md) for the independent graph-document,
history, runtime-candidate and recovery contracts.

[Semantic palettes and editor themes](ui_palettes.md) describe independent
chrome/preview contexts, user YAML overrides, preset/reset controls and
literal versus semantic tint authoring.
