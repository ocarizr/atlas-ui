"""Generate public-header HTML/XML references and a complete GitHub Wiki snapshot."""

import argparse
import re
import subprocess
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.wiki_renderer import render_library
from scripts.ci.wiki_snapshot import write_snapshot


def public_libraries(root):
    """Discover library include roots, excluding vendors, apps, tests and implementation sources."""
    return {
        path.parent.relative_to(root / "libs").as_posix().replace("/", "-"): path
        for path in sorted((root / "libs").rglob("include"))
        if path.is_dir() and (any(path.rglob("*.hpp")) or any(path.rglob("*.h")))
    }


def generate(root, output, executable, repository, revision):
    """Run pinned Doxygen for every public library, publishing no partial snapshots."""
    root, output = Path(root).resolve(), Path(output).resolve()
    pin = read_yaml(root / ".ci/doxygen.yaml")
    if subprocess.check_output([executable, "--version"], text=True).split()[0] != pin["version"]:
        raise ValueError(f"Expected Doxygen {pin['version']}")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Reference provenance requires a full commit SHA")
    if output == root or not output.is_relative_to(root / "build"):
        raise ValueError("Reference output must be inside the ignored build directory")
    if output.exists() and any(output.iterdir()):
        raise ValueError("Reference output must be empty; use a fresh build directory")
    libraries = public_libraries(root)
    if not libraries:
        raise ValueError("No public libraries found")
    output.mkdir(parents=True, exist_ok=True)
    base = (root / ".ci/doxyfile").read_text(encoding="utf-8")
    pages = {}
    navigation = []
    for name, include in libraries.items():
        headers = sorted(include.rglob("*.hpp")) + sorted(include.rglob("*.h"))
        destination = output / name
        configuration = base + f'\nPROJECT_NUMBER = "{revision}"\n'
        configuration += f'OUTPUT_DIRECTORY = "{destination.as_posix()}"\n'
        configuration += f'STRIP_FROM_PATH = "{root.as_posix()}/"\n'
        configuration += (
            "INPUT = "
            + " ".join(f'"{path.relative_to(root).as_posix()}"' for path in headers)
            + "\n"
        )
        subprocess.run([executable, "-"], input=configuration, text=True, cwd=root, check=True)
        pages.update(render_library(name, destination / "xml", repository, revision))
        navigation.append(f"- [{name}](library-{name})")
    introduction = "# Atlas UI API reference\n\n"
    introduction += "Generated from public C++ headers by Doxygen. Edit source comments to change these pages.\n\n"
    introduction += f"[Source revision `{revision[:12]}`]({repository}/tree/{revision})\n\n"
    pages["Home.md"] = introduction + "## Libraries\n\n" + "\n".join(navigation) + "\n"
    pages["_Sidebar.md"] = "[API reference](Home)\n\n" + "\n".join(navigation) + "\n"
    write_snapshot(output / "wiki", pages, revision)
    print(f"Generated {len(libraries)} libraries and {len(pages)} wiki pages at {output}")


def main():
    """Generate references locally or in CI using an explicitly installed Doxygen binary."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--doxygen", required=True)
    parser.add_argument("--output", default="build/documentation")
    parser.add_argument("--repository", default="https://github.com/ocarizr/atlas-ui")
    parser.add_argument("--revision")
    args = parser.parse_args()
    revision = (
        args.revision or subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    )
    generate(Path.cwd(), args.output, args.doxygen, args.repository.rstrip("/"), revision)


if __name__ == "__main__":
    main()
