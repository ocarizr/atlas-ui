"""Map owned sources to independent formatting contexts without consumer expansion."""

from pathlib import Path

from scripts.ci.config import matches

FORMAT_SUFFIXES = {
    ".cpp",
    ".cc",
    ".cxx",
    ".c",
    ".hpp",
    ".h",
    ".hxx",
    ".inl",
    ".ixx",
    ".proto",
    ".py",
}


def formatting_contexts(config):
    """Combine build-component ownership with formatting-only tooling contexts."""
    return {
        **{name: item["paths"] for name, item in config["components"].items()},
        **config.get("formatting_contexts", {}),
    }


def formatting_files(config, files, component=None):
    """Return sorted owned sources, optionally limited to one registered context."""
    contexts = formatting_contexts(config)
    if component is not None and component not in contexts:
        raise ValueError(f"Unknown formatting component: {component}")
    external = (
        "third_party/",
        "dependencies/",
        "build/",
        "generated/",
        "tmp/",
        ".local/",
        ".tools/",
    )
    all_patterns = [pattern for paths in contexts.values() for pattern in paths]
    for path in files:
        if (
            Path(path).suffix in FORMAT_SUFFIXES
            and not path.startswith(external)
            and not matches(path, all_patterns)
        ):
            raise ValueError(f"Unmapped formatting source: {path}")
    patterns = (
        contexts[component] if component else [p for paths in contexts.values() for p in paths]
    )
    return sorted(
        {path for path in files if Path(path).suffix in FORMAT_SUFFIXES and matches(path, patterns)}
    )


def formatting_plan(config, changed, files, full=False):
    """Select direct owners; shared formatter policies invalidate all populated contexts."""
    formatting_files(config, files)
    global_change = full or any(
        matches(path, config.get("formatting_global_paths", [])) for path in changed
    )
    return [
        name
        for name, patterns in sorted(formatting_contexts(config).items())
        if formatting_files(config, files, name)
        and (global_change or any(matches(path, patterns) for path in changed))
    ]
