"""Validate debugger/shipping capability selection without compiling third-party libraries."""

import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize(
    ("debugger", "shipping", "allowed"),
    [(False, False, True), (True, False, True), (False, True, True), (True, True, False)],
    ids=["normal", "development", "shipping", "forbidden-shipping-debugger"],
)
def test_debugger_build_policy(tmp_path, debugger, shipping, allowed):
    """Shipping configurations reject development capability before dependency discovery."""
    script = tmp_path / "policy.cmake"
    script.write_text(
        f'include("{(ROOT / "cmake/atlas_lua_development.cmake").as_posix()}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            "cmake",
            f"-DATLAS_ENABLE_LUA_DEBUGGER={'ON' if debugger else 'OFF'}",
            f"-DATLAS_SHIPPING_BUILD={'ON' if shipping else 'OFF'}",
            "-P",
            str(script),
        ],
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert (result.returncode == 0) == allowed, result.stdout + result.stderr
    if not allowed:
        assert "Shipping builds cannot enable the Lua debugger" in result.stderr
