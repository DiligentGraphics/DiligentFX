"""Load shared binding configuration with optional backend-specific extensions."""

import json
from pathlib import Path

from binding_model import ModelError


def _merge(base, extension, key=None):
    if isinstance(base, dict) and isinstance(extension, dict):
        result = dict(base)
        for key, value in extension.items():
            result[key] = _merge(base[key], value, key) if key in base else value
        return result
    if isinstance(base, list) and isinstance(extension, list):
        if key == "clang_args":
            # Compiler options are an ordered token sequence, not a selection
            # set. Repeated options such as "-I" must keep their arguments.
            return base + extension
        return base + [value for value in extension if value not in base]
    return extension


def load_config(path, _parents=()):
    """Resolve `extends` relative to its file, leaving API paths source-root relative.

    Objects merge recursively; clang_args concatenate without deduplication;
    other lists append entries not already in the base. Other values replace
    the base value. Semantic validation belongs to the model or adapter consumer,
    after the complete configuration is assembled.
    """
    path = Path(path).resolve()
    if path in _parents:
        raise ModelError(f"Cyclic binding configuration inheritance: {path}")
    config = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ModelError(f"Binding configuration must be an object: {path}")
    if "extends" not in config:
        return config
    parent = config.pop("extends")
    if not isinstance(parent, str) or not parent:
        raise ModelError(f"Configuration extends must name a base file: {path}")
    base = load_config(path.parent / parent, _parents + (path,))
    return _merge(base, config)
