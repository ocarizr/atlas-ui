"""Exercise Doxygen XML conversion and deterministic reference navigation."""

from pathlib import Path

import pytest

from scripts.ci.wiki_renderer import render_library
from scripts.ci.wiki_snapshot import validate_pages

DATA = Path(__file__).parent / "data/doxygen"


def test_doxygen_reference_preserves_contracts_and_symbol_links():
    """Expose signatures, parameter contracts, examples and navigable references."""
    pages = render_library("jobs", DATA, "https://github.com/owner/repo", "a" * 40)
    page = pages["api-jobs-classworker.md"]
    assert "void atlas::worker::run(int count)" in page
    assert "template <typename T>" in page
    assert "secret" not in page
    assert "Number of items." in page
    assert "**Warning:**" in page and "Do not block." in page
    assert "```cpp\nint count = 3;\n```" in page
    assert "[worker](api-jobs-classworker)" in page
    assert "/blob/" + "a" * 40 + "/libs/jobs/include/worker.hpp#L10" in page
    assert "api-jobs-classworker" in pages["library-jobs.md"]
    assert "concept atlas::ready = sizeof(T) > 0" in pages["api-jobs-conceptready.md"]
    assert render_library("jobs", DATA, "https://github.com/owner/repo", "a" * 40) == pages


def test_missing_doxygen_compound_fails_instead_of_publishing_partial_api(tmp_path):
    """A missing XML file invalidates the entire reference snapshot."""
    (tmp_path / "index.xml").write_bytes((DATA / "index.xml").read_bytes())
    with pytest.raises(FileNotFoundError):
        render_library("jobs", tmp_path, "https://github.com/owner/repo", "a" * 40)


@pytest.mark.parametrize(
    "pages",
    [
        {},
        {"Home.md": "home"},
        {"Home.md": "[Missing](gone)", "_Sidebar.md": "sidebar"},
        {"Home.md": "[Missing](Home#absent)", "_Sidebar.md": "sidebar"},
        {"Home.md": "home", "_Sidebar.md": "sidebar", "../outside.md": "bad"},
    ],
    ids=["empty", "missing-sidebar", "broken-page", "broken-anchor", "unsafe-path"],
)
def test_invalid_snapshot_is_rejected(pages):
    """Reject incomplete generation and broken navigation before modifying the wiki."""
    with pytest.raises(ValueError):
        validate_pages(pages)
