#!/usr/bin/env python3
"""Extract an explicitly selected native API into a language-neutral JSON model."""

import argparse
import json
from pathlib import Path
import sys

from binding_model import ModelError, apply_policies, validate_binding, validate_keys
from clang_parser import extract_api


def build_model(source_root, config, target, clang_args=()):
    """Compose native extraction and policy validation for the selected API."""
    validate_keys(config, {"schema_version", "headers", "clang_args", "declarations"}, "configuration")
    if config.get("schema_version") != 1:
        raise ModelError("Unsupported configuration schema_version")
    if not isinstance(config.get("headers"), list) or not config["headers"]:
        raise ModelError("Configuration must select entry headers")
    if not isinstance(config.get("declarations"), dict) or not config["declarations"]:
        raise ModelError("Configuration must select declarations")
    policies = config["declarations"]
    external = [name for name, policy in policies.items() if validate_binding(policy, name) == "external"]
    model, symbols = extract_api(source_root, config["headers"], policies.keys(), target,
                                 config.get("clang_args", []) + list(clang_args),
                                 external_declarations=external)
    return apply_policies(model, policies, symbols)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--target", required=True, help="Clang target triple")
    parser.add_argument("--clang-arg", action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    try:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        model = build_model(args.source_root, config, args.target, args.clang_arg)
        output = json.dumps(model, indent=2, ensure_ascii=False) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
            args.output.write_text(output, encoding="utf-8", newline="\n")
    except (ModelError, OSError, ValueError) as error:
        parser.exit(1, f"Binding model generation failed: {error}\n")

    print(f"Generated {len(model['declarations'])} declarations: {args.output}")


if __name__ == "__main__":
    sys.exit(main())
