"""Check or apply pinned C++/Protobuf/Python formatting for owned project sources."""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from scripts.ci.config import read_yaml
from scripts.ci.cpp_spacing import separate_control_blocks
from scripts.ci.documentation import check_documentation
from scripts.ci.format_selection import formatting_files
from scripts.ci.impact import repository_files


def formatter_executable(override=None):
    """Resolve the pinned formatter beside Python before considering toolchain PATH."""
    executable = (
        override
        or shutil.which("clang-format", path=str(Path(sys.executable).parent))
        or shutil.which("clang-format")
    )
    if not executable:
        raise ValueError("Install scripts/ci/requirements.txt to provide clang-format 19.1.7")
    version = subprocess.check_output([executable, "--version"], text=True)
    if version.split("version ", 1)[-1].split()[0] != "19.1.7":
        raise ValueError(f"Expected clang-format 19.1.7, got {version.strip()}")
    return executable


def format_commands(files, fix, clang_format=None):
    """Build separate language commands without shell interpolation or vendor traversal."""
    cpp = [path for path in files if Path(path).suffix != ".py"]
    python = [path for path in files if Path(path).suffix == ".py"]
    commands = []
    if cpp:
        flags = ["-i"] if fix else ["--dry-run", "--Werror"]
        commands.append([formatter_executable(clang_format), *flags, "--style=file", "--", *cpp])
    if python:
        flags = ["--fix"] if fix else []
        commands.append([sys.executable, "-m", "ruff", "check", *flags, "--", *python])
        flags = [] if fix else ["--check"]
        commands.append([sys.executable, "-m", "ruff", "format", *flags, "--", *python])
    return commands


def main():
    """Run project/file-scoped formatting and documentation checks from the repository root."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component")
    parser.add_argument("--path", action="append", default=[])
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("--clang-format")
    args = parser.parse_args()
    root = Path.cwd()
    config = read_yaml(root / ".ci/components.yaml")
    files = formatting_files(config, repository_files(root), args.component)
    if args.path:
        requested = {
            Path(path).resolve().relative_to(root.resolve()).as_posix() for path in args.path
        }
        unknown = requested - set(files)
        if unknown:
            raise ValueError(f"Files outside the selected owned sources: {sorted(unknown)}")
        files = [path for path in files if path in requested]
    if not files:
        print("No owned sources selected for formatting.")
        return
    failed = False
    for command in format_commands(files, args.fix, args.clang_format):
        failed |= subprocess.run(command, check=False).returncode != 0
    for name in files:
        if Path(name).suffix in {".py", ".proto"}:
            continue
        path = Path(name)
        source = path.read_bytes()
        spaced = separate_control_blocks(source)
        if spaced != source:
            if args.fix:
                path.write_bytes(spaced)
            else:
                print(f"{name}: separate completed control blocks with a blank line")
                failed = True
    for path in files:
        errors = check_documentation(path)
        for error in errors:
            print(error)
        failed |= bool(errors)
    if failed:
        raise SystemExit(1)
    print(f"Formatting and documentation passed: {args.component or 'all'} ({len(files)} files).")


if __name__ == "__main__":
    main()
