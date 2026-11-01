# API reference and repository wiki

The [repository wiki](https://github.com/ocarizr/atlas-ui/wiki) is a generated API
reference. Source comments are the source of truth. Do not maintain wiki pages by
hand: publication replaces all tracked wiki content, including obsolete pages and
attachments, while retaining the wiki Git history. Guides and architectural
decisions remain reviewed Markdown under `docs/` in the source repository.

## Generation

Doxygen 1.18.0 is pinned in `.ci/doxygen.yaml`. The installer downloads its official
Windows archive and verifies SHA-256 before extraction or execution. The shared
`.ci/doxyfile` enables HTML and XML, checks documentation warnings as errors and
avoids native compilation, vcpkg, Graphviz and external include discovery.

The generator discovers `libs/**/include/` roots containing C++ headers and runs
Doxygen independently for each library. The current references cover application,
diagnostics, IPC, jobs, jobs diagnostics and streaming. New public-header library
roots join automatically. Tests, application entry points, third-party sources,
private implementation directories and generated Protobuf bindings are not inputs.
The wiki renderer omits private members; public-header support/detail symbols can
still appear and do not become stable API merely because they are documented.

Doxygen XML is converted to flat GitHub Wiki Markdown pages with library indexes,
types, namespaces, files, signatures, template parameters, constraints, enumerators,
parameter contracts and description text. Symbol references and explicit member
anchors are linked, with source locations pinned to the source commit. Common
Doxygen description elements retain lists, code examples, warnings, returns and
links. Unrecognized description containers retain their text; diagrams and full
Doxygen HTML navigation remain available in the retained HTML output rather than
being embedded as HTML applications in the wiki.

Generation produces `build/documentation/<library>/{html,xml}` and
`build/documentation/wiki/`. A YAML inventory records the source revision and every
Markdown page's SHA-256. All local page and explicit-anchor links are validated.
Output must be a fresh directory inside `build/`; a failed generation cannot produce
a complete, publishable snapshot. Use another output directory for repeated local
runs, or remove only the previous generated directory before running again.

```powershell
python -m pip install -r scripts/ci/requirements.txt
$doxygen = ./scripts/install_doxygen.ps1
python -m scripts.ci.build_reference --doxygen $doxygen
```

Open `build/documentation/jobs/html/index.html` for the full local HTML reference.
Building references does not require the Windows SDK; the provided installer and
Actions runner currently target Windows, consistent with validated project support.

## Pull requests and publication

`reusable_reference.yml` builds and validates all public library references.
`reusable_validation.yml` starts this job independently on pull requests and the
required gate demands its success. PR generation uses read-only permissions, no
publication secret and no wiki writes.

PRs run the generator with `--check`: HTML generation is disabled, intermediate
Doxygen XML lives in a temporary directory and reference conversion/link validation
run in memory. Temporary XML is removed on success and failure; no Markdown snapshot,
HTML tree or reference artifact is retained or uploaded. The same check is available
locally with `python -m scripts.ci.build_reference --doxygen <executable> --check`.
The reusable workflow independently guards artifact upload with the `main` branch
condition, even if a caller incorrectly requests upload from another branch.

`documentation.yml` runs on every push to `main`, and can be manually dispatched
on `main` to retry publication. It does not wait for native builds or package
delivery. A generation job uploads the complete HTML/XML/wiki tree as an internal
14-day transfer artifact. A separate publication job downloads that same run's
artifact, validates the inventory again, clones the wiki and synchronizes its
tracked content. The public destination is the wiki; the artifact is supplementary
and no documentation package or release is published by this workflow. Future
product releases may include the retained reference tree.

Publication commits additions, updates and deletions, then pushes normally to the
wiki's existing default branch (which need not be named `main`). It never
force-pushes. Repeating an unchanged snapshot creates no commit. Concurrent wiki
runs are serialized; a run whose source revision no longer equals remote `main`
skips publication. Push conflicts fail visibly rather than overwriting history;
rerun the workflow on current `main` to retry. Generation, artifact transfer,
authentication and push failures fail their jobs instead of reporting publication
success. Wiki edits are recoverable through its Git history, but subsequent runs
replace them with generated content.

## One-time repository setup and local publication

1. Enable Wiki under repository Settings and create the initial `Home` page. GitHub
   initializes the separate `owner/repository.wiki.git` repository after this step.
2. Set the repository Actions secret `WIKI_TOKEN` to a credential authorized to
   read and write that wiki. A classic PAT uses `public_repo` for a public repository
   or `repo` when required for a private repository. Keep its lifetime and access
   appropriate to the account policy. The built-in workflow token is not used as
   a silent fallback. Rotate the secret when its credential expires.
3. Merge the workflow through a PR. The push to `main` publishes; use **Publish API
   wiki → Run workflow → main** for subsequent retries.

The token is passed through child-process Git HTTP configuration, never embedded
in a URL, command argument, tracked file or commit message. The wiki checkout is
temporary. A local maintainer with existing SSH write access can publish the same
validated snapshot explicitly:

```powershell
python -m scripts.ci.publish_wiki --repository owner/repository --ssh
```

Run this command only from a local `main` checkout. The publication entry point
rejects other local branches, detached checkouts and non-main Actions refs before
accessing the wiki. CI also passes `--current-main` to reject stale snapshots. The
source checkout itself must never be used as the wiki destination.

References: [Doxygen XML output](https://www.doxygen.nl/manual/customize.html),
[official downloads and checksums](https://www.doxygen.nl/download.html), and
[GitHub wiki Git workflow](https://docs.github.com/en/communities/documenting-your-project-with-wikis/adding-or-editing-wiki-pages).
