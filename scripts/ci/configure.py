"""Configure component builds using exact installed products without package installation."""

import argparse
import os
import re
import subprocess
from pathlib import Path


def prepared_options(environment):
    installed = Path(environment['ATLAS_CI_INSTALLED_DIR']).resolve()
    owned = Path(environment.get('ATLAS_OWNED_ROOT', 'build/owned')).resolve()
    toolchain = Path(environment['ATLAS_CI_TOOLCHAIN'])
    return [
        '--fresh', '-DATLAS_USE_PREPARED_DEPENDENCIES=ON', '-DVCPKG_MANIFEST_INSTALL=OFF',
        '-DCMAKE_CONFIGURATION_TYPES=' + environment['ATLAS_BUILD_CONFIGURATION'],
        '-DCMAKE_TOOLCHAIN_FILE=' + toolchain.as_posix(),
        '-DVCPKG_INSTALLED_DIR=' + installed.as_posix(),
        '-DCMAKE_PREFIX_PATH=' + (installed / 'x64-windows').as_posix(),
        '-DATLAS_PREBUILT_ROOT=' + owned.as_posix(),
    ]


def configure_command(preset, version=None, environment=None, components=None):
    environment = os.environ if environment is None else environment
    command = ["cmake", "--preset", preset]
    if components:
        command.append('-DATLAS_COMPONENTS=' + ';'.join(components))
    if environment.get('ATLAS_CI_ARTIFACTS') == '1':
        command.extend(prepared_options(environment))
    else:
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
