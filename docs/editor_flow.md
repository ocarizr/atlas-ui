# Flow graph documents

`atlas::editor_flow` is the portable editor-owned graph-document library. The
`editor-flow` CMake component exports `atlas::editor_flow`. It depends on shared
UI content contracts and has no dependency on editor application classes or native
SDK types.

A `document` contains a stable document ID, Flow regions/screens/routes/parameters,
editor layout records and selection. Item references combine their kind and stable
ID. Labels and positions are independent metadata: changing a caption or moving a
node does not rename a screen, route, region or referenced asset.

## Graph workspace

`workspace` borrows one `session`. Selection, pan and zoom are transient view state;
they do not create authored revisions. Multi-selection retains stable item IDs and
prunes items removed by undo or another edit. Captions never rename identities.
The editor allocates IDs within a fresh 128-bit authoring scope on each tool open,
so recreating an item does not recycle an older deleted item's identity. Friendly
captions remain separate. Hosts can inject a unique scope for reproducible fixtures.

`begin_move` captures the revision and selected nodes. `move_by` updates one pending
displacement; `end_move` commits one complete revision. Cancellation writes nothing.
A changed source revision invalidates the gesture instead of overwriting newer
edits. Deleting screens or regions removes owned regions, incident routes, condition
decorators and layout records in one transaction. Removed entry references become
empty, saveable draft values.

`canvas` is a reusable Atlas UI surface with persistent input registrations,
pointer capture and ancestor clipping. It publishes ordinary paint snapshots and
clipped label rectangles. Callbacks produce value-only intents; mutations run after
dispatch returns. Nodes and elbow connections reuse tree storage. The host supplies
existing layout/paint services and may supply a shared PMR resource. Cheap graph
edits stay on the coordinator rather than creating jobs.
The editor supplies the application's shared PMR resource to its graph tree, input
state and isolated callable domain.

Typed catalog choices follow region ownership. Top-level nodes accept Menu, Popup
and legacy UI assets; submenu regions accept Submenu assets. Reusable Widgets and
media are not graph destinations. Missing assets remain explicit draft values.
Forward connections require existing endpoints in the same region; history-return
operations have no authored destination.

## Event and condition inspectors

Inspector metadata comes from the shared native/Lua callable registry. Discovery
does not inspect source or invoke behavior. Defaults, exact scalar types and
optional integer choices are checked before committing a binding.

`bind_event` returns an independent UI document with the binding on its selected
element. The graph stores route permissions, not UI event bindings. Semantic click,
focus-enter and focus-exit are distinct from hover; focus events require a focusable
control. Click requires matched in-bounds press/release or its configured performed
input action. Cancellation cannot become a click.

Built-ins are `atlas.navigate_to`, `atlas.exit_menu` and `atlas.close_popup`.
Navigation stores an exact outgoing route ID and source-screen context. A Tab Group
may declare its region; validation requires an owned Submenu Area, same-region
Submenu destination and replace operation. Registry actions retain descriptor ID,
named model context and scalar arguments. Hosts receive semantic bindings through
`construction::dispatch_event` on the UI owner. The isolated `preview` installs this
bridge per loaded graph instance. Standalone single-document hosts may leave it
unset for a static authoring preview.

`transition_conditions` stores persistent predicate/context IDs and arguments in
ordered AND groups. A leaf or group may be negated. Missing registrations remain
saveable drafts; `resolve_conditions` explicitly validates schemas and resolves
current domain generations. Unknown, retired or failed predicates cannot become
allowed through negation. Each expression allows 256 nodes, 64 children per group
and depth 8, with one decorator root per route.

Runtime candidates retain immutable condition declarations separately from strict
Flow XML. Hosts must resolve these declarations and compose the Flow guard evaluator
before running a candidate. Later draft edits cannot change a retained candidate.
Graph editing does not execute predicates or mutate a live Flow Manager.

## Isolated runtime preview

`preview` retains an immutable `runtime_candidate`, a streamer-backed screen
loader and a frozen native/Lua registration callback. `apply` validates every
screen privately, one asynchronous load at a time, including typed presentations,
submenu hosts, action arguments/model contexts and outgoing condition bindings.
The executable condition budget is 64 nodes at depth 8; incomplete AND groups
remain saveable authoring data but cannot run. Preflight resolves schemas without
executing predicates. Runtime evaluation uses each actual screen's model/domain
and exact callable generations, including revalidation while a transition loads.

A complete initial presentation replaces the old preview only after preflight.
Failure or cancellation preserves its active views. `restart` uses the last
applied graph/registration snapshot, independent of dirty drafts; `stop` cancels
loads and delivers owner lifecycle cleanup. No editor graph edits occur during
runtime navigation, and no source/recovery file is overwritten by preview.
Preflight adds bounded startup reads; it is explicit, not a per-frame discovery
pass. Ordinary transition loads continue through the same streamer and shared
job pool. The host retains mounts and any borrowed construction tables until
accepted loads have retired.

`ui_content::flow_viewport` is the reusable presentation/input adapter. It maps
Submenu Areas to their parent host geometry, clips their paint and hit testing,
and concatenates immutable packets in back-to-front presentation order. Submenu
hosts preserve their full layout size when partially clipped and inherit ancestor
translations and opacity. Scaled or rotated submenu hosts require a transformed
surface adapter and are rejected explicitly; transforms inside each composed UI
retain their regular support.

Modal popups suspend lower input. Modeless layers remain pointer-eligible, with one
keyboard/focus owner. Capture wins over geometric routing. Outside-dismiss
consumes both press and release, including across publication frames. Escape or
the configured performed Back action closes an allowed popup or exits the owning
menu; a popup that forbids Back consumes it. UI frames close before navigation
publication. Lua/input dispatch stays on its owner; shared layout/paint engines
retain their existing threshold-based job policies.

`presentation`, `history`, `conditions`, `status`, `diagnostic` and `active_route`
return bounded observations for inspection. `plan` returns a forward structural
path under the graph's declared scalar guards and identifies routes with custom
conditions. It does not execute Lua or prove those conditions reachable. A test
supplies real model fixtures/registrations and replays logical input packets via
`frame`; direct `request` traversal is deliberately a separate operation. Prepared
UI v4 uses the same event bridge and retains existing v2/v3 decoding compatibility.

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
`<flow-authoring version="1" id="stable_document_id">`. Version 2 adds outgoing
`conditions` records with recursive `predicate` and scalar `arg` records. Readers
accept versions 1 and 2; writers retain version 1 when decorators are absent.
Version 1 rejects version 2-only records. Runtime XML is a separate
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

Creation is exclusive by default. The editor's explicit `preserve_recovery` mode
can open a missing source with a protected orphan checkpoint. It adopts only a
valid checkpoint's document identity into the empty fallback, not its graph. A
malformed checkpoint remains pending for explicit discard. Checkpointing the
unchanged fallback preserves the old file; new edits require a restore/discard
decision first. `pristine_creation()` distinguishes an unchanged new revision
from an opened or explicitly restored document. The editor skips recovery for an
untouched empty bootstrap, while an explicitly restored empty graph remains
protected until Save or Discard.

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
