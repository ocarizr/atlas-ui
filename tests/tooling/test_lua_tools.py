"""Reproducible Lua tooling rejects corrupt archives and incompatible debugger inputs."""

import hashlib
import io
import zipfile

import pytest

from scripts.dev.setup_lua import adapt_debug_adapter, adapt_luapanda, install, unpack_verified


def archive(entries):
    """Create an isolated deterministic archive without downloading external tools."""
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as stream:
        for name, content in entries.items():
            stream.writestr(name, content)
    return output.getvalue()


@pytest.mark.parametrize(
    "name", ["../escape", "/escape", "C:/escape"], ids=["parent", "absolute", "drive"]
)
def test_rejects_archive_escape(tmp_path, name):
    """No archive member may escape the selected installation directory."""
    data = archive({name: "bad"})
    with pytest.raises(ValueError, match="archive path"):
        unpack_verified(data, hashlib.sha256(data).hexdigest(), tmp_path / "install")
    assert not (tmp_path / "escape").exists()


def test_rejects_checksum_before_extracting(tmp_path):
    """A compromised download never creates installation files."""
    with pytest.raises(ValueError, match="SHA-256"):
        unpack_verified(archive({"tool.exe": "bad"}), "0" * 64, tmp_path)
    assert not (tmp_path / "tool.exe").exists()


def test_extracts_verified_archive(tmp_path):
    """Verified nested files remain inside the installation root."""
    data = archive({"bin/tool.exe": "verified"})
    unpack_verified(data, hashlib.sha256(data).hexdigest(), tmp_path)
    assert (tmp_path / "bin/tool.exe").read_text() == "verified"


def test_agent_patch_requires_exact_upstream_pattern():
    """A new upstream revision requires an explicit compatibility review."""
    with pytest.raises(ValueError, match="LuaPanda compatibility"):
        adapt_luapanda("return {}")


def test_agent_patch_preserves_loop_key():
    """Lua 5.5 generic-for control variables are immutable."""
    source = 'k = basicSerialize(k)\nname, k)\n"[%s]", k)'
    patched = adapt_luapanda(source)
    assert "local serializedKey = basicSerialize(k)" in patched
    assert "name, serializedKey)" in patched
    assert '"[%s]", serializedKey)' in patched


def test_adapter_binds_loopback_only():
    """The developer editor must not expose the debugger server on every interface."""
    name = "extension/out/debug/luaDebug.js"
    data = archive({name: ".listen(this.TCPPort, 0, function () {});", "LICENSE": "retained"})
    with zipfile.ZipFile(io.BytesIO(adapt_debug_adapter(data))) as patched:
        assert b'.listen(this.TCPPort, "127.0.0.1", function ()' in patched.read(name)
        assert patched.read("LICENSE") == b"retained"


def test_adapter_patch_rejects_changed_upstream():
    """Do not silently lose the loopback-only policy after upgrading a dependency."""
    with pytest.raises(ValueError, match="loopback adapter patch"):
        adapt_debug_adapter(archive({"extension/out/debug/luaDebug.js": "changed"}))


def test_verified_existing_installation_is_not_replaced(tmp_path, monkeypatch):
    """Repeated setup works while VS Code holds a verified tool's executable open."""
    data = b'k = basicSerialize(k)\nname, k)\n"[%s]", k)'
    cache = tmp_path / ".tools/lua_dev"
    cache.mkdir(parents=True)
    (cache / "luapanda.download").write_bytes(data)
    specification = {"version": "fixture", "sha256": hashlib.sha256(data).hexdigest()}
    install(tmp_path, "luapanda", specification)

    def replace(*args, **kwargs):
        """Reject replacement rather than interfering with temporary-directory cleanup."""
        pytest.fail("matching installation must remain in place")

    monkeypatch.setattr("scripts.dev.setup_lua.shutil.move", replace)
    install(tmp_path, "luapanda", specification)
    assert (cache / "luapanda/atlas_version.txt").read_text() == "fixture"
