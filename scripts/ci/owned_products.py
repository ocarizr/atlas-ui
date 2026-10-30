"""Provenance checks for Atlas libraries shared only within one workflow run."""

from pathlib import Path

import yaml

from scripts.ci.config import read_yaml


def write_product(root, component, configure, configuration, revision, run):
    metadata = Path(root) / 'share/atlas/ci' / f'{component}.yaml'
    metadata.parent.mkdir(parents=True, exist_ok=True)
    metadata.write_text(yaml.safe_dump(dict(component=component, configure=configure,
                                          configuration=configuration, revision=revision, run=run)),
                        encoding='utf-8')


def validate_products(root, components, configure, configuration, revision, run):
    for component in components:
        metadata = Path(root) / 'share/atlas/ci' / f'{component}.yaml'
        if not metadata.is_file():
            raise ValueError(f'Missing compiled component: {component}')
        expected = dict(component=component, configure=configure, configuration=configuration,
                        revision=revision, run=run)
        if read_yaml(metadata) != expected:
            raise ValueError(f'Incompatible compiled component: {component}')
