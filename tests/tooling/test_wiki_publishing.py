"""Exercise authenticated publication policy and a real local wiki Git round trip."""

import subprocess
from pathlib import Path

import pytest

from scripts.ci import publish_wiki
from scripts.ci.config import read_yaml
from scripts.ci.wiki_snapshot import write_snapshot

ROOT = Path(__file__).resolve().parents[2]


def test_token_is_passed_outside_remote_urls_and_command_arguments(monkeypatch):
    """Keep the wiki token in child-process Git configuration, without changing parent state."""
    monkeypatch.delenv("GIT_CONFIG_COUNT", raising=False)
    environment = publish_wiki.authentication("test-only-token")
    assert environment["GIT_CONFIG_COUNT"] == "1"
    assert environment["GIT_CONFIG_KEY_0"] == "http.https://github.com/.extraheader"
    assert environment["GIT_CONFIG_VALUE_0"].startswith("AUTHORIZATION: basic ")
    assert environment["GIT_TERMINAL_PROMPT"] == "0"


def test_missing_secret_fails_before_remote_access(tmp_path, monkeypatch):
    """Do not silently skip publishing or fall back to an insufficient workflow token."""
    write_snapshot(tmp_path, {"Home.md": "home", "_Sidebar.md": "nav"}, "a" * 40)
    monkeypatch.delenv("WIKI_TOKEN", raising=False)
    with pytest.raises(ValueError, match="WIKI_TOKEN"):
        publish_wiki.publish(tmp_path, "owner/repository")


def test_superseded_revision_never_clones_or_pushes(tmp_path, monkeypatch, capsys):
    """A queued older main run cannot overwrite newer API documentation."""
    write_snapshot(tmp_path, {"Home.md": "home", "_Sidebar.md": "nav"}, "a" * 40)
    commands = []

    def latest(arguments, environment, directory=None):
        """Supply the current main revision and retain the attempted remote operations."""
        commands.append(arguments)
        return "b" * 40 + "\trefs/heads/main"

    monkeypatch.setattr(publish_wiki, "git", latest)
    publish_wiki.publish(tmp_path, "owner/repository", ssh=True, current_main=True)
    assert len(commands) == 1 and commands[0][0] == "ls-remote"
    assert "superseded" in capsys.readouterr().out


def test_publication_round_trip_and_unchanged_snapshot_do_not_add_commits(tmp_path, monkeypatch):
    """Publish through real Git, retaining history and avoiding duplicate publication commits."""
    environment = publish_wiki.authentication(None)
    remote, seed, snapshot = (tmp_path / name for name in ("remote.git", "seed", "snapshot"))
    subprocess.run(["git", "init", "--bare", str(remote)], check=True, capture_output=True)
    subprocess.run(["git", "clone", str(remote), str(seed)], check=True, capture_output=True)
    for name, value in [("user.name", "Test"), ("user.email", "test@example.com")]:
        publish_wiki.git(["config", name, value], environment, seed)
    (seed / "Home.md").write_text("Initial wiki", encoding="utf-8")
    (seed / "obsolete.md").write_text("Removed type", encoding="utf-8")
    publish_wiki.git(["add", "."], environment, seed)
    publish_wiki.git(["commit", "-m", "Seed test wiki"], environment, seed)
    publish_wiki.git(["push", "origin", "HEAD"], environment, seed)
    write_snapshot(
        snapshot,
        {"Home.md": "[API](library-jobs)", "_Sidebar.md": "nav", "library-jobs.md": "Jobs"},
        "a" * 40,
    )
    execute = publish_wiki.git

    def local_remote(arguments, environment, directory=None):
        """Redirect only cloning to the isolated bare repository; all Git operations remain real."""
        if arguments[0] == "clone":
            arguments = ["clone", str(remote), arguments[2]]
        return execute(arguments, environment, directory)

    monkeypatch.setattr(publish_wiki, "git", local_remote)
    publish_wiki.publish(snapshot, "owner/repository", ssh=True)
    first = execute(["rev-parse", "HEAD"], environment, remote)
    assert execute(["show", "HEAD:Home.md"], environment, remote) == "[API](library-jobs)"
    assert "obsolete.md" not in execute(["ls-tree", "--name-only", "HEAD"], environment, remote)
    assert execute(["rev-list", "--count", "HEAD"], environment, remote) == "2"
    publish_wiki.publish(snapshot, "owner/repository", ssh=True)
    assert execute(["rev-parse", "HEAD"], environment, remote) == first


def test_pr_checks_generate_without_secrets_and_only_main_publishes():
    """Wiki publication stays separate from pull requests, native builds and package releases."""
    validation = read_yaml(ROOT / ".github/workflows/reusable_validation.yml")["jobs"]
    assert "reference" in validation["required"]["needs"]
    assert "needs" not in validation["reference"]
    assert "reference.result" in validation["required"]["steps"][0]["run"]
    workflow = read_yaml(ROOT / ".github/workflows/documentation.yml")
    assert workflow["on"]["push"]["branches"] == ["main"]
    assert "pull_request" not in workflow["on"]
    for name in ("generate", "publish"):
        assert workflow["jobs"][name]["if"] == "github.ref == 'refs/heads/main'"
    steps = workflow["jobs"]["publish"]["steps"]
    assert steps[-1]["env"]["WIKI_TOKEN"] == "${{ secrets.WIKI_TOKEN }}"
    assert "--current-main" in steps[-1]["run"]
