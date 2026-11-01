"""Verify full generated-wiki replacement, cleanup and failed-publication safety."""

import subprocess

import pytest

from scripts.ci.wiki_snapshot import synchronize, write_snapshot


@pytest.fixture
def wiki(tmp_path):
    """Create an isolated wiki checkout with tracked stale content."""
    directory = tmp_path / "wiki"
    directory.mkdir()
    subprocess.run(["git", "init", str(directory)], check=True, capture_output=True)
    for name in ["Home.md", "Removed-Class.md", "old-image.png"]:
        (directory / name).write_text("old", encoding="utf-8")
    subprocess.run(["git", "-C", str(directory), "add", "."], check=True)
    return directory


def test_sync_creates_updates_removes_and_is_idempotent(tmp_path, wiki):
    """A complete snapshot replaces every tracked page while preserving Git metadata."""
    source = tmp_path / "snapshot"
    pages = {"Home.md": "[Jobs](library-jobs)", "_Sidebar.md": "nav", "library-jobs.md": "Jobs"}
    write_snapshot(source, pages, "a" * 40)
    synchronize(source, wiki)
    assert (wiki / "Home.md").read_text(encoding="utf-8") == pages["Home.md"]
    assert (wiki / "library-jobs.md").is_file()
    assert not (wiki / "Removed-Class.md").exists()
    assert not (wiki / "old-image.png").exists()
    assert (wiki / ".git").is_dir()
    synchronize(source, wiki)
    assert sorted(path.name for path in wiki.glob("*.md")) == sorted(pages)


def test_corrupt_snapshot_does_not_touch_existing_wiki(tmp_path, wiki):
    """Validate page checksums before updating or deleting any published content."""
    source = tmp_path / "snapshot"
    write_snapshot(source, {"Home.md": "home", "_Sidebar.md": "nav"}, "a" * 40)
    (source / "Home.md").write_text("corrupted", encoding="utf-8")
    with pytest.raises(ValueError, match="checksum"):
        synchronize(source, wiki)
    assert (wiki / "Home.md").read_text(encoding="utf-8") == "old"
    assert (wiki / "Removed-Class.md").exists()


def test_sync_refuses_source_repository_as_wiki(tmp_path, wiki):
    """A mistakenly selected source checkout must not be erased."""
    source = tmp_path / "snapshot"
    write_snapshot(source, {"Home.md": "home", "_Sidebar.md": "nav"}, "a" * 40)
    (wiki / "CMakeLists.txt").write_text("project(example)", encoding="utf-8")
    with pytest.raises(ValueError, match="dedicated wiki"):
        synchronize(source, wiki)
    assert (wiki / "Removed-Class.md").exists()
