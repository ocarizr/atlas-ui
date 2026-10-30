import subprocess
from dataclasses import dataclass
from pathlib import Path

import pytest

from scripts.ci.release_policy import evaluate_ref, version_from_tag


@pytest.mark.parametrize("tag", ["v0.1.0", "V1.20.300", "v10.0.1"])
def test_valid_versions(tag):
    assert version_from_tag(tag) == tag[1:]


@pytest.mark.parametrize(
    "tag",
    [pytest.param("v1", id="missing-fields"),
     pytest.param("1.2.3", id="missing-prefix"),
     pytest.param("v01.2.3", id="leading-zero"),
     pytest.param("v1.2", id="missing-patch"),
     pytest.param("v1.2.3-rc1", id="unsupported-suffix"),
     pytest.param("v1.2.3/evil", id="invalid-path")],
)
def test_invalid_versions(tag):
    with pytest.raises(ValueError, match="Expected vMAJOR"):
        version_from_tag(tag)


@dataclass
class GitRepository:
    root: Path

    def run(self, *arguments):
        return subprocess.run(
            ["git", *arguments], cwd=self.root, check=True, capture_output=True, text=True
        ).stdout.strip()


@pytest.fixture
def release_repository(tmp_path):
    repository = GitRepository(tmp_path)
    repository.run("init", "-b", "main")
    repository.run("config", "user.name", "Pipeline Tests")
    repository.run("config", "user.email", "tests@example.invalid")
    repository.run("commit", "--allow-empty", "-m", "Initial")
    repository.run("tag", "-a", "v1.2.3", "-m", "Version")
    return repository


def test_main_packages_without_publishing(release_repository):
    assert evaluate_ref("refs/heads/main", "main", release_repository.root) == {
        "publish": False, "version": "development", "tag": ""
    }


def test_annotated_tag_in_main_history_is_publishable(release_repository):
    assert evaluate_ref("refs/tags/v1.2.3", "main", release_repository.root) == {
        "publish": True, "version": "1.2.3", "tag": "v1.2.3"
    }


def test_side_branch_tag_is_rejected(release_repository):
    release_repository.run("checkout", "-b", "feature")
    release_repository.run("commit", "--allow-empty", "-m", "Unmerged")
    release_repository.run("tag", "v9.0.0")
    with pytest.raises(ValueError, match="main history"):
        evaluate_ref("refs/tags/v9.0.0", "main", release_repository.root)


def test_checkout_must_match_tag(release_repository):
    release_repository.run("commit", "--allow-empty", "-m", "Later")
    with pytest.raises(ValueError, match="checkout"):
        evaluate_ref("refs/tags/v1.2.3", "main", release_repository.root)
