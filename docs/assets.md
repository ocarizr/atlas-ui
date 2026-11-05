# Assets and storage

## Current content

The repository maps loose assets through `assets/manifest.xml`. The CI validator
parses XML, validates asset identities/paths and resolves declared references.
Shader and image inputs are staged for the native application demonstration.
See [asset validation](ci_and_delivery.md#assets-and-delivery) for the inventory
contract. XML is serialized application content; YAML is configuration. There is
no UI/ViewModel XML schema, asset cooker or bundle format implementation.

## Asset-management boundary

The agreed product model uses one persistent asset identity across project
creation, editor authoring, cooking and standalone runtime loading. This shared
identity/catalog layer and the editor asset browser are not implemented. Current
logical streamer IDs and the CI inventory do not establish a persistent editor
database. Temporary image request/residency keys and GPU handles are separate
from serialized asset identity.

| Context | Reference and resolution |
| --- | --- |
| UI documents and other content | Typed persistent asset ID, without physical paths |
| Development/editor | Shared catalog maps IDs to authorized loose sources and derived products |
| Packaged application | Validated bundle manifest maps the same IDs to packaged entries/chunks |
| Consumer resource ownership | Temporary CPU/GPU handles with their own generations and lifetimes |

Renaming or moving a source preserves its identity. Content revisions, importer
settings and source generations describe changed content independently of that
identity. Source locators belong to infrastructure; codecs, UI and other asset
consumers receive metadata/buffers through shared acquisition contracts.

Project configuration uses YAML for source/cache/output roots. Serialized source
catalog metadata and UI asset references follow the XML content policy; the exact
ID representation and central-versus-per-asset metadata layout remain design
decisions. Packaged manifests are versioned binary data produced by the content
pipeline. A runtime read-only catalog must not depend on editor widgets or a
writable authoring database.

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

The implemented path is:

```text
memory / registered loose files
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
