# Assets and storage

## Current content

The repository maps loose assets through `assets/manifest.xml`. The CI validator
parses XML, validates asset identities/paths and resolves declared references.
Shader and image inputs are staged for the native application demonstration.
See [asset validation](ci_and_delivery.md#assets-and-delivery) for the inventory
contract. XML is serialized application content; YAML is configuration. There is
no UI/ViewModel XML schema, asset cooker or bundle format implementation.

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
