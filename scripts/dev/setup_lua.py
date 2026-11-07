"""Install SHA-256-pinned Windows Lua authoring tools below .tools/lua_dev."""

import argparse
import hashlib
import io
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

from scripts.ci.config import read_yaml


def verify(data, digest):
    """Reject changed downloads before executing, adapting or extracting them."""
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("Lua tool SHA-256 mismatch")


def unpack_verified(data, digest, destination):
    """Validate every archive path before extracting any verified file."""
    verify(data, digest)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            path = PurePosixPath(member.filename.replace("\\", "/"))
            if path.is_absolute() or ".." in path.parts or ":" in member.filename:
                raise ValueError("Unsafe Lua tool archive path")
            if (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError("Unsafe Lua tool archive path: symlink")
        archive.extractall(destination)


def adapt_luapanda(source):
    """Preserve upstream semantics while avoiding a Lua 5.5 immutable loop-key assignment."""
    replacements = {
        "k = basicSerialize(k)": "local serializedKey = basicSerialize(k)",
        "name, k)": "name, serializedKey)",
        '"[%s]", k)': '"[%s]", serializedKey)',
    }
    for before, after in replacements.items():
        if source.count(before) != 1:
            raise ValueError("LuaPanda compatibility patch no longer matches pinned source")
        source = source.replace(before, after)
    return source


def adapt_debug_adapter(data):
    """Keep the pinned LuaPanda VS Code listener on loopback instead of every interface."""
    output = io.BytesIO()
    name = "extension/out/debug/luaDebug.js"
    with zipfile.ZipFile(io.BytesIO(data)) as source, zipfile.ZipFile(output, "w") as destination:
        script = source.read(name).decode("utf-8")
        before = ".listen(this.TCPPort, 0, function ()"
        if script.count(before) != 1:
            raise ValueError("LuaPanda loopback adapter patch no longer matches pinned source")
        script = script.replace(before, '.listen(this.TCPPort, "127.0.0.1", function ()')
        for member in source.infolist():
            destination.writestr(
                member, script.encode("utf-8") if member.filename == name else source.read(member)
            )
    return output.getvalue()


def install(root, name, specification):
    """Cache verified archives and replace a tool only after successful staging."""
    base = root / ".tools/lua_dev"
    base.mkdir(parents=True, exist_ok=True)
    cache = base / (name + ".download")
    if cache.exists():
        data = cache.read_bytes()
        verify(data, specification["sha256"])
    else:
        request = urllib.request.Request(
            specification["url"], headers={"User-Agent": "Atlas-Lua-Setup"}
        )
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(64 * 1024 * 1024 + 1)
        verify(data, specification["sha256"])
        cache.write_bytes(data)
    with tempfile.TemporaryDirectory(prefix=name + "-", dir=base) as temporary:
        staging = Path(temporary) / "install"
        staging.mkdir()
        if name == "luapanda":
            source = adapt_luapanda(data.decode("utf-8"))
            (staging / "LuaPanda.lua").write_text(source, encoding="utf-8", newline="\n")
        elif name.endswith("_extension"):
            content = adapt_debug_adapter(data) if name == "luapanda_extension" else data
            (staging / "extension.vsix").write_bytes(content)
        else:
            unpack_verified(data, specification["sha256"], staging)
        (staging / "atlas_version.txt").write_text(str(specification["version"]), encoding="utf-8")
        destination = base / name
        expected = [path for path in staging.rglob("*") if path.is_file()]
        if destination.is_dir() and all(
            (destination / path.relative_to(staging)).is_file()
            and (destination / path.relative_to(staging)).read_bytes() == path.read_bytes()
            for path in expected
        ):
            print(f"Verified existing {name} {specification['version']}")
            return
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(staging, destination)
    print(f"Verified {name} {specification['version']}")


def main():
    """Install only explicitly selected tools, or the complete contributor set."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tools",
        nargs="+",
        choices=["luapanda", "luals", "stylua", "luapanda_extension", "luals_extension"],
    )
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    specifications = read_yaml(root / ".ci/lua_tools.yaml")
    for name in arguments.tools or specifications:
        install(root, name, specifications[name])


if __name__ == "__main__":
    main()
