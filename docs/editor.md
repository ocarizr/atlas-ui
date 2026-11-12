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
