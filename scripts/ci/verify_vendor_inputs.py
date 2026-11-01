"""Verify the version and ABI of every dependency product consumed by a component."""

import argparse
import json
import os
from pathlib import Path

from scripts.ci.config import read_yaml, write_outputs
from scripts.ci.parallel_plan import plan_components
from scripts.ci.vendor_products import fingerprint, toolchain_identity


def main():
    """Validate restored vendor-product fingerprints before allowing CMake consumption."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", action="store_true")
    args = parser.parse_args()
    component = os.environ["COMPONENT"]
    config = read_yaml(".ci/components.yaml")
    if args.plan:
        item = config["components"][component]
        parents = plan_components(config, {component})["prerequisites"][component]
        write_outputs(
            {
                "groups": json.dumps(item["vendors"]),
                "has-owned": bool(parents),
                "owned-pattern": "@(" + "|".join(parents) + ")",
            },
            os.environ["GITHUB_OUTPUT"],
        )
        return
    keys = json.loads(os.environ["KEYS"])
    toolchain = toolchain_identity()
    for group in config["components"][component]["vendors"]:
        root = Path("build/vendor") / group
        actual = read_yaml(root / "product.yaml")
        expected = {"group": group, "key": fingerprint(Path.cwd(), group, toolchain)}
        if actual != expected or keys[group] != expected["key"]:
            raise ValueError(f"Incompatible dependency product: {group}")
        if not (root / "installed/x64-windows").is_dir():
            raise ValueError(f"Missing installed dependency product: {group}")


if __name__ == "__main__":
    main()
