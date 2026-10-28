"""Shared configuration loading and GitHub output helpers."""

import copy
import re
from pathlib import Path

import yaml


class ConfigLoader(yaml.SafeLoader):
    pass


# Preserve YAML keys such as GitHub's `on`; recognize only true/false as booleans.
ConfigLoader.yaml_implicit_resolvers = copy.deepcopy(yaml.SafeLoader.yaml_implicit_resolvers)
for first, resolvers in ConfigLoader.yaml_implicit_resolvers.items():
    ConfigLoader.yaml_implicit_resolvers[first] = [
        item for item in resolvers if item[0] != "tag:yaml.org,2002:bool"
    ]
ConfigLoader.add_implicit_resolver(
    "tag:yaml.org,2002:bool", re.compile(r"^(?:true|false)$", re.IGNORECASE), list("tTfF")
)


def unique_mapping(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise ValueError(f"Duplicate YAML key: {key}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


ConfigLoader.add_constructor("tag:yaml.org,2002:map", unique_mapping)


def read_yaml(path):
    with Path(path).open(encoding="utf-8") as source:
        result = yaml.load(source, Loader=ConfigLoader)
    if not isinstance(result, dict):
        raise ValueError(f"Expected a YAML mapping: {path}")
    return result


def write_outputs(values, destination):
    if not destination:
        return
    with Path(destination).open("a", encoding="utf-8") as output:
        for name, value in values.items():
            value = str(value).lower() if isinstance(value, bool) else str(value)
            if "\n" in value or "\r" in value:
                raise ValueError("Output values must fit on one line")
            output.write(f"{name}={value}\n")
