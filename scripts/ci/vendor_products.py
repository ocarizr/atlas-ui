"""Version/ABI identity and reproducible manifests for compiled vendor products."""

import hashlib
import json
import os
import subprocess
from pathlib import Path

from scripts.ci.config import read_yaml


def manifest_for(root, group):
    root = Path(root)
    groups = read_yaml(root / '.ci/dependencies.yaml')['groups']
    if group not in groups:
        raise ValueError(f'Unknown dependency group: {group}')
    source = json.loads((root / 'vcpkg.json').read_text(encoding='utf-8'))
    packages = {item if isinstance(item, str) else item['name']: item
                for item in source['dependencies']}
    return {'name': f'atlas-vendor-{group}', 'version-semver': '1.0.0',
            'builtin-baseline': source['builtin-baseline'],
            'dependencies': [packages[name] for name in groups[group]]}


def toolchain_identity():
    installation = os.environ.get('VCToolsInstallDir', '').rstrip('\\/')
    sdk = os.environ.get('WindowsSDKVersion', '').strip('\\/')
    if not installation or not sdk:
        raise ValueError('Initialize the Windows toolchain before identifying dependency products')
    llvm = subprocess.run(['clang-cl', '--version'], check=True, capture_output=True, text=True)
    return {'msvc': Path(installation).name, 'sdk': sdk,
            'llvm': llvm.stdout.splitlines()[0], 'triplet': 'x64-windows'}


def fingerprint(root, group, toolchain):
    root = Path(root)
    manifest = manifest_for(root, group)
    digest = hashlib.sha256(json.dumps({'schema': 2, 'manifest': manifest, 'toolchain': toolchain},
                                    sort_keys=True).encode())
    digest.update((root / 'vcpkg-configuration.json').read_bytes())
    for item in manifest['dependencies']:
        name = item if isinstance(item, str) else item['name']
        for path in sorted((root / 'dependencies/vcpkg-ports' / name).rglob('*')):
            if path.is_file():
                digest.update(path.relative_to(root).as_posix().encode())
                digest.update(path.read_bytes())
    return digest.hexdigest()


def prepare_manifest(root, group, destination):
    root, destination = Path(root), Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'vcpkg.json').write_text(json.dumps(manifest_for(root, group)), encoding='utf-8')
    configuration = json.loads((root / 'vcpkg-configuration.json').read_text(encoding='utf-8'))
    configuration['overlay-ports'] = [str((root / path).resolve())
                                      for path in configuration.get('overlay-ports', [])]
    (destination / 'vcpkg-configuration.json').write_text(json.dumps(configuration), encoding='utf-8')
