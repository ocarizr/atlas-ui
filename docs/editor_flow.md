# Flow graph documents

`atlas::editor_flow` is the portable editor-owned graph-document library. The
`editor-flow` CMake component exports `atlas::editor_flow`. It depends on shared
UI content contracts and has no dependency on editor application classes or native
SDK types.

A `document` contains a stable document ID, Flow regions/screens/routes/parameters,
editor layout records and selection. Item references combine their kind and stable
ID. Labels and positions are independent metadata: changing a caption or moving a
node does not rename a screen, route, region or referenced asset.

## Drafts and runtime candidates

Draft validation checks structure, unique declaration IDs, scalar values,
selection and layout references, and resource limits. It allows empty graphs,
unconnected screens, missing assets and unresolved runtime references. These are
saveable authoring states. Invalid XML syntax, duplicate IDs, unknown fields and
nonfinite values are rejected.

`session::apply` accepts an independently edited document as one undoable revision.
The document identity remains fixed for the session. Graph declarations, labels,
positions and selection belong to the same revision. Selection changes submitted
through `apply` are persisted edits and participate in undo/redo. The session keeps
immutable snapshots; transferring history does not copy entire graphs.

History defaults to 64 earlier revisions, accepts limits from zero (disabled) to
128, and discards redo after a divergent edit. Dirty state compares exact revision
identities, so undoing to the last saved revision becomes clean again. History is
session-local; saved XML contains the current complete document, not an edit log.

`session::compile` separately validates the graph using the existing strict Flow
contract, including its 512 KiB runtime XML limit. An optional `asset_check` validates screen references against a
host-provided catalog snapshot. Capture a retained immutable catalog/mount in that
callback when its lifetime belongs to the candidate. This library acquires no
asset bytes during compilation.

The resulting `runtime_candidate` owns only runtime graph data and its dependency
capability. Its XML uses the existing versioned runtime Flow format and excludes
editor labels, positions and selection. `import_runtime` explicitly converts
strict runtime XML into authoring values without changing its screen/route/region
or asset identities.

`publish` accepts only a candidate issued by the same session for its current
revision. It rechecks the candidate's asset capability before replacing the
published snapshot. Invalid drafts, stale/foreign tokens and dependency failures
preserve the previous snapshot. Retaining `published()` also retains its graph and
captured dependency snapshot. Callbacks run synchronously on the session owner;
reentrant mutation and worker access are rejected.

This publication boundary stores validated runtime data. A host composes and
loads actual screens separately with the existing Flow Manager; graph editing
does not mutate a running manager.

## XML contract

Authoring and recovery use the same complete UTF-8 XML representation:
`<flow-authoring version="1" id="stable_document_id">`. Runtime XML is a separate
contract with a `<flow>` root; imports are explicit.

| Record | Contents |
| --- | --- |
| `region` | Stable ID, initial screen, owner screen, host element, remember flag |
| `screen` | Stable ID, region, typed asset ID, focus and optional popup overrides |
| `transition` | Stable route ID, source/destination, operation, input binding and optional equality guard |
| `parameter` | Stable name, exact scalar type and value |
| `layout` | Item kind/ID, display label and graph coordinates |
| `selection` | Selected item kind/ID, or the explicit empty selection |

Required reference attributes may contain empty strings in incomplete drafts. An
empty screen `asset` is the missing-asset sentinel; valid references use the
catalog's canonical persistent ID. Optional popup Booleans distinguish absence
(inherit) from an explicit false override.

Version 1 item tags are `none=0`, `region=1`, `screen=2`, `transition=3`. Scalar tags
are `boolean=0`, `integer=1`, `number=2`, `text=3`. Operation tags preserve the
current Flow enum order from `push=0` through `close_popup=7`. These are versioned
wire values. DTDs, processing instructions, unexpected content, unknown attributes
and duplicate attributes are rejected; ordinary XML comments are permitted.

Drafts allow 64 regions, 256 screens, 1,024 transitions, 64 parameters and 1,344
layout records. IDs use the runtime's 64-character portable spelling. Labels allow
1,024 UTF-8 bytes; scalar text allows 4,096 bytes. Coordinates must be finite and
within +/-1,000,000. Serialized snapshots are limited to 2 MiB.

## Persistence and recovery

`persistence` owns one trusted host-selected path and one session. The editor can
retain independent owners for multiple graphs. The `storage` adapter provides
read, atomic complete-file replacement and remove operations. The library performs
no direct filesystem I/O. The editor application's `make_flow_storage` adapter
uses existing streamer acquisition and native sibling-temp/flush/replace writes.
Its SDK-free implementation reports unavailable native storage explicitly.

Opening a source never restores or overwrites an existing checkpoint implicitly.
`recovery_pending()` reports that an explicit choice is required. A malformed
checkpoint remains pending until restore or discard is requested. The recovery
path appends `.atlas-flow-recovery.xml`, separate from ordinary UI recovery files.

- `checkpoint()` writes a complete changed dirty revision atomically. An unchanged
  revision is skipped. Existing unresolved recovery is preserved, and conflicting
  newer edits require an explicit decision before another checkpoint.
- `recover()` restores checkpoint values as an undoable dirty edit without writing
  the source. A checkpoint with a different document identity is rejected.
- Opening with `restore=true` explicitly starts from recovery and can bypass a
  malformed or missing source.
- `save()` writes the current draft, then removes recovery, then marks the exact
  revision clean. Source replacement and checkpoint removal are separate atomic
  operations; failed cleanup leaves the session dirty for retry, even if source
  replacement already succeeded.
- `discard_recovery()` explicitly removes the checkpoint. `close(false)` preserves
  unsaved work first; `close(true)` explicitly discards recovery. A failed close
  leaves the session accessible. Destruction itself performs no fallible I/O.

The host owns prompts, autosave scheduling, trusted project-root authorization and
the lifetime of borrowed storage services. Native file access is selected by the
existing CMake platform integration flag. Reads may wait on the coordinator during
an explicit file operation; no CPU job worker waits for these reads.

Optional diagnostics profile editing, compilation, publication, open/save,
checkpoint and recovery. Compilation and dependency-check exceptions report warnings
while preserving the published snapshot. Cheap graph edits and Lua dispatch do not
create jobs merely for parallelism.
