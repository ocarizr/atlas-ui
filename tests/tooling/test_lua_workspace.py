"""Use one canonical source identity for the LuaLS workspace and annotation library."""

import json
from contextlib import contextmanager
from pathlib import Path

from scripts.dev import check_lua


def test_workspace_and_library_paths_are_canonical(tmp_path, monkeypatch):
    """Temporary-directory aliases must not register the same annotations under two URIs."""
    root = tmp_path / "repository"
    (root / ".ci").mkdir(parents=True)
    (root / ".ci/lua_tools.yaml").write_text("luals:\n  version: 3.19.2\n", encoding="utf-8")
    (root / ".luarc.json").write_text("{}", encoding="utf-8")
    (root / "tools/lua_api").mkdir(parents=True)
    (root / "tools/lua_api/atlas.lua").write_text("---@meta\n", encoding="utf-8")
    (tmp_path / "alias").mkdir()
    temporary = tmp_path / "analysis"
    temporary.mkdir()

    @contextmanager
    def temporary_directory(**kwargs):
        """Provide an equivalent noncanonical path without requiring platform-specific symlinks."""
        yield str(tmp_path / "alias/../analysis")

    commands = []

    def run(command, **kwargs):
        """Inspect the actual isolated analysis configuration before its cleanup."""
        commands.append(command)
        workspace = command[1].removeprefix("--check=")
        config = command[3].removeprefix("--configpath=")
        library = json.loads(Path(config).read_text(encoding="utf-8"))["workspace.library"]
        assert Path(workspace) == temporary / "workspace"
        assert Path(config) == temporary / "config.json"
        assert library == [(temporary / "workspace/tools/lua_api").as_posix()]
        assert command[4] == f"--logpath={temporary}/logs"

    monkeypatch.setattr(check_lua.tempfile, "TemporaryDirectory", temporary_directory)
    monkeypatch.setattr(check_lua, "tool_path", lambda *args: "lua-language-server.exe")
    monkeypatch.setattr(check_lua.subprocess, "check_output", lambda *args, **kwargs: "3.19.2\n")
    monkeypatch.setattr(check_lua.subprocess, "run", run)
    check_lua.check_files(root, ["tools/lua_api/atlas.lua"])
    assert len(commands) == 1
