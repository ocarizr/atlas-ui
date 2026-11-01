"""Check owned path naming and keep native SDK headers behind platform boundaries."""

import re
from pathlib import Path

OWNED_ROOTS = {
    "libs",
    "apps",
    "tools",
    "tests",
    "protocols",
    "assets",
    "cmake",
    "scripts",
    "docs",
    ".github",
    ".ci",
}
LEGACY_ROOTS = {"Engine", "Apps", "Tools", "Tests", "Protocols"}
REQUIRED_NAMES = {"CMakeLists.txt", "README.md", "CODEOWNERS"}
WINDOWS_HEADERS = {
    "windows.h",
    "d3d12.h",
    "dxgi1_4.h",
    "dxgi.h",
    "dxguid.h",
    "xaudio2.h",
    "oaidl.h",
    "unknwn.h",
    "objbase.h",
    "wrl/client.h",
    "directx-dxc/dxcapi.h",
}


def validate_architecture(root):
    """Reject owned naming violations and native SDK includes outside the Windows boundary."""
    errors = []
    for entry in Path(root).iterdir():
        if entry.name in LEGACY_ROOTS:
            errors.append(f"Owned path must use snake_case: {entry.name}")
        if not entry.is_dir() or entry.name not in OWNED_ROOTS:
            continue
        for path in entry.rglob("*"):
            relative = path.relative_to(root)
            if "__pycache__" in relative.parts:
                continue
            for part in relative.parts[1:]:
                if part not in REQUIRED_NAMES and not re.fullmatch(
                    r"[a-z0-9_]+(?:\.[a-z0-9_]+)*", part
                ):
                    errors.append(f"Owned path must use snake_case: {relative.as_posix()}")
                    break
            if path.suffix not in {".cpp", ".hpp", ".h"} or not path.is_file():
                continue
            includes = re.findall(
                r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]',
                path.read_text(encoding="utf-8"),
                re.MULTILINE,
            )
            if WINDOWS_HEADERS.intersection(header.lower() for header in includes):
                if not relative.as_posix().startswith(
                    ("libs/platform/windows/", "tests/platform/windows/")
                ):
                    errors.append(
                        f"Platform header outside Windows boundary: {relative.as_posix()}"
                    )
    if errors:
        raise ValueError("\n".join(errors))
