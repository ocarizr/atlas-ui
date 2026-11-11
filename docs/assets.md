# Assets and storage

## Current content

The repository maps loose assets through `assets/manifest.xml`. The CI validator
parses XML, validates asset identities/paths and resolves declared references.
Shader and image inputs are staged for the native application demonstration.
See [asset validation](ci_and_delivery.md#assets-and-delivery) for the inventory
contract. XML is serialized application content; YAML is configuration. There is
no asset cooker or bundle format implementation. [UI authoring](ui_authoring.md)
uses typed catalog references for model definitions, Lua and textures.
[ViewModel XML and native factories](mvvm.md) are implemented independently of
asset acquisition and editor persistence.

## Asset-management boundary

`atlas::asset_catalog` provides a shared persistent identity and a validated
read-only snapshot for project authoring, cooking and runtime mounting. The
editor asset browser and persistence commands have their own ownership. The CI
inventory, low-level streamer keys, temporary image residency keys and GPU
handles remain separate contracts.

| Context | Reference and resolution |
| --- | --- |
| UI documents and other content | Typed persistent asset ID, without physical paths |
| Development/editor | Shared catalog maps IDs to authorized loose sources and derived products |
| Packaged application | Prepared mount routes map the same IDs to logical ranges in registered backing sources |
| Consumer resource ownership | Temporary CPU/GPU handles with their own generations and lifetimes |

### Persistent identity

An ID is 32 lowercase hexadecimal digits for a nonzero 128-bit project
namespace, a colon, and 16 lowercase hexadecimal digits for a positive 64-bit
ordinal. Type is a separate field in every serialized reference. A composition
root provisions the namespace once and commits it with the central catalog. Use
a fresh namespace for an independent project. A checkout or relocation of the
same project preserves it. Ordinal zero is reserved, and the maximum ordinal is
the exhausted allocation watermark.

The project catalog owns one monotonic allocation watermark, `next-local`.
Allocation reserves the next ordinal and advances the watermark. Persist the
watermark even after deletion or abandoned allocations. A rename, move or
relink updates only the locator when the bytes and importer settings are
unchanged. Changes to bytes or import settings advance `revision`. A copy or an
import from another project receives a fresh local ID. Import tools must remap
references explicitly into the local namespace. V1 rejects cross-namespace
edges, duplicated IDs and case-insensitive duplicate source locators. Concurrent
authoring branches that allocate the same ordinal need an explicit ID and
reference remap before their catalog changes can be combined.

### XML catalog v1

The central document is the authoritative source metadata. It owns type,
locator, logical byte length, revision, importer and importer/settings versions,
typed dependencies and named derived links. The catalog is independent of the
editor and accepts bytes acquired by the composition root. The editor can edit
a draft and compile a complete replacement before saving or publishing it.

```xml
<asset-catalog version="1" namespace="018d0b95cf2673c6a0d7dd8ce5d29b23" next-local="3">
  <asset id="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"
         type="ui" root="ui" path="menus/hud.xml" revision="1" byte-size="128"
         importer="raw" importer-version="1" settings-version="1">
    <dependency id="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002" type="view_model"/>
  </asset>
  <asset id="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002"
         type="view_model" root="view_models" path="hud.xml" revision="1" byte-size="96"
         importer="raw" importer-version="1" settings-version="1"/>
</asset-catalog>
```

A derived link uses `<derived role="preview" id="…" type="texture"/>`.
Required dependency edges must form a directed acyclic graph. Derived links
identify products and may point to a product that depends on its source.
Missing or type-invalid references invalidate the candidate, as do repeated
dependency edges or product roles. Serialization orders assets by ID,
dependencies by ID, and products by role. V1 accepts up to 4,096 records, 64
links per record, 16,384 links per catalog, 512 bytes per source path and 8 MiB
of XML. Metadata fields use literal values without XML entity or character
references. Names and path components use ASCII letters, digits, underscores,
periods and hyphens. Root and role keys are case-sensitive. Paths use forward
slashes, reject dot segments, reserved Windows device names and trailing dots,
and are checked for case-insensitive collisions within a root.

### Mount and migration

Project YAML configuration supplies authorized root keys and directories to the
composition root. The resolver consumes that typed root mapping. Loose mounting
checks canonical containment, regular-file status and the declared length
before starting the streamer. The roots are controlled by the application. Live
file replacement and protection against hostile concurrent directory changes
require the source generation work in #59. Runtime asset references contain only
ID and type, and asset consumers receive metadata and bytes through typed reads.

A prepared mount can route an ID and revision to a raw range of a registered
memory or file source. Cooking and bundle readers can build this table without
changing serialized references. The mount validates all routes before creating
its storage worker. Each completion carries the mount generation and asset
revision. At the publication boundary the owner checks `accepts(stamp)` on the
current mount, then applies its frame/activation rules. Replacing a mount
invalidates completions even when ID and content revision are unchanged.

To migrate a legacy inventory, provision a project namespace, allocate one ID
per retained entry, fill typed metadata and rewrite references using an explicit
old-key-to-ID mapping. Prepared routes can bind those IDs to existing streamer
keys during migration. The `assets/manifest.xml` CI inventory and existing
string-keyed reads retain their current format and behavior.

The editor owns asset-management commands such as import, move, rename, relink,
reference inspection and deletion. The reusable catalog/resolver owns identity,
type/reference validation and consistent lookup. Catalog/bootstrap reads retain
the streamer boundary. Source/catalog editing requires an explicit persistence
service; the current streamer exposes reads only.

Shared identity is tracked in [#127](https://github.com/ocarizr/atlas-ui/issues/127),
editor asset management in [#128](https://github.com/ocarizr/atlas-ui/issues/128),
and external project setup in [#105](https://github.com/ocarizr/atlas-ui/issues/105).

## Runtime boundary

The streamer is the sole application-managed storage read boundary. Composition
roots register memory bytes or authorized loose paths in a catalog. Consumers
request logical asset IDs and exact ranges, then receive immutable owned bytes or
errors. Codecs and graphics consumers do not open files independently. Build tools
and isolated fixture setup are outside the running-application boundary.

The acquisition path is:

```text
typed catalog mount -> registered memory / authorized loose files
    -> bounded streamer request
    -> ready CPU completion/preparation job
    -> consumer resource ownership
    -> fenced GPU upload where required
```

Disk reads use bounded Windows IOCP, with operation-specific cancellation and
completion acknowledgment before reclamation. Streaming may span frames. Consumers
poll readiness or schedule ready continuations, keep placeholder presentation, and
publish usable results at controlled boundaries. CPU workers do not wait for I/O.
See [streaming](streaming.md) and
[renderer content](renderer_content.md).

## Packaging boundary

The main workflow uses CPack to assemble executable deliverables, runtime dependencies,
documentation and notices. PR workflows validate without packaging/publishing.
This is executable distribution, not content cooking. `atlas_cook` and
`atlas_package` are placeholder commands. Bundle layout, reader and writer are
tracked in [Content](https://github.com/ocarizr/atlas-ui/issues/29), with format
design required before reader/writer implementation. Loose-asset loading does not
depend on that format.

The planned bundle manifest is the packaged representation of the shared asset
catalog. Mounting it builds a validated runtime mapping from persistent IDs to
entries and logical byte ranges. Changing between loose and bundled storage must
preserve document references. Duplicate/missing IDs, stale revisions, invalid
dependencies and corrupt metadata require explicit diagnostics and safe failure.

Optional chunk compression and opt-in authenticated encryption are planned content
capabilities, tracked in [#129](https://github.com/ocarizr/atlas-ui/issues/129) and
[#130](https://github.com/ocarizr/atlas-ui/issues/130). The binary layout, codecs,
cryptographic suite and key provider remain undecided. Transforms preserve asset
IDs and logical ranges, use bounded ready jobs and retain cancellation/resource
ownership. Encryption includes an explicit threat model and external key
provisioning; production secrets do not belong in source, public metadata or logs.
Plain bundle determinism and encrypted-output reproducibility have separate
contracts, including nonce uniqueness. No bundle transform is currently shipped.
