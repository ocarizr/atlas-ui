"""Validate generated wiki snapshots before replacing a dedicated wiki checkout."""

import hashlib
import re
import subprocess
from pathlib import Path

import yaml


def page_name(name):
    """Accept only flat Markdown page names that cannot escape the wiki root."""
    if not re.fullmatch(r"[A-Za-z0-9_-]+\.md", name):
        raise ValueError(f"Unsafe wiki page name: {name}")
    return name


def validate_pages(pages):
    """Require navigation and resolve every generated local page/explicit-anchor link."""
    if not {"Home.md", "_Sidebar.md"} <= pages.keys():
        raise ValueError("A complete snapshot needs Home and _Sidebar pages")
    for name, content in pages.items():
        page_name(name)
        if not content.strip():
            raise ValueError(f"Empty wiki page: {name}")
        prose = re.sub(r"```.*?```", "", content, flags=re.DOTALL)
        for target in re.findall(r"\]\(([^)]+)\)", prose):
            if "://" in target or target.startswith("mailto:"):
                continue
            path, _, anchor = target.partition("#")
            destination = f"{path}.md" if path else name
            if destination not in pages:
                raise ValueError(f"Broken wiki link in {name}: {target}")
            if anchor and f'<a name="{anchor}"></a>' not in pages[destination]:
                raise ValueError(f"Broken wiki anchor in {name}: {target}")


def write_snapshot(directory, pages, revision):
    """Write validated Markdown plus a checksummed YAML inventory to a fresh directory."""
    validate_pages(pages)
    directory = Path(directory)
    if directory.exists() and any(directory.iterdir()):
        raise ValueError(f"Snapshot output must be empty: {directory}")
    directory.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name, content in sorted(pages.items()):
        data = content.encode("utf-8")
        (directory / name).write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    (directory / "manifest.yaml").write_text(
        yaml.safe_dump({"revision": revision, "pages": hashes}, sort_keys=True), encoding="utf-8"
    )


def read_snapshot(directory):
    """Load and verify the entire generated inventory before allowing publication."""
    directory = Path(directory)
    manifest = yaml.safe_load((directory / "manifest.yaml").read_text(encoding="utf-8"))
    pages = {}
    for name, checksum in manifest["pages"].items():
        path = directory / page_name(name)
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != checksum:
            raise ValueError(f"Wiki snapshot checksum mismatch: {name}")
        pages[name] = path.read_bytes().decode("utf-8")
    validate_pages(pages)
    return manifest, pages


def synchronize(source, wiki):
    """Mirror a validated snapshot; the dedicated wiki is entirely generated content."""
    _, pages = read_snapshot(source)
    wiki = Path(wiki).resolve()
    root = subprocess.check_output(
        ["git", "-C", str(wiki), "rev-parse", "--show-toplevel"], text=True
    ).strip()
    if Path(root).resolve() != wiki or (wiki / "CMakeLists.txt").exists():
        raise ValueError("Publication requires a dedicated wiki checkout")
    tracked = subprocess.check_output(["git", "-C", str(wiki), "ls-files", "-z"], text=True).split(
        "\0"
    )
    untracked = subprocess.check_output(
        ["git", "-C", str(wiki), "ls-files", "--others", "--exclude-standard", "-z"], text=True
    ).split("\0")
    if any(name and name not in pages for name in untracked):
        raise ValueError("Unexpected untracked files in dedicated wiki checkout")
    stale = []
    for name in filter(None, tracked):
        path = wiki / name
        if path.is_symlink() or not path.resolve().is_relative_to(wiki):
            raise ValueError(f"Unsafe tracked wiki path: {name}")
        if name not in pages:
            stale.append(path)
    for name in pages:
        if (wiki / name).is_symlink():
            raise ValueError(f"Unsafe destination wiki page: {name}")
    for path in stale:
        path.unlink(missing_ok=True)
    for name, content in pages.items():
        (wiki / name).write_bytes(content.encode("utf-8"))
