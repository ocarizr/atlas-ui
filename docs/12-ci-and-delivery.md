# Continuous integration and delivery

## Workflow structure

| File | Responsibility |
| --- | --- |
| `.github/workflows/pr.yml` | PR orchestration and stable final status |
| `.github/workflows/delivery.yml` | Main-branch packaging and version-tag publishing |
| `reusable-plan.yml` | Change impact and dependency-aware build matrix |
| `reusable-quality.yml` | Repository checks, assets, tooling tests, workflow lint, style/configuration |
| `reusable-build.yml` | Named component targets and labeled tests across Windows profiles |
| `reusable-package.yml` | Release build, CPack ZIP and SHA256 checksums |
| `.github/actions/setup-ci/action.yml` | Shared Python/toolchain initialization |

All jobs use Windows runners. Workflows remain small; scripts own testable policies. External actions are pinned to verified commit SHAs. Validation uses a read-only token; only publishing receives `contents: write`. PRs run with `pull_request`, not a privileged PR trigger.

## Pull requests

Every PR targeting `main` gets planning and a final `PR validation` status. Documentation-only changes validate links/configuration/assets without starting C++ builds. Infrastructure changes additionally test the CI tools, lint workflows with pinned actionlint, check style/analyzer configuration, and configure CMake.

`.ci/components.yaml` maps paths to components, build targets, CTest labels and dependencies. The planner uses a merge-base diff and disables rename detection so both old and new locations participate. It selects changed components plus transitive consumers.

- Editor application changes select editor validation; game application changes select game validation.
- Shared UI, jobs, content or protocol changes also select their consuming applications.
- Shared build/tooling changes are conservatively global.
- Assets run content validation, not unrelated application compilation.
- New unmapped C++/Protobuf source fails instead of disappearing from checks.

Selected builds compile explicit targets rather than the default all target. Their dependencies may still compile when necessary. Building an editor's required runtime libraries is different from unnecessarily building the AtlasGame executable.

Affected components share one build job per compiler/configuration profile. Targets and labels are deduplicated, so shared libraries are not rebuilt in separate jobs for each consumer. Component jobs start after repository quality/configuration, allowing the initial dependency build to populate the shared binary cache first.

## Registered targets and dependency setup

Jobs, streaming, protocol, dependency integration, application-host contracts and all four executable entry points have actual targets and CTest labels. Streaming registers `atlas_streaming`, `atlas_streaming_tests` and the `streaming` label; changes to its scheduler or tested media dependencies select its validation. Planned contexts still have empty target lists; adding source without targets fails CI. Dependency setup bootstraps pinned vcpkg and caches compiled archives. Initial uncached builds may take substantially longer.

When implementing a component, add its actual library/application and test target names to `build_targets`, its CTest labels to `test_labels`, and any missing paths/dependencies. CTest labels scope test execution, and zero discovered tests is an error. Profiles cover Debug, Release, ASan, MSVC analysis, clang-tidy and LLVM coverage. Coverage jobs merge fresh profiles and retain a text report; no percentage threshold has been set.

Set the stable `PR validation` check as required in branch protection. CODEOWNERS assigns ownership but does not, by itself, enable required approvals or branch protection; these are repository settings.

## Asset inventory

Once `Assets/` contains files, it must include `Assets/manifest.xml`:

```xml
<Assets schemaVersion="1">
  <Asset id="menu" type="ui" path="UI/menu.xml">
    <DependsOn id="logo" />
  </Asset>
  <Asset id="logo" type="texture" path="Textures/logo.png" />
</Assets>
```

Inventory paths use forward slashes relative to `Assets/`. Validation checks XML well-formedness, required fields, unique IDs/paths, existing files, directory containment, mapped files, and declared dependencies. XML is UTF-8 and does not use DTD/entity declarations. An empty foundation needs no invented assets. Full UI/ViewModel semantic schemas will be added with their implementations.

## Main packaging

Pushes to `main` run full applicable validation and then packaging. A normal branch push never creates a GitHub Release. CPack uses explicit install rules; it does not archive the whole checkout or local tools. ZIP files and SHA256 checksums are retained as workflow artifacts.

The current **application bootstrap** package contains AtlasGame, AtlasEditor, AtlasCook and AtlasPackage, non-system runtime DLLs, design documents, an XML manifest and dependency notices. The applications perform startup smoke checks; content tools only expose help/reject unimplemented commands. No rendered UI, active preview service or cooked-content pipeline is claimed.

## Version-tag publishing

Only tags matching `vMAJOR.MINOR.PATCH` or `VMAJOR.MINOR.PATCH` qualify, for example `v1.2.3`. No leading zeros, suffixes or incomplete versions are accepted. Standard semantics are major for incompatible changes, minor for compatible features, and patch for compatible fixes.

The release gate peels annotated tags to their commit, verifies that the checkout is that commit, and checks that it belongs to `origin/main` history. Tags on unmerged side branches fail. The tag triggers full applicable validation, rebuilding and packaging at its exact commit, even if tagged after the main-branch run.

The package version is taken from the tag. Publishing downloads the package artifact from that run and invokes GitHub CLI with `--verify-tag`; it does not create tags or overwrite existing releases. Normal main builds use the CMake development version.

The workflows are ready for review and local validation; their first hosted run requires committing and pushing them. No workflow, release, tag, or branch-protection change is performed merely by editing these files.
