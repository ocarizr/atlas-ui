# Internal drag-and-drop

`atlas::ui::drag_drop_session` coordinates widgets and independently owned panels
in one native window. The application owns one domain; it does not own another
input loop, scheduler, OS drag object or filesystem adapter. Pointer capture,
presentation transforms and inherited rectangular clipping come from each
participating `input_session`.

## Sources, targets and payloads

Configure a live bound source with `configure_drag_source` and a destination with
`configure_drop_target` between frames. Unconfigured elements never accept data.
Types are exact ASCII keys of 1–64 characters (`A-Z`, `a-z`, `0-9`, `.`, `_`, `-`,
`:`). A destination accepts at most 32 distinct keys.

`drag_payload::binary` and `drag_payload::text` copy at most 65,536 bytes once.
The resulting immutable handle shares ownership across notifications and transfer
requests. Text is non-null UTF-8; empty text remains distinct from binary bytes.
Retained payloads keep their injected PMR resource alive. The shared control block
uses the standard allocator; payload data and type storage use the supplied PMR
resource. Do not encode registry pointers, Lua references or mutable buffers.
Asset transfers use stable asset IDs, not storage paths.

An absent static offer allows `drag_prepare` to return an owned payload on lift.
The default pointer threshold is four logical units, independent of DPI. Before
crossing it, the press/movement/release retain ordinary click behavior. After
crossing it, the domain cancels the ordinary pointer interaction, acquires the
source's existing capture and consumes the drag release so it cannot also click.

## Window frame integration

Open every participating input frame and call `process({})` before leasing it.
Pass views in back-to-front order. All views and the domain have the same serial
coordinator owner. Route each logical packet through the domain once:

```cpp
std::array views{&background_panel.input(), &foreground_panel.input()};
drag.begin_frame(window_frame, views);
for (const auto& packet : packets)
{
    if (!drag.process(packet))
    {
        route_normal_input(packet);
    }
}
if (auto request = drag.pending_drop(); request && drag.can_commit(request->transfer))
{
    const bool committed = apply_atomic_transfer(*request);
    drag.resolve(request->transfer, committed);
}
drag.end_frame();
// Only now: input.tick(), late_tick(), end_frame(), seal and apply UI commands.
```

A domain frame must end before any participating input advances to tick. The
gesture may span frames. Weak lifetime leases prevent it from retaining a
destroyed panel. Removed/recycled generations and omitted owners invalidate the
gesture instead of reaching replacement widgets. Destructors release ownership
without implicitly invoking application callbacks; orderly shutdown should
cancel in an open frame first.

The frontmost eligible hit determines the destination ancestry. The deepest
accepting target wins. A rejected nested target may fall back to its ancestor;
it does not expose a widget or panel occluded behind that hit. Type/capability
intersection precedes `drag_query`. Queries must be lightweight and free of
application mutations. No worker enters a handler or Lua VM. Candidate visits,
input packets and callbacks have explicit independent frame budgets.

## Copy, move and confirmation

Both endpoints declare copy/move capabilities. Without an explicit preference,
copy wins when supported. An explicit move never silently becomes copy. A query
can reject a target or choose a supported operation.

Release/confirm emits `drag_drop` and exposes an owned `drop_request` with a
nonreused ticket. This notification is not a completed transfer. The application
checks `can_commit` immediately before its atomic transaction, then calls
`resolve(ticket, committed)` after it finishes. A move removes source data only
inside that transaction. The runtime never deletes, copies or moves application
data itself. Async preparation may retain the request and return in a later
frame; it must not block the serial owner or CPU workers on I/O.

`resolve(false)`, rejection, Escape, pointer cancellation or window focus loss
preserve application data. Source/target disappearance or policy changes make a
pending preflight fail. Tickets cannot be reused. Once an application has
committed, completion callback failure does not reclassify the transfer as
cancelled or undo it. Retained handlers receive terminal identities even if an
already committed move removed the source element. Callback exceptions release
capture and uncommitted gesture state, preserve the first error and discard the
failing input owner's unsealed commands. Arbitrary external side effects inside
user callbacks are not rollbackable.

## Keyboard and semantic input

Space lifts the focused source, Tab/Shift+Tab selects eligible destinations,
Enter requests a transfer and Escape cancels. `drag_actions` provides equivalent
lift/confirm/cancel/next/previous/copy/move bindings for the input library's
semantic actions, including future controller mappings. Navigation uses stable
presented order and wraps. These alternatives use the same payload, acceptance
and confirmation contract as the pointer path. A host can disable portable keys
while retaining semantic actions.

`feedback()` contains phase, source, destination, logical position and operation.
It is rendering data, not a UI-tree mutation. A ghost must not participate in hit
testing. The editor uses a small accepted/rejected/pending marker and shares the
unchanged base packet; its render thread reuses scratch when appending feedback.

## XML and Lua

```xml
<element id="asset_row" parent="root">
  <drag-source type="atlas.asset" text="stable-asset-id"
               operations="copy" threshold="4"/>
</element>
<element id="reference_slot" parent="root">
  <drop-target types="atlas.asset;application.reference" operations="copy"/>
</element>
```

`operations` is `copy`, `move` or `copy-move`. Omit source `type`/`text` only when
a behavior supplies the offer from `drag_prepare`. Source/target policies survive
prepared UI version 10. The reader retains versions 2–9 and rejects drag event
records disguised as an earlier version. Prepared files never contain a live
gesture, callback pointer or pending ticket.

Lua handlers are `drag_prepare`, `drag_started`, `drag_query`, `drag_enter`,
`drag_over`, `drag_leave`, `drag_drop`, `drag_completed` and `drag_cancelled`.
The original ten event arguments are preserved. The additional arguments are:
payload type, text, `is_text`, operation (0 none / 1 copy / 2 move), full source
identity, full target identity, decimal transfer ticket and cancellation reason.
Binary payloads are not implicitly converted to text. Full identities are opaque
diagnostic strings, not durable authoring IDs.

The original five response booleans are followed by optional offer type/text,
acceptance (0 defer / 1 accept / 2 reject) and operation (0 default / 1 copy /
2 move). Offers are valid only in prepare; acceptance/operation only in query.
Invalid result types, identifiers or values reject the script's demands before
model/native-work admission. Existing handlers can continue returning fewer
values. A Lua drop notification still requires its host's transaction adapter;
returning from a script is not proof of successful data transfer.

## Editor transactions and diagnostics

Hierarchy rows offer stable authored element IDs with move capability. Container
rows accept them after root/leaf/cycle/no-op checks. The editor validates the
complete reordered candidate before replacing its preview and publishing one
undo revision. Parent-specific slot metadata is reset; incompatible retained
sibling or scrollbar relationships reject the candidate without partial edits.

Texture/material browser rows offer `atlas.asset` by copy. Drop onto the selected
element's inspector reference row. The editor resolves the ID against its current
catalog, creates the preview privately and assigns the reference in one undo
revision. This does not import, move or delete files. Authored panels participate
in the same window domain; native editor transfers are confirmed only for the
document operations described here. Other application-specific payloads require
an explicit host transfer adapter and are rejected without one.

Optional diagnostics scopes include `ui.drag.dispatch`, `ui.drag.callback`,
`editor.input`, `editor.drag.commit` and `editor.drag.reparent`. Invalid editor
transactions log warnings through diagnostics. Pointer motion is not logged.
These scopes obey the shared AUTO/ON/OFF profiling build policy. Work is serial
and bounded; there is no advantage in dispatching tiny policy checks to jobs.

System file drops, cross-window/process transfer, auto-scrolling during a drag,
and visual redesign of the editor are outside this contract.
