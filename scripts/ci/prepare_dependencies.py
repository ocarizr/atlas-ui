"""Prepare pinned manifest dependencies with live output and native-build heartbeats."""

import argparse
import os
import queue
import re
import subprocess
import threading
import time
from pathlib import Path


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
    args = parser.parse_args()
    executable = Path(".tools/vcpkg") / ("vcpkg.exe" if os.name == "nt" else "vcpkg")
    command = [str(executable.resolve()), "install", f"--triplet={args.triplet}",
               f"--x-install-root={Path(args.install_root).resolve()}", "--no-print-usage",
               "--clean-buildtrees-after-build", "--clean-packages-after-build"]
    print("Preparing manifest dependencies (Debug and Release); first uncached builds can be lengthy.", flush=True)
    raise SystemExit(run_logged(command, "reports/dependencies/install.log", ".tools/vcpkg/buildtrees"))


if __name__ == "__main__":
    main()
