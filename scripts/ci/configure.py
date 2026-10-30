"""Configure a build using the prepared dependency tree and CI binary-only policy."""

import argparse
import os
import re
import subprocess


def configure_command(preset, version=None, environment=None):
    environment = os.environ if environment is None else environment
    command = ["cmake", "--preset", preset]
    installed = environment.get("ATLAS_VCPKG_INSTALLED_DIR")
    if installed:
        command.append(f"-DVCPKG_INSTALLED_DIR={installed}")
    if environment.get("ATLAS_REQUIRE_BINARY_CACHE") == "1":
        command.append("-DVCPKG_INSTALL_OPTIONS=--only-binarycaching")
    if version is not None and version != "development":
        if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
            raise ValueError("Invalid package version")
        command.append(f"-DATLAS_VERSION={version}")
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--version")
    args = parser.parse_args()
    subprocess.run(configure_command(args.preset, args.version), check=True)


if __name__ == "__main__":
    main()
