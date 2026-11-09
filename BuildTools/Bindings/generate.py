#!/usr/bin/env python3
"""Extract selected API entry points and their types into a language-neutral model."""

import argparse
import json
from pathlib import Path
import sys

from binding_model import ModelError, apply_policies, validate_binding, validate_keys, validate_method_selection
from clang_parser import extract_api


def build_model(source_root, config, target, clang_args=()):
    """Compose native extraction and policy validation for the selected API."""
    validate_keys(config, {"schema_version", "headers", "clang_args", "exports", "declarations", "conventions"}, "configuration")
    if config.get("schema_version") != 1:
        raise ModelError("Unsupported configuration schema_version")
    if not isinstance(config.get("headers"), list) or not config["headers"]:
        raise ModelError("Configuration must select entry headers")
    policies = config.get("declarations", {})
    if not isinstance(policies, dict):
        raise ModelError("declarations must contain semantic overrides")
    # Older configurations select every annotated declaration. With exports,
    # annotations only refine declarations reached from the selected roots.
    exports = config.get("exports", list(policies))
    export_headers, excluded_headers, excluded_declarations = [], [], []
    if isinstance(exports, dict):
        validate_keys(exports, {"headers", "exclude_headers", "exclude", "include"}, "exports")
        for key in ("headers", "exclude_headers", "exclude", "include"):
            values = exports.get(key, [])
            if (not isinstance(values, list) or any(not isinstance(value, str) or not value for value in values) or
                    len(set(values)) != len(values) or (key == "headers" and not values)):
                raise ModelError(f"exports.{key} must be a list of unique nonempty strings; headers must not be empty")
        export_headers = exports["headers"]
        excluded_headers = exports.get("exclude_headers", [])
        excluded_declarations = exports.get("exclude", [])
        exports = exports.get("include", [])
    elif (not isinstance(exports, list) or not exports or
          any(not isinstance(name, str) or not name for name in exports) or
          len(set(exports)) != len(exports)):
        raise ModelError("exports must be a nonempty list of unique declaration names or a header selection")
    external = [name for name, policy in policies.items() if validate_binding(policy, name) == "external"]
    methods = {name: policy["include_methods"] for name, policy in policies.items() if "include_methods" in policy}
    exclusions = {name: policy["exclude_methods"] for name, policy in policies.items() if "exclude_methods" in policy}
    if methods.keys() & exclusions.keys():
        raise ModelError("include_methods and exclude_methods are mutually exclusive")
    for key, selections in (("include_methods", methods), ("exclude_methods", exclusions)):
        for name, selection in selections.items():
            validate_method_selection(selection, name, key)
    model, symbols = extract_api(source_root, config["headers"], exports, target,
                                 config.get("clang_args", []) + list(clang_args),
                                 external_declarations=external, method_selections=methods, method_exclusions=exclusions, include_dependencies=True,
                                 export_headers=export_headers, excluded_headers=excluded_headers,
                                 excluded_declarations=excluded_declarations)
    return apply_policies(model, policies, symbols, config.get("conventions"))


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
