"""Exercise the actual PowerShell installer with controlled downloads and isolated caches."""

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture
def installer_project(tmp_path):
    """Copy only the installer and a tiny pinned payload into a per-test repository."""
    (tmp_path / "scripts").mkdir()
    shutil.copyfile(ROOT / "scripts/install_doxygen.ps1", tmp_path / "scripts/install_doxygen.ps1")
    (tmp_path / ".ci").mkdir()
    digest = hashlib.sha256(b"verified archive").hexdigest()
    (tmp_path / ".ci/doxygen.yaml").write_text(
        yaml.safe_dump(
            {
                "version": "1.18.0",
                "windows_url": "https://example.invalid/doxygen.zip",
                "windows_sha256": digest,
            }
        ),
        encoding="utf-8",
    )
    return tmp_path


@pytest.mark.parametrize(
    "cached,outcomes,success,attempts",
    [
        ("valid", [], True, 0),
        ("invalid", ["valid"], True, 1),
        (None, ["invalid", "valid"], True, 2),
        (None, ["network", "valid"], True, 2),
        (None, ["invalid"] * 3, False, 3),
        (None, ["network"] * 3, False, 3),
    ],
    ids=[
        "verified-cache",
        "corrupt-cache",
        "corrupt-download",
        "interrupted-download",
        "persistent-corruption",
        "persistent-network-failure",
    ],
)
def test_verified_install_recovery(installer_project, cached, outcomes, success, attempts):
    """Only verified bytes may be extracted; failed downloads must not poison the next run."""
    root = installer_project
    archive = root / ".tools/doxygen/doxygen-1.18.0.zip"
    if cached:
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"verified archive" if cached == "valid" else b"corrupt cache")
    scenario = root / "scenario.json"
    scenario.write_text(json.dumps({"outcomes": outcomes}), encoding="utf-8")
    harness = root / "harness.ps1"
    harness.write_text(
        r"""
param($Root, $Python)
$ErrorActionPreference = 'Stop'
$global:attempts = 0
$global:extractions = 0
$global:scenario = Get-Content -Raw (Join-Path $Root 'scenario.json') | ConvertFrom-Json
function Invoke-WebRequest {
    param($Uri, $OutFile, $TimeoutSec, $Headers)
    $outcome = $global:scenario.outcomes[$global:attempts++]
    $content = if ($outcome -eq 'valid') { 'verified archive' } else { 'partial archive' }
    [IO.File]::WriteAllBytes($OutFile, [Text.Encoding]::UTF8.GetBytes($content))
    if ($outcome -eq 'network') { throw 'Controlled network interruption' }
}
function Expand-Archive {
    param($LiteralPath, $DestinationPath, [switch]$Force)
    ++$global:extractions
    $pin = Get-Content -Raw (Join-Path $Root '.ci/doxygen.yaml')
    $hash = (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $pin.Contains($hash)) { throw 'Forbidden extraction of unverified archive' }
    New-Item -ItemType Directory -Force $DestinationPath | Out-Null
    Set-Content (Join-Path $DestinationPath 'doxygen.ps1') "'1.18.0'"
}
function Get-ChildItem {
    param($LiteralPath, $Filter, [switch]$Recurse, [switch]$File)
    [pscustomobject]@{FullName = (Join-Path $LiteralPath 'doxygen.ps1')}
}
function Start-Sleep { param($Seconds) }
try {
    & (Join-Path $Root 'scripts/install_doxygen.ps1') -Python $Python
} finally {
    @{attempts=$global:attempts; extractions=$global:extractions} |
        ConvertTo-Json | Set-Content (Join-Path $Root 'observations.json')
}
""",
        encoding="utf-8",
    )
    result = subprocess.run(
        ["pwsh", "-NoProfile", "-File", str(harness), str(root), sys.executable],
        capture_output=True,
        text=True,
        timeout=30,
    )
    observed = json.loads((root / "observations.json").read_text(encoding="utf-8-sig"))
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    assert observed == {"attempts": attempts, "extractions": int(success)}
    assert not list(archive.parent.glob("*.partial"))
    if success:
        assert archive.read_bytes() == b"verified archive"
    else:
        assert not archive.exists()
        assert "after 3 attempts" in result.stderr
        if outcomes[0] == "invalid":
            assert "SHA256" in result.stderr
