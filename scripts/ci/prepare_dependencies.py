"""Prepare pinned manifest dependencies with live output and native-build heartbeats."""

import argparse
import os
import queue
import re
import shutil
import subprocess
import threading
import time
from pathlib import Path

import yaml

from scripts.ci.config import read_yaml
from scripts.ci.vendor_products import fingerprint, prepare_manifest, toolchain_identity


def native_progress(buildtrees):
    try:
        logs = list(Path(buildtrees).glob("*/install-*-out.log"))
        if not logs:
            return "waiting for native build logs"
        latest = max(logs, key=lambda path: path.stat().st_mtime_ns)
        with latest.open("rb") as source:
            source.seek(max(0, latest.stat().st_size - 65536))
            tail = source.read().decode("utf-8", errors="replace")
        counters = re.findall(r"\[(\d+/\d+)\]", tail)
        relative = latest.relative_to(buildtrees).as_posix()
        return f"{relative}: {counters[-1]}" if counters else f"{relative}: native build output recorded"
    except OSError:
        return "native logs are rotating during package cleanup"


def run_logged(command, destination, buildtrees):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    lines = queue.Queue()
    with destination.open("w", encoding="utf-8") as log:
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", errors="replace") as process:
            def read_output():
                try:
                    for line in process.stdout:
                        lines.put(line)
                finally:
                    lines.put(None)

            reader = threading.Thread(target=read_output)
            reader.start()
            try:
                while True:
                    try:
                        line = lines.get(timeout=30)
                    except queue.Empty:
                        line = (f"Dependency preparation still running ({int(time.monotonic() - started)}s); "
                                f"{native_progress(buildtrees)}\n")
                    if line is None:
                        break
                    print(line, end="", flush=True)
                    log.write(line)
                    log.flush()
            except BaseException:
                if process.poll() is None:
                    process.terminate()
                raise
            finally:
                process.wait()
                reader.join()
            return process.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--triplet", default="x64-windows")
    parser.add_argument("--install-root", default="build/dependencies/vcpkg_installed")
    parser.add_argument('--group')
    parser.add_argument('--key')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    if args.group:
        product = Path('build/vendor') / args.group
        expected = fingerprint(Path.cwd(), args.group, toolchain_identity())
        if args.key != expected:
            raise ValueError('Dependency identity differs from the planned versions/toolchain')
        if args.verify:
            metadata = read_yaml(product / 'product.yaml')
            if metadata != {'group': args.group, 'key': expected}:
                raise ValueError('Dependency product identity mismatch')
            return
        manifest = Path('build/vendor_manifests') / args.group
        prepare_manifest(Path.cwd(), args.group, manifest)
        args.install_root = str(product / 'installed')
    executable = Path(".tools/vcpkg") / ("vcpkg.exe" if os.name == "nt" else "vcpkg")
    command = [str(executable.resolve()), "install", f"--triplet={args.triplet}",
               f"--x-install-root={Path(args.install_root).resolve()}", "--no-print-usage",
               "--clean-buildtrees-after-build", "--clean-packages-after-build"]
    if args.group:
        command.append(f'--x-manifest-root={manifest.resolve()}')
    print("Preparing manifest dependencies (Debug and Release); first uncached builds can be lengthy.", flush=True)
    result = run_logged(command, 'reports/dependencies/install.log', '.tools/vcpkg/buildtrees')
    if result == 0 and args.group:
        tooling = product / 'toolchain'
        for directory in ('scripts', 'triplets'):
            shutil.copytree(Path('.tools/vcpkg') / directory, tooling / directory, dirs_exist_ok=True)
        for name in ('.vcpkg-root', 'vcpkg.exe', 'LICENSE.txt'):
            shutil.copy2(Path('.tools/vcpkg') / name, tooling / name)
        (product / 'product.yaml').write_text(yaml.safe_dump({'group': args.group, 'key': expected}),
                                             encoding='utf-8')
    raise SystemExit(result)


if __name__ == "__main__":
    main()
