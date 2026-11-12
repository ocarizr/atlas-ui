# Atlas Editor

Run `atlas_editor --document path/to/view.xml` to open an authored viewport UI
document. A missing file starts a generic document, and Save creates it at that
path. The default path is `atlas-ui.xml` in the launch directory. Use `--warp`
to select software graphics.

The hierarchy lists authored element IDs and highlights the selection. The
inspector changes width and visibility, adds a generic painted leaf to a panel,
and removes nonroot subtrees. Undo and Redo restore up to 32 retained revisions.
New creates a generic document. Reopen replaces current edits with the saved file
after successful validation. Save writes canonical [UI XML](ui_authoring.md)
through a flushed sibling temporary file and replaces the destination.

The editor owns the editable document and its history. Each successful edit
constructs a separate runtime instance before publishing the new preview. The
main owner builds and routes the Atlas UI panels and submits immutable paint
packets. The render coordinator uploads [glyph pages](ui_text.md), presents
the panels and preview together, and retires graphics through GPU fences.

The bootstrap is intended for asset-free viewport documents of up to 256
elements. Preview construction accepts a typed catalog mount, which is the
integration point for project assets. The launcher's initial mount is empty.
File bytes are read through the streamer. The Windows editor persistence
boundary handles authoring writes. The game executable remains a separate
consumer of the runtime libraries.

## Authored editor tools

The editor loads its shared shell and tools from `content/editor_tools` beside
the executable. The repository sources are in `assets/editor_tools`. They are
staged with the editor, so changes in a fork are available by default to every
project using that build. The shell declares the panels and controls in
[UI XML](ui_authoring.md). Its Lua behavior forwards authored action bindings
to native editor commands. Hierarchy rows and selection details come from the
editor's live authoring state.

Project tools go in `editor_tools` next to the document selected by
`--document`. Give that directory a [typed catalog](assets.md#xml-catalog-v1)
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
Redo. These commands run on the editor owner after event routing finishes.

Tool panels use solid geometry and authored captions. Each origin can
register up to 16 contributed panels, and each panel can contain up to 256
elements. An invalid catalog is logged independently of the other origin.
An invalid shell leaves the native recovery interface available, and an
invalid contribution is skipped when selected.
