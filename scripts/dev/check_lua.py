"""Run pinned LuaLS diagnostics for one owned Lua component and the embedded Lua 5.5 API."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.format_selection import formatting_files
from scripts.ci.impact import repository_files


def tool_path(name, executable):
    """Reject incomplete installations or a stale version marker before invoking a tool."""
    root = Path(__file__).resolve().parents[2]
    specification = read_yaml(root / ".ci/lua_tools.yaml")[name]
    directory = root / ".tools/lua_dev" / name
    marker = directory / "atlas_version.txt"
    result = directory / executable
    if (
        not result.is_file()
        or not marker.is_file()
        or marker.read_text().strip() != str(specification["version"])
    ):
        raise ValueError(f"Run python -m scripts.dev.setup_lua --tools {name}")
    return str(result)


def lua_formatter():
    """Use the verified StyLua installation, not an unrelated executable on PATH."""
    executable = tool_path("stylua", "stylua.exe")
    expected = read_yaml(Path(__file__).resolve().parents[2] / ".ci/lua_tools.yaml")["stylua"][
        "version"
    ]
    if (
        subprocess.check_output([executable, "--version"], text=True).strip()
        != f"stylua {expected}"
    ):
        raise ValueError("Unexpected StyLua executable version")
    return executable


def check_files(root, files):
    """Analyze selected sources in an isolated workspace with editor-only library annotations."""
    executable = tool_path("luals", "bin/lua-language-server.exe")
    version = read_yaml(root / ".ci/lua_tools.yaml")["luals"]["version"]
    if version != subprocess.check_output([executable, "--version"], text=True).strip():
        raise ValueError("Unexpected Lua Language Server executable version")
    with tempfile.TemporaryDirectory(prefix="atlas-luals-") as temporary:
        workspace = Path(temporary) / "workspace"
        workspace.mkdir()
        for name in files:
            destination = workspace / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes((root / name).read_bytes())
        configuration = json.loads((root / ".luarc.json").read_text(encoding="utf-8"))
        annotations = workspace / "tools/lua_api"
        configuration["workspace.library"] = [
            (annotations if annotations.is_dir() else root / "tools/lua_api").as_posix()
        ]
        config = Path(temporary) / "config.json"
        config.write_text(json.dumps(configuration), encoding="utf-8")
        subprocess.run(
            [
                executable,
                f"--check={workspace}",
                "--checklevel=Warning",
                f"--configpath={config}",
                f"--logpath={temporary}/logs",
            ],
            check=True,
        )


def main():
    """Select the same direct component ownership used by formatting and CI impact analysis."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component")
    parser.add_argument(
        "--setup",
        action="store_true",
        help="Install pinned analysis/formatting tools when Lua is selected",
    )
    arguments = parser.parse_args()
    root = Path.cwd()
    files = [
        name
        for name in formatting_files(
            read_yaml(root / ".ci/components.yaml"), repository_files(root), arguments.component
        )
        if name.endswith(".lua")
    ]
    if files:
        if arguments.setup:
            from scripts.dev.setup_lua import install

            specifications = read_yaml(root / ".ci/lua_tools.yaml")
            for name in ("luals", "stylua"):
                install(root, name, specifications[name])
        check_files(root, files)
    else:
        print("No owned Lua sources in this component.")


if __name__ == "__main__":
    main()
