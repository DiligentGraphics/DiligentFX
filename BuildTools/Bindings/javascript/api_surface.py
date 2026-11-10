#!/usr/bin/env python3
"""Describe the generated binding contract and compare or explicitly update a baseline."""

import argparse
import difflib
import json
from pathlib import Path
import sys


def build_surface(generator):
    """Use the emitter's effective selection and policies, after successful generation.

    One entry per declaration/member keeps changes local in diffs. Named public
    aliases remain visible; implementation typedef chains reduce to scalar types
    with explicit widths. Field and method order is irrelevant to these wrappers,
    while parameter order is retained.
    """
    aliases = {name for name, d in generator.types.items() if d["kind"] == "alias"}

    def native_type(t):
        qualifiers = " ".join(q for q in ("const", "volatile") if t.get(q))
        kind = t["kind"]
        if kind == "alias" and t["name"] not in aliases:
            target = dict(t["target"])
            for q in ("const", "volatile"):
                target[q] = t.get(q, False) or target.get(q, False)
            return native_type(target)
        if kind in ("alias", "named"):
            result = t["name"]
        elif kind == "scalar":
            result = f'{t["name"]}:{t["bits"]}'
        elif kind == "void":
            result = "void"
        elif kind in ("pointer", "reference"):
            result = native_type(t["target"]) + ("*" if kind == "pointer" else "&")
            return result + (" " + qualifiers if qualifiers else "")
        elif kind == "array":
            element = dict(t["element"])
            for q in ("const", "volatile"):
                element[q] = t.get(q, False) or element.get(q, False)
            return f'{native_type(element)}[{t["count"]}]'
        else:
            raise ValueError(f"Unsupported API surface type: {kind}")
        return (qualifiers + " " if qualifiers else "") + result

    def policy(value):
        # Selection has already been applied. Reasons and binding mechanisms
        # explain configuration rather than change the exposed contract.
        return {k: v for k, v in value.get("policy", {}).items()
                if k not in ("binding", "reason", "include_methods", "exclude_methods")}

    def value(item):
        result = {"type": native_type(item["type"])}
        if "name" in item:
            result["name"] = item["name"]
        if item.get("default") is not None:
            result["default"] = item["default"]
        if policy(item):
            result["policy"] = policy(item)
        return result

    def callable_entry(item, name, kind):
        result = {"kind": kind, "result": value(item["result"]),
                  "parameters": [value(p) for p in item["parameters"]]}
        if kind == "method":
            result["const"] = item["const"]
        if policy(item):
            result["policy"] = policy(item)
        if name in generator.scopes:
            result["scope"] = generator.scopes[name]
        if name in generator.fixed_arguments:
            result["fixed_arguments"] = generator.fixed_arguments[name]
        return result

    entries = {}
    for name, d in generator.types.items():
        if name in generator.native_only or d["kind"] == "external":
            continue
        kind = d["kind"]
        entry = {"kind": kind}
        if policy(d):
            entry["policy"] = policy(d)
        if kind == "function":
            entry = callable_entry(d, name, kind)
        elif kind in ("record", "interface"):
            entry["bases"] = d["bases"]
            for field in d["fields"]:
                field_name = name + "::" + field["name"]
                member = value(field)
                del member["name"]
                member["kind"] = "field"
                if field_name in generator.field_count_functions:
                    member["count_function"] = generator.field_count_functions[field_name]
                entries[field_name] = member
            for method in d["methods"]:
                method_name = name + "::" + method["name"]
                if method_name not in generator.releases:
                    entries[method_name] = callable_entry(method, method_name, "method")
        elif kind == "enum":
            entry["underlying_type"] = native_type(d["underlying_type"])
            for enumerator in d["values"]:
                entries[name + "::" + enumerator["name"]] = {"kind": "enumerator", "value": enumerator["value"]}
        elif kind in ("alias", "constant"):
            entry["type"] = native_type(d["type"])
            if kind == "constant":
                entry["value"] = d["value"]
        else:
            raise ValueError(f"Unsupported API surface declaration: {kind}")
        entries[name] = entry
    return {"schema_version": 1, "target": "wasm32", "entries": entries}


def format_surface(surface):
    """Canonical JSON with one declaration/member per line, not parser metadata."""
    def encode(value):
        return json.dumps(value, ensure_ascii=False, sort_keys=True)

    lines = ["{", f'  "schema_version": {encode(surface["schema_version"])},',
             f'  "target": {encode(surface["target"])},', '  "entries": {']
    lines += [f"    {encode(name)}: {encode(entry)}," for name, entry in sorted(surface["entries"].items())]
    if surface["entries"]:
        lines[-1] = lines[-1].removesuffix(",")
    return "\n".join([*lines, "  }", "}", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--actual", type=Path, required=True, help="Surface produced by binding generation")
    parser.add_argument("--baseline", type=Path, required=True, help="Checked-in API surface")
    parser.add_argument("--update", action="store_true", help="Explicitly accept the current surface after updating its tests")
    args = parser.parse_args()
    try:
        actual = format_surface(json.loads(args.actual.read_text(encoding="utf-8")))
        if args.update:
            args.baseline.write_text(actual, encoding="utf-8", newline="\n")
            print(f"Updated API surface baseline: {args.baseline}")
            return 0
        if not args.baseline.exists():
            parser.exit(1, "API surface baseline is missing. Add the corresponding tests and explicitly update the baseline.\n")
        baseline = format_surface(json.loads(args.baseline.read_text(encoding="utf-8")))
        if actual != baseline:
            diff = "".join(difflib.unified_diff(baseline.splitlines(keepends=True), actual.splitlines(keepends=True),
                                               fromfile=str(args.baseline), tofile=str(args.actual)))
            parser.exit(1, "Binding API surface changed. Update the corresponding tests, then explicitly update the baseline.\n" + diff)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"API surface check failed: {error}\n")
    print("Binding API surface matches the baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
