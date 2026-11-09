#!/usr/bin/env python3
"""Generate Wasm32 call glue, JavaScript wrappers, and TypeScript declarations."""

import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from binding_model import ModelError, apply_values_policy, record_usage, resolve_span_length, unalias


def short(name):
    return name.removeprefix("Diligent::")


def symbol(name):
    return "binding_" + name.replace("::", "_")


def cpp_type(t):
    qualifier = "const " if t["const"] else ""
    if t["kind"] in ("alias", "named"):
        return qualifier + t["name"]
    if t["kind"] in ("pointer", "reference"):
        return cpp_type(t["target"]) + ("*" if t["kind"] == "pointer" else "&")
    if t["kind"] == "void":
        return qualifier + "void"
    names = {"char_s": "char", "char_u": "char", "schar": "signed char", "uchar": "unsigned char",
             "ushort": "unsigned short", "uint": "unsigned int", "ulong": "unsigned long",
             "longlong": "long long", "ulonglong": "unsigned long long"}
    if t["kind"] == "scalar":
        return qualifier + names.get(t["name"], t["name"])
    raise ModelError(f"JavaScript backend: unsupported native type {t['kind']}")


def ts_scalar(kind):
    return {"u64": "bigint", "i64": "bigint", "bool": "boolean", "void": "void"}.get(kind, "number")


class Generator:
    """An emitter over the validated model; does not import or invoke Clang.

    Struct layout and default expressions stay in native code. The JS runtime
    receives scalar conversion and ownership information, never vtable offsets.
    """

    def __init__(self, model, module, headers, adapters=None):
        if model.get("schema_version") != 1 or not model.get("target", "").startswith("wasm32-"):
            raise ModelError("JavaScript generation requires a validated Wasm32 model")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", module) or not headers:
            raise ModelError("A module identifier and at least one entry header are required")
        self.module = module
        self.conventions = model.get("conventions")
        # Backend selection and policies must not modify the language-neutral model.
        self.types = {d["name"]: deepcopy(d) for d in model["declarations"]}
        if adapters and (not isinstance(adapters, dict) or adapters.get("schema_version") != 1 or
                         adapters.keys() - {"schema_version", "scoped_bytes", "native_only", "field_policies", "fixed_arguments", "field_count_functions"}):
            raise ModelError("Invalid JavaScript adapter configuration")
        self.native_only = set((adapters or {}).get("native_only", []))
        self.fixed_arguments = (adapters or {}).get("fixed_arguments", {})
        self.field_count_functions = (adapters or {}).get("field_count_functions", {})
        if not isinstance(self.field_count_functions, dict):
            raise ModelError("field_count_functions must map descriptor fields to native functions")
        self.configure_selection((adapters or {}).get("field_policies", {}))
        for name, function in self.field_count_functions.items():
            owner, _, member = name.rpartition("::")
            field = next((f for f in self.types.get(owner, {}).get("fields", []) if f["name"] == member), {})
            if (not isinstance(function, str) or not re.fullmatch(r"[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*", function) or
                    unalias(field.get("type", {"kind": "void", "const": False, "volatile": False})).get("kind") != "pointer" or "length" in field.get("policy", {})):
                raise ModelError(f"{name}: field count adapter requires a pointer without a length and a qualified native function name")
        scopes = (adapters or {}).get("scoped_bytes", {})
        if not isinstance(scopes, dict):
            raise ModelError("scoped_bytes must map qualified method names to adapters")
        self.scopes = {}
        self.releases = set()
        for name, scope in scopes.items():
            self.scopes[name] = self.validate_scope(name, scope)
            self.releases.add(name.rsplit("::", 1)[0] + "::" + scope["release"])
        self.record_arrays = {}
        self.constants = {}
        self.schema = {"records": {}, "interfaces": {}, "functions": {}, "enums": {}}
        self.cpp = ['// Generated file. Do not edit.', '#include <cstdint>', '#include <new>',
                    '#include <emscripten/emscripten.h>']
        self.cpp += [f'#include "{header}"' for header in headers]
        self.cpp += ['static_assert(sizeof(uintptr_t) == 4, "These bindings require Wasm32");', 'extern "C" {']
        self.ts = ['// Generated file. Do not edit.',
                   'export interface Result<T> { readonly status: number; readonly value: T; }',
                   'export interface NativeType<T extends NativeObject> { readonly prototype: T; }',
                   'export interface NativeObject {',
                   '  readonly disposed: boolean;', '  dispose(): void;',
                   '  QueryInterface<T extends NativeObject>(type: NativeType<T>): T | null;', '}',
                   'export interface ModuleOptions {',
                   '  locateFile?: (path: string, prefix: string) => string;',
                   '  wasmBinary?: ArrayBuffer | Uint8Array;',
                   '  /** Files copied into the virtual filesystem before native initialization. Use absolute POSIX paths. */',
                   '  files?: Readonly<Record<string, Uint8Array>>;',
                   '  print?: (message: string) => void;',
                   '  printErr?: (message: string) => void;', '}']
        if self.scopes:
            self.ts += [
                'export interface ReadByteAccess {',
                '  readonly byteLength: number;', '  readonly disposed: boolean;',
                '  readByte(index: number): number;',
                '  copyTo(destination: Uint8Array, sourceOffset?: number): void;',
                '  toUint8Array(): Uint8Array;', '  dispose(): number;', '}',
                'export interface WriteByteAccess extends ReadByteAccess {',
                '  writeByte(index: number, value: number): void;',
                '  copyFrom(source: Uint8Array, destinationOffset?: number): void;', '}']

    def configure_selection(self, field_policies):
        """Apply only the JavaScript subset while preserving native declarations."""
        for name in self.native_only:
            declaration = self.types.get(name)
            if declaration and declaration["kind"] in ("enum", "external"):
                continue
            owner, _, member = name.rpartition("::")
            record = self.types.get(owner, {})
            field = next((f for f in record.get("fields", []) if f["name"] == member), None)
            if record.get("kind") != "record" or field is None or field.get("default") is None:
                raise ModelError(f"{name}: native-only selection requires an enum, external type, or a descriptor field with a native default")
            record["fields"].remove(field)
        for owner, overrides in field_policies.items():
            record = self.types.get(owner, {})
            if record.get("kind") != "record" or not isinstance(overrides, dict):
                raise ModelError(f"{owner}: field policies require a selected descriptor")
            policies = {field["name"]: field.get("policy", {}) for field in record["fields"]}
            for member, override in overrides.items():
                policy = dict(policies.get(member, {}), **override)
                if override.get("binding") == "automatic" and "reason" not in override:
                    policy.pop("reason", None)
                policies[member] = policy
            apply_values_policy(record["fields"], policies, owner, is_field=True, types=self.types, conventions=self.conventions)
        for name, arguments in self.fixed_arguments.items():
            function = self.types.get(name, {})
            if function.get("kind") != "function" or not isinstance(arguments, dict):
                raise ModelError(f"{name}: fixed arguments require a selected free function")
            parameters = {p["name"]: p for p in function["parameters"]}
            for parameter, value in arguments.items():
                p = parameters.get(parameter)
                enum = self.types.get(unalias(p["type"]).get("name"), {}) if p else {}
                namespace = enum.get("name", "").rpartition("::")[0]
                values = {namespace + "::" + v["name"] for v in enum.get("values", [])}
                if enum.get("kind") != "enum" or value not in values:
                    raise ModelError(f"{name}::{parameter}: fixed argument must name an enumerator of the parameter type")

    def validate_scope(self, name, spec):
        """Scope adapters describe API semantics explicitly, never by method name."""
        if not isinstance(spec, dict) or spec.keys() != {"release", "size", "success"}:
            raise ModelError(f"{name}: scoped bytes require only release, size, and success")
        if type(spec["success"]) is not int:
            raise ModelError(f"{name}: invalid scoped byte access mode or success status")
        owner, _, acquire = name.rpartition("::")
        declaration = self.types.get(owner, {})
        methods = self.interface_methods(owner)
        if declaration.get("kind") != "interface" or any(
                method not in methods for method in (acquire, spec["release"], spec["size"])):
            raise ModelError(f"{name}: scoped byte methods must belong to the selected interface or its bases")
        begin, end, size = (methods[m] for m in (acquire, spec["release"], spec["size"]))
        for method in (begin, end):
            status = self.types.get(unalias(method["result"]["type"]).get("name"), {})
            if (method.get("policy", {}).get("binding") != "manual" or
                    status.get("policy", {}).get("role") != "status"):
                raise ModelError(f"{name}: acquire/release must be manual methods returning a status")
            if self.scalar(method["result"]["type"]) != "i32":
                raise ModelError(f"{name}: scoped access requires a 32-bit signed status")
        if end["parameters"] or size["parameters"] or self.scalar(size["result"]["type"]) not in ("u32", "u64"):
            raise ModelError(f"{name}: release and unsigned size methods must have no arguments")
        if len(begin["parameters"]) != 1:
            raise ModelError(f"{name}: acquire must have one borrowed byte-pointer output")
        param = begin["parameters"][0]
        outer = unalias(param["type"])
        inner = unalias(outer["target"]) if outer["kind"] == "pointer" else {}
        byte = unalias(inner["target"]) if inner.get("kind") == "pointer" else {}
        policy = param.get("policy", {})
        if (byte.get("kind") != "void" or
                inner.get("const") or policy.get("direction") != "out" or
                policy.get("ownership") != "borrowed" or policy.get("nullable") is not False):
            raise ModelError(f"{name}: acquire must return a borrowed void pointer")
        if not -(2 ** 31) <= spec["success"] < 2 ** 31:
            raise ModelError(f"{name}: scoped access requires a 32-bit signed status")
        return dict(spec, writable=not byte["const"])

    def interface_methods(self, name):
        declaration = self.types.get(name, {})
        methods = {}
        for base in declaration.get("bases", []):
            methods.update(self.interface_methods(base))
        methods.update((m["name"], m) for m in declaration.get("methods", []))
        return methods

    def record_array(self, name, *, output, allocate=True):
        # Layout facts and element addressing stay in native code. Output-only
        # arrays need no native allocation or default construction.
        prefix = symbol(name) + '_Array'
        if name not in self.record_arrays:
            self.record_arrays[name] = {
                "element": self.export(prefix + 'Element', 'uintptr_t', ['uintptr_t pointer', 'uint32_t index'],
                    f'return reinterpret_cast<uintptr_t>(&reinterpret_cast<const {name}*>(pointer)[index]);'),
            }
        info = self.record_arrays[name]
        if output and "size" not in info:
            info.update(
                size=self.export(prefix + 'Size', 'uint32_t', [], f'return sizeof({name});'),
                alignment=self.export(prefix + 'Alignment', 'uint32_t', [], f'return alignof({name});'))
        if not output and allocate and "create" not in info:
            info.update(
                maxCount=self.export(prefix + 'MaxCount', 'uint32_t', [], f'return PTRDIFF_MAX / sizeof({name});'),
                create=self.export(prefix + 'Create', 'uintptr_t', ['uint32_t count'],
                    f'return reinterpret_cast<uintptr_t>(new (std::nothrow) {name}[count]{{}});'),
                destroy=self.export(prefix + 'Destroy', 'void', ['uintptr_t pointer'],
                    f'delete[] reinterpret_cast<{name}*>(pointer);'))
        return info

    def fixed_array(self, t, owner, path, *, as_input, as_output):
        t = unalias(t)
        context = owner + "::" + path
        if t["volatile"] or (as_input and t["const"]):
            raise ModelError(f"{context}: fixed arrays require non-volatile elements, writable for input")
        if not isinstance(t["count"], int) or t["count"] <= 0:
            raise ModelError(f"{context}: fixed arrays require a positive extent")
        element = unalias(t["element"])
        if element["volatile"] or (as_input and element["const"]):
            raise ModelError(f"{context}: fixed arrays require non-volatile elements, writable for input")
        spec = {"count": t["count"], "nullable": False}
        declaration = self.types.get(element.get("name"), {})
        if element["kind"] == "array":
            # The native compiler supplies the row stride; no layout arithmetic
            # or struct offsets are duplicated in JavaScript.
            element_path = path + '[0]'
            prefix = symbol(owner + "::" + path.split('[')[0])
            spec["element"] = self.export(prefix + f'_ArrayDimension{path.count("[0]")}_Element',
                'uintptr_t', ['uintptr_t pointer', 'uint32_t index'],
                f'return pointer + index * sizeof(reinterpret_cast<const {owner}*>(pointer)->{element_path});')
            spec["array"], child_ts = self.fixed_array(element, owner, element_path,
                                                     as_input=as_input, as_output=as_output)
            return spec, f'{"ReadonlyArray" if as_input else "Array"}<{child_ts}>'
        if declaration.get("kind") == "record":
            spec.update(record=short(declaration["name"]),
                        **self.record_array(declaration["name"], output=as_output, allocate=False))
        elif element["kind"] == "scalar" or declaration.get("kind") == "enum":
            spec["type"] = 'u8' if element.get("name") in ("char_s", "char_u") else self.scalar(element)
        else:
            raise ModelError(f"{context}: fixed arrays support only scalars, plain structs, and nested arrays")
        return spec, self.span_ts(spec, output=not as_input)

    def span(self, value, siblings, context, *, output=False, field=False):
        policy = value.get("policy", {})
        if field and context in self.field_count_functions:
            # A native adapter derives the element count from the complete
            # descriptor, for example a type-tagged parameter's byte size.
            policy = dict(policy, length=0)
        if "length" not in policy:
            return None
        t = unalias(value["type"])
        element = unalias(t["target"]) if t["kind"] == "pointer" else {}
        length = resolve_span_length(policy["length"], {s["name"]: s for s in siblings}, context, self.types, field=field)
        directions = {"out" if output else "in"}
        if field:
            directions.add("inout")
        lifetimes = ("call", "object", "static") if output else ("call",)
        if (not length or (t["const"] and not output) or t["volatile"] or
                not element.get("const") or element.get("volatile") or
                policy.get("direction") not in directions or policy.get("ownership") != "borrowed" or
                policy.get("lifetime") not in lifetimes):
            raise ModelError(f"{context}: {'output' if output else 'input'} spans require borrowed const elements with a supported lifetime and direction")
        length_type = self.scalar(length)
        if length_type not in ("u32", "u64"):
            raise ModelError(f"{context}: span length must be an unsigned 32-bit or 64-bit scalar")
        span = {"length": policy["length"], "lengthType": length_type, "nullable": policy["nullable"]}
        declaration = self.types.get(element.get("name"), {})
        if declaration.get("kind") == "record":
            span.update(record=short(declaration["name"]), **self.record_array(declaration["name"], output=output))
        elif element.get("kind") == "void" or element.get("name") in ("char_s", "char_u"):
            # Void/character spans are bytes, including embedded NULs.
            span["type"] = 'u8'
        elif element.get("kind") == "scalar" or declaration.get("kind") == "enum":
            span["type"] = self.scalar(element)
        elif element.get("kind") == "pointer":
            interface = self.interface_value({"type": element, "policy": dict(policy, nullable=policy.get("element_nullable", False))}, context, output=output, field=field)
            if not interface:
                raise ModelError(f"{context}: pointer arrays require interface elements")
            span["interface"] = interface
        else:
            raise ModelError(f"{context}: {'output' if output else 'input'} spans support only scalars, structs, and interfaces")
        if "element_nullable" in policy and "interface" not in span:
            raise ModelError(f"{context}: element_nullable is only valid for interface arrays")
        return span

    def output_length_query(self, query, siblings, context):
        source = next((p for p in siblings if p["name"] == query["parameter"]), None)
        source_type = unalias(source["type"]) if source else {}
        inner = unalias(source_type["target"]) if source_type.get("kind") in ("pointer", "reference") else {}
        declaration = self.types.get(inner.get("name"), {})
        if (declaration.get("kind") != "interface" or inner.get("volatile") or
                source.get("policy", {}) != {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call"}):
            raise ModelError(f"{context}: length_from requires a non-null borrowed interface input")
        method = self.interface_methods(declaration["name"]).get(query["method"])
        if not method or method["parameters"] or not method.get("const"):
            raise ModelError(f"{context}: length_from requires a const no-argument method")
        count_type = method["result"]["type"]
        expression = f'reinterpret_cast<{cpp_type(source_type["target"])}*>(source)->{method["name"]}()'
        if "field" in query:
            result = unalias(count_type)
            record = unalias(result["target"]) if result["kind"] == "reference" else {}
            if (not record.get("const") or record.get("volatile") or
                    self.types.get(record.get("name"), {}).get("kind") != "record"):
                raise ModelError(f"{context}: length_from field requires a const-reference descriptor result")
            field = next((f for f in self.record_fields(record["name"]) if f["name"] == query["field"]), None)
            if not field:
                raise ModelError(f"{context}: unknown length_from field")
            count_type = field["type"]
            expression += '.' + field["name"]
        if self.scalar(count_type) not in ("u32", "u64"):
            raise ModelError(f"{context}: length_from must produce an unsigned 32-bit or 64-bit count")
        export = self.export(symbol(context) + '_Length', 'uint64_t', ['uintptr_t source'],
                             f'return {expression};')
        return count_type, {"export": export, "parameter": source["name"]}

    def output_buffer(self, value, siblings, context):
        policy = value.get("policy", {})
        if policy.get("direction") != "out" or not {"length", "length_from"} & policy.keys():
            return None
        t = unalias(value["type"])
        element = unalias(t["target"]) if t["kind"] == "pointer" else {}
        if (not element or element["const"] or element["volatile"] or
                policy.get("ownership") != "borrowed" or policy.get("lifetime") != "call"):
            raise ModelError(f"{context}: output buffers require writable borrowed call-scoped elements")
        # Reuse element layout and capacity validation, with a writable destination.
        proxy = deepcopy(value)
        proxy["type"] = dict(t, target=dict(element, const=True))
        proxy["policy"] = dict(policy, direction="in")
        proxy["policy"].pop("written", None)
        query = None
        if "length_from" in policy:
            count_type, query = self.output_length_query(policy["length_from"], siblings, context)
            proxy["policy"].pop("length_from")
            proxy["policy"]["length"] = "count"
            siblings = [{"name": "count", "type": count_type}]
        span = self.span(proxy, siblings, context)
        if query:
            span.pop("length")
            span["lengthQuery"] = query
        span["nullable"] = False
        if "record" in span:
            span.update(self.record_array(element["name"], output=True))
        if "written" in policy:
            span["written"] = policy["written"]
        return span

    @staticmethod
    def span_ts(span, *, output=False):
        if "interface" in span:
            element = span["interface"]
            return f'{"Array" if output else "ReadonlyArray"}<{element["interface"]}' + (' | null' if element['nullable'] else '') + '>'
        if "record" in span:
            return f'Array<StructSnapshot<{span["record"]}>>' if output else f'ReadonlyArray<{span["record"]}>'
        if span["type"] == "i8" and not output:
            return "Int8Array | Uint8Array"
        return {"bool": "Uint8Array", "i8": "Int8Array", "u8": "Uint8Array",
                "i16": "Int16Array", "u16": "Uint16Array", "i32": "Int32Array", "u32": "Uint32Array",
                "i64": "BigInt64Array", "u64": "BigUint64Array", "float": "Float32Array", "double": "Float64Array"}[span["type"]]

    def scalar(self, t):
        t = unalias(t)
        if t["volatile"]:
            raise ModelError("Volatile values need an explicit adapter")
        if t["kind"] == "named":
            declaration = self.types.get(t["name"], {})
            if declaration.get("kind") == "enum":
                return self.scalar(declaration["underlying_type"])
        if t["kind"] == "scalar":
            name = t["name"]
            if name in ("bool", "float", "double"):
                return name
            return ("u" if name.startswith("u") or name == "char_u" else "i") + str(t["bits"])
        if t["kind"] == "void":
            return "void"
        raise ModelError(f"JavaScript backend: expected a scalar, got {t['kind']}")

    def string(self, value, context, *, output=False, field=False):
        t, policy = unalias(value["type"]), value.get("policy", {})
        char = unalias(t["target"]) if t["kind"] == "pointer" else {}
        if char.get("kind") != "scalar" or char.get("name") not in ("char_s", "char_u"):
            return None
        lifetimes = ("call", "object", "static") if output else ("call",)
        directions = {"out" if output else "in"}
        if field:
            directions.add("inout")
        if (t["volatile"] or char["volatile"] or not char["const"] or "length" in policy or
                policy.get("direction") not in directions or
                policy.get("ownership") != "borrowed" or policy.get("lifetime") not in lifetimes):
            raise ModelError(f"{context}: strings require borrowed const char data with a supported lifetime and direction")
        return {"nullable": policy["nullable"]}

    @staticmethod
    def automatic(value, context):
        if value.get("policy", {}).get("binding", "automatic") != "automatic":
            raise ModelError(f"{context}: an explicit JavaScript adapter is required")

    def export(self, name, result, parameters, body):
        self.cpp += [f'EMSCRIPTEN_KEEPALIVE {result} {name}({", ".join(parameters)})', '{', f'    {body}', '}']
        return "_" + name

    def record_fields(self, name):
        declaration = self.types[name]
        fields = []
        for base in declaration.get("bases", []):
            if self.types.get(base, {}).get("kind") != "record":
                raise ModelError(f"{name}: descriptor bases must be selected records")
            fields.extend(self.record_fields(base))
        fields.extend(declaration["fields"])
        if len({f["name"] for f in fields}) != len(fields):
            raise ModelError(f"{name}: inherited descriptor fields must have distinct names")
        return fields

    def interface_value(self, value, context, *, output=False, field=False):
        t, policy = unalias(value["type"]), value.get("policy", {})
        inner = unalias(t["target"]) if t["kind"] == "pointer" else {}
        if self.types.get(inner.get("name"), {}).get("kind") != "interface":
            return None
        direction = policy.get("direction")
        borrowed = policy.get("ownership") == "borrowed"
        lifetime = policy.get("lifetime")
        if (t["volatile"] or inner["volatile"] or inner["const"] or
                direction not in ({"out", "inout"} if output and field else {"out"} if output else {"in", "inout"} if field else {"in"}) or
                not ((borrowed and lifetime in (("call", "object", "static") if output else ("call",))) or
                     (output and not field and policy.get("ownership") == "owned" and lifetime == "caller"))):
            raise ModelError(f"{context}: unsupported interface ownership or lifetime")
        return {"interface": short(inner["name"]), "nullable": policy["nullable"], "borrowed": borrowed}

    def record_pointer(self, value, context, *, output=False, field=False):
        t, policy = unalias(value["type"]), value.get("policy", {})
        inner = unalias(t["target"]) if t["kind"] == "pointer" else {}
        if self.types.get(inner.get("name"), {}).get("kind") != "record":
            return None
        directions = {"out" if output else "in"}
        if field:
            directions.add("inout")
        if (t["volatile"] or inner["volatile"] or not inner["const"] or
                policy.get("direction") not in directions or policy.get("ownership") != "borrowed" or
                policy.get("lifetime") not in (("call", "object", "static") if output else ("call",)) or "length" in policy):
            raise ModelError(f"{context}: struct pointers require borrowed const data with a supported lifetime")
        return {"recordPointer": short(inner["name"]), "nullable": policy["nullable"]}

    def record_result(self, value, context):
        t, policy = unalias(value["type"]), value.get("policy", {})
        inner = unalias(t["target"]) if t["kind"] == "reference" else {}
        declaration = self.types.get(inner.get("name"), {})
        if declaration.get("kind") != "record":
            return None
        if (t["volatile"] or inner["volatile"] or not inner["const"] or
                policy.get("direction") != "out" or policy.get("nullable") is not False or
                policy.get("ownership") != "borrowed" or policy.get("lifetime") not in ("call", "object", "static") or
                "length" in policy):
            raise ModelError(f"{context}: struct results require a borrowed const reference with a supported lifetime")
        return short(declaration["name"])

    def record(self, d):
        name = d["name"]
        if d["methods"]:
            raise ModelError(f"{name}: only plain descriptors are supported")
        as_input, as_output = name in self.input_records, name in self.output_records
        rec = {"fields": {}}
        if name in self.storage_records:
            rec["create"] = self.export(symbol(name) + "_Create", "uintptr_t", [],
                                        f'return reinterpret_cast<uintptr_t>(new (std::nothrow) {name}{{}});')
            rec["destroy"] = self.export(symbol(name) + "_Destroy", "void", ['uintptr_t pointer'],
                                         f'delete reinterpret_cast<{name}*>(pointer);')
        self.ts.append(f'export interface {short(name)} {{')
        fields = self.record_fields(name)
        for field in fields:
            context = name + "::" + field["name"]
            self.automatic(field, context)
            field_type = unalias(field["type"])
            if as_input and field_type["const"]:
                raise ModelError(f"{name}: const fields need an explicit adapter")
            if field_type["kind"] == "array":
                if "length" in field.get("policy", {}):
                    raise ModelError(f"{context}: fixed arrays use their declared extent, not a length policy")
                array, field_ts = self.fixed_array(field_type, name, field["name"],
                                                  as_input=as_input, as_output=as_output)
                address = self.export(symbol(name) + '_Field_' + field["name"], 'uintptr_t',
                    ['uintptr_t pointer'],
                    f'return reinterpret_cast<uintptr_t>(&reinterpret_cast<const {name}*>(pointer)->{field["name"]});')
                rec["fields"][field["name"]] = {"array": array, "address": address}
                self.ts.append(f'  {field["name"]}{"?" if as_input else ""}: {field_ts};')
                continue
            span = None
            if as_input:
                span = self.span(field, fields, context, field=True)
            if as_output:
                span = self.span(field, fields, context, output=True, field=True)
            if span:
                count = str(span["length"]) if type(span["length"]) is int else f'reinterpret_cast<const {name}*>(pointer)->{span["length"]}'
                if context in self.field_count_functions:
                    count = f'{self.field_count_functions[context]}(*reinterpret_cast<const {name}*>(pointer))'
                span["getLength"] = self.export(symbol(name) + "_Length_" + field["name"],
                    'uint64_t', ['uintptr_t' if type(span["length"]) is int and context not in self.field_count_functions else 'uintptr_t pointer'], f'return {count};')
                spec = {"span": span}
                if as_input:
                    spec["set"] = self.export(symbol(name) + "_Set_" + field["name"], "void",
                        ['uintptr_t pointer', 'uintptr_t data'],
                        f'reinterpret_cast<{name}*>(pointer)->{field["name"]} = reinterpret_cast<{cpp_type(field["type"])}>(data);')
                if as_output:
                    spec["get"] = self.export(symbol(name) + "_Get_" + field["name"], 'uintptr_t',
                        ['uintptr_t pointer'],
                        f'return reinterpret_cast<uintptr_t>(reinterpret_cast<const {name}*>(pointer)->{field["name"]});')
                rec["fields"][field["name"]] = spec
                field_ts = self.span_ts(span, output=not as_input) + (' | null' if span['nullable'] else '')
                self.ts.append(f'  {field["name"]}{"?" if as_input else ""}: {field_ts};')
                continue
            string = None
            if as_input:
                string = self.string(field, context, field=True)
            if as_output:
                string = self.string(field, context, output=True, field=True)
            interface = None
            if as_input:
                interface = self.interface_value(field, context, field=True)
            if as_output:
                interface = self.interface_value(field, context, output=True, field=True)
            record_pointer = None
            if as_input:
                record_pointer = self.record_pointer(field, context, field=True)
            if as_output:
                record_pointer = self.record_pointer(field, context, output=True, field=True)
            spec = {}
            if string or interface or record_pointer:
                spec.update(record_pointer if record_pointer else interface if interface else {"string": string})
                field_ts = ((record_pointer['recordPointer'] + (' | null' if record_pointer['nullable'] else '')) if record_pointer else
                            (interface['interface'] + (' | null' if interface['nullable'] else '')) if interface else
                            'string' + (' | null' if string['nullable'] else ''))
                if as_input:
                    spec["set"] = self.export(symbol(name) + "_Set_" + field["name"], "void",
                        ['uintptr_t pointer', 'uintptr_t data'],
                        f'reinterpret_cast<{name}*>(pointer)->{field["name"]} = reinterpret_cast<{cpp_type(field["type"])}>(data);')
                if as_output:
                    spec["get"] = self.export(symbol(name) + "_Get_" + field["name"], cpp_type(field["type"]),
                        ['uintptr_t pointer'], f'return reinterpret_cast<const {name}*>(pointer)->{field["name"]};')
            elif field_type["kind"] == "named" and self.types.get(field_type["name"], {}).get("kind") == "record":
                if field_type["volatile"]:
                    raise ModelError("Volatile values need an explicit adapter")
                # Embedded structs keep the containing object's native layout and
                # lifetime. Input writes preserve the parent's initialized defaults.
                qualifier = 'const ' if as_output else ''
                address = self.export(symbol(name) + "_Field_" + field["name"], "uintptr_t",
                    ['uintptr_t pointer'],
                    f'return reinterpret_cast<uintptr_t>(&reinterpret_cast<{qualifier}{name}*>(pointer)->{field["name"]});')
                field_ts = short(field_type["name"])
                spec = {"record": field_ts, "address": address}
            else:
                kind = self.scalar(field["type"])
                spec["type"] = kind
                field_ts = ts_scalar(kind)
                if as_input:
                    spec["set"] = self.export(symbol(name) + "_Set_" + field["name"], "void",
                        ['uintptr_t pointer', cpp_type(field["type"]) + ' value'],
                        f'reinterpret_cast<{name}*>(pointer)->{field["name"]} = value;')
                if as_output:
                    spec["get"] = self.export(symbol(name) + "_Get_" + field["name"], cpp_type(dict(field_type, const=False)),
                        ['uintptr_t pointer'], f'return reinterpret_cast<const {name}*>(pointer)->{field["name"]};')
            rec["fields"][field["name"]] = spec
            self.ts.append(f'  {field["name"]}{"?" if as_input else ""}: {field_ts};')
        self.ts.append('}')
        self.schema["records"][short(name)] = rec

    def result_span(self, d, owner, context):
        result = d["result"]
        method_name = result.get("policy", {}).get("length_method")
        if method_name is None:
            return self.span(result, d["parameters"], context, output=True)
        method = self.interface_methods(owner).get(method_name) if owner else None
        if (not method or method["parameters"] or
                self.scalar(method["result"]["type"]) not in ("u32", "u64")):
            raise ModelError(f"{context}: length_method must name an unsigned no-argument method on this interface")
        proxy = deepcopy(result)
        proxy["policy"].pop("length_method")
        proxy["policy"]["length"] = method_name
        span = self.span(proxy, [{"name": method_name, "type": method["result"]["type"]}], context, output=True)
        span["lengthExport"] = self.export(symbol(context.removesuffix(' result')) + '_Length',
            'uint64_t', ['uintptr_t self'], f'return reinterpret_cast<{owner}*>(self)->{method_name}();')
        return span

    def callable(self, d, owner=None):
        name = owner + "::" + d["name"] if owner else d["name"]
        if name in self.scopes:
            return self.scoped_bytes(d, owner, self.scopes[name])
        self.automatic(d, name)
        self.automatic(d["result"], name + " result")
        result_span = self.result_span(d, owner, name + " result")
        result_string = self.string(d["result"], name + " result", output=True) if not result_span else None
        result_record = self.record_result(d["result"], name + " result") if not result_span else None
        result_interface = self.interface_value(d["result"], name + " result", output=True) if not result_span else None
        result_pointer = self.record_pointer(d["result"], name + " result", output=True) if not result_span else None
        result = (result_pointer if result_pointer else result_interface if result_interface else {"span": result_span} if result_span else {"record": result_record} if result_record else
                  {"string": result_string} if result_string else self.scalar(d["result"]["type"]))
        native_params, native_args, parameters, ts_params = [], [], [], []
        if owner:
            native_params.append('uintptr_t self')
        output_type = None
        output_types = {}
        default_prefixes = {}
        for p in d["parameters"]:
            pname = p["name"]
            if pname in self.fixed_arguments.get(name, {}):
                native_args.append(self.fixed_arguments[name][pname])
                continue
            self.automatic(p, name + "::" + pname)
            if p.get("default") is not None:
                default_prefixes[pname] = (len(native_params), len(native_args), len(ts_params))
            t, policy = unalias(p["type"]), p.get("policy", {})
            buffer = self.output_buffer(p, d["parameters"], name + "::" + pname)
            if buffer:
                native_params.append('uintptr_t ' + pname)
                native_args.append(f'reinterpret_cast<{cpp_type(p["type"])}>({pname})')
                parameters.append({"name": pname, "output": {"buffer": buffer}})
                output_types[pname] = self.span_ts(buffer, output=True)
                continue
            span = self.span(p, d["parameters"], name + "::" + pname)
            if span:
                native_params.append('uintptr_t ' + pname)
                native_args.append(f'reinterpret_cast<{cpp_type(p["type"])}>({pname})')
                parameters.append({"name": pname, "span": span})
                ts_params.append(f'{pname}: {self.span_ts(span)}' + (' | null' if span['nullable'] else ''))
                continue
            string = self.string(p, name + "::" + pname)
            if string:
                native_params.append('uintptr_t ' + pname)
                native_args.append(f'reinterpret_cast<{cpp_type(p["type"])}>({pname})')
                parameters.append({"name": pname, "string": string})
                ts_params.append(f'{pname}: string' + (' | null' if string['nullable'] else ''))
                continue
            if t["kind"] in ("pointer", "reference"):
                inner = unalias(t["target"])
                declaration = self.types.get(inner.get("name"), {})
                native_params.append('uintptr_t ' + pname)
                native_args.append(('*' if t["kind"] == "reference" else '') +
                                   f'reinterpret_cast<{cpp_type(t["target"])}*>({pname})')
                record_pointer = self.record_pointer(p, name + "::" + pname) if policy.get("direction") == "in" else None
                if record_pointer:
                    parameters.append({"name": pname, **record_pointer})
                    ts_params.append(f'{pname}: {record_pointer["recordPointer"]}' + (' | null' if record_pointer['nullable'] else ''))
                elif (t["kind"] == "reference" and inner["const"] and declaration.get("kind") == "record"
                        and policy == {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call"}):
                    parameters.append({"name": pname, "record": short(inner["name"])})
                    ts_params.append(f'{pname}: {short(inner["name"])}')
                elif (declaration.get("kind") == "interface"
                        and policy.get("direction") == "in" and policy.get("ownership") == "borrowed"
                        and policy.get("lifetime") == "call"
                        and (t["kind"] == "pointer" or policy.get("nullable") is False)):
                    # References use the same borrowed wrapper as pointers, but
                    # must be non-null before the native bridge dereferences them.
                    parameters.append({"name": pname, "interface": short(inner["name"]), "nullable": policy["nullable"]})
                    ts_params.append(f'{pname}: {short(inner["name"])}' + (' | null' if policy['nullable'] else ''))
                elif (policy.get("direction") == "out"
                        and policy.get("nullable") is False):
                    target = unalias(inner["target"]) if inner["kind"] == "pointer" else {}
                    string = self.string({"type": inner, "policy": policy}, name + "::" + pname, output=True)
                    if string and not inner["const"]:
                        # nullable describes the caller-provided output slot. Its
                        # string value may be null, including on failure.
                        output_type = 'string | null'
                        parameters.append({"name": pname, "output": {"string": {"nullable": True}}})
                    elif (target.get("kind") == "named" and self.types.get(target["name"], {}).get("kind") == "interface"
                            and not inner["const"]):
                        interface = self.interface_value({"type": inner, "policy": dict(policy, nullable=True)}, name + "::" + pname, output=True)
                        output_type = short(target["name"]) + ' | null'
                        parameters.append({"name": pname, "output": interface})
                    elif (declaration.get("kind") == "record" and not inner["const"] and not inner["volatile"]
                            and policy.get("ownership") == "borrowed" and policy.get("lifetime") == "call"):
                        output_type = f'StructSnapshot<{short(inner["name"])}>'
                        parameters.append({"name": pname, "output": {"record": short(inner["name"])}})
                    elif policy.get("ownership") == "borrowed" and policy.get("lifetime") == "call":
                        kind = self.scalar(inner)
                        if kind == "void" or inner["const"]:
                            raise ModelError(f"{name}::{pname}: output must point to a writable scalar")
                        output_type = ts_scalar(kind)
                        parameters.append({"name": pname, "output": {"type": kind}})
                    else:
                        raise ModelError(f"{name}::{pname}: unsupported output ownership")
                else:
                    raise ModelError(f"{name}::{pname}: unsupported pointer/reference marshalling")
                if parameters[-1].get("output"):
                    output_types[pname] = output_type
            else:
                kind = self.scalar(t)
                native_params.append(cpp_type(p["type"]) + ' ' + pname)
                native_args.append(pname)
                parameters.append({"name": pname, "type": kind})
                ts_params.append(f'{pname}: {ts_scalar(kind)}')
        output_type = (next(iter(output_types.values())) if len(output_types) == 1 else
                       '{ ' + '; '.join(f'{n}: {t}' for n, t in output_types.items()) + ' }' if output_types else None)
        if output_type:
            result_decl = self.types.get(unalias(d["result"]["type"]).get("name"), {})
            if result != "void" and result_decl.get("policy", {}).get("role") != "status":
                raise ModelError(f"{name}: output callables require a status or void result")
        target = f'reinterpret_cast<{owner}*>(self)->{d["name"]}' if owner else name
        call = target + '(' + ', '.join(native_args) + ')'
        if result_pointer:
            body = f'return reinterpret_cast<uintptr_t>({call});'
            native_result_type = 'uintptr_t'
            result_type = f'StructSnapshot<{result_pointer["recordPointer"]}>' + (' | null' if result_pointer['nullable'] else '')
        elif result_interface:
            body = f'return reinterpret_cast<uintptr_t>({call});'
            native_result_type = 'uintptr_t'
            result_type = result_interface['interface'] + (' | null' if result_interface['nullable'] else '')
        elif result_span:
            body = f'return reinterpret_cast<uintptr_t>({call});'
            native_result_type = 'uintptr_t'
            result_type = self.span_ts(result_span, output=True) + (' | null' if result_span['nullable'] else '')
        elif result_record:
            # Borrow only until the synchronous JS snapshot is complete; neither
            # an allocation nor a native copy is needed for a const-reference result.
            body = f'return reinterpret_cast<uintptr_t>(&{call});'
            native_result_type = 'uintptr_t'
            result_type = f'StructSnapshot<{result_record}>'
        else:
            body = ('' if result == "void" else 'return ') + call + ';'
            native_result_type = cpp_type(d["result"]["type"])
            result_type = ('string' + (' | null' if result_string['nullable'] else '')) if result_string else ts_scalar(result)
        export = self.export(symbol(name), native_result_type, native_params, body)
        defaults = {}
        # Let the native call site evaluate omitted defaults. Count arguments
        # still needed to validate JS arrays must remain explicit. Likewise, do
        # not let a borrowed result outlive a default-constructed input temporary.
        lengths = {p.get("policy", {}).get("length") for p in (*d["parameters"], d["result"])}
        lengths.update(p["policy"]["length_from"]["parameter"] for p in d["parameters"]
                       if "length_from" in p.get("policy", {}))
        for p in reversed(d["parameters"]):
            pname = p["name"]
            parameter = next((item for item in parameters if item["name"] == pname), None)
            if pname not in default_prefixes or not parameter or "output" in parameter or pname in lengths:
                break
            if unalias(p["type"])["kind"] in ("pointer", "reference") and (isinstance(result, dict) or output_types):
                break
            param_count, native_count, input_count = default_prefixes[pname]
            default_call = target + '(' + ', '.join(native_args[:native_count]) + ')'
            defaults[str(input_count)] = self.export(symbol(name) + f'_Args{input_count}', native_result_type,
                native_params[:param_count], body.replace(call, default_call))
            parameter["default"] = True
            ts_params[input_count] = ts_params[input_count].replace(pname + ':', pname + '?:', 1)
        ts_return = (output_type if result == 'void' else f'Result<{output_type}>') if output_type else result_type
        return {"export": export, "parameters": parameters, "result": result, "defaults": defaults,
                "ts": f'{short(d["name"])}({", ".join(ts_params)}): {ts_return};'}

    def scoped_bytes(self, d, owner, spec):
        name = owner + "::" + d["name"]
        acquire = self.export(symbol(name), cpp_type(d["result"]["type"]),
            ['uintptr_t self', 'uintptr_t output'],
            f'return reinterpret_cast<{owner}*>(self)->{d["name"]}('
            f'reinterpret_cast<{cpp_type(d["parameters"][0]["type"])}>(output));')
        release = self.export(symbol(name) + '_Release', 'int32_t', ['uintptr_t self'],
            f'return reinterpret_cast<{owner}*>(self)->{spec["release"]}();')
        size = self.export(symbol(name) + '_Size', 'uint64_t', ['uintptr_t self'],
            f'return reinterpret_cast<{owner}*>(self)->{spec["size"]}();')
        return {"scope": {"acquire": acquire, "release": release, "size": size,
                          "writable": spec["writable"], "success": spec["success"]},
                "ts": f'{d["name"]}(): Result<{"WriteByteAccess" if spec["writable"] else "ReadByteAccess"} | null>;'}

    def alias_type(self, t):
        t = unalias(t)
        declaration = self.types.get(t.get("name"), {})
        if declaration.get("kind") == "record":
            return short(t["name"])
        if declaration.get("kind") == "enum":
            name = short(t["name"])
            return f'typeof {name}[keyof typeof {name}]'
        if t["kind"] == "pointer":
            inner = unalias(t["target"])
            if inner.get("name") in ("char_s", "char_u") and inner["const"]:
                return 'string'
            if self.types.get(inner.get("name"), {}).get("kind") in ("interface", "record"):
                return short(inner["name"])
        if t["kind"] == "array":
            return f'ReadonlyArray<{self.alias_type(t["element"])}>'
        return ts_scalar(self.scalar(t))

    def constant_value(self, t, value, *, field=False):
        """Emit evaluated values without passing exact integers through JS JSON."""
        t = unalias(t)
        if value is None:
            return 'null', 'null'
        if isinstance(value, str):
            return json.dumps(value), 'string'
        if t["kind"] == "array":
            elements = [self.constant_value(t["element"], element, field=field) for element in value]
            element_type = unalias(t["element"])
            if field and (element_type["kind"] == "scalar" or
                          self.types.get(element_type.get("name"), {}).get("kind") == "enum"):
                array_type = self.span_ts({"type": self.scalar(t["element"])}, output=True)
                return f'new {array_type}([' + ', '.join(v for v, _ in elements) + '])', array_type
            return ('Object.freeze([' + ', '.join(v for v, _ in elements) + '])',
                    'readonly [' + ', '.join(ts for _, ts in elements) + ']')
        if self.types.get(t.get("name"), {}).get("kind") == "record":
            fields = {f["name"]: f["type"] for f in self.record_fields(t["name"])}
            elements = [(name, *self.constant_value(fields[name], value, field=True)) for name, value in value.items()]
            # Typed arrays cannot be frozen. Return a fresh array from each field
            # getter so constants remain unchanged and can be passed as descriptors.
            members = [f'get [{json.dumps(n)}]() {{ return {v}; }}' if unalias(fields[n])["kind"] == "array"
                       else f'[{json.dumps(n)}]: {v}' for n, v, _ in elements]
            return ('Object.freeze({' + ', '.join(members) + '})',
                    '{ ' + '; '.join(f'readonly {n}: {ts}' for n, _, ts in elements) + ' }')
        kind = self.scalar(t)
        return (str(value) + 'n' if kind in ('i64', 'u64') else json.dumps(value)), ts_scalar(kind)

    def generate(self):
        self.input_records, self.output_records, self.storage_records = record_usage(self.types)
        if self.output_records:
            self.ts += ['export type StructSnapshot<T> = T extends ArrayBufferView | NativeObject ? T :',
                        '  T extends ReadonlyArray<infer E> ? Array<StructSnapshot<E>> : T extends object ?',
                        '  { [K in keyof T]-?: StructSnapshot<Exclude<T[K], undefined>> } : T;']
        used_names = {"NativeObject", "NativeType", "Result", "ModuleOptions", "Bindings", "ReadByteAccess", "WriteByteAccess", "StructSnapshot"}
        for name in self.types:
            public_name = short(name)
            if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", public_name) or public_name in used_names:
                raise ModelError(f"{name}: invalid or conflicting JavaScript name")
            used_names.add(public_name)
        if any(d["kind"] == "interface" for d in self.types.values()):
            self.export('binding_Release', 'void', ['uintptr_t pointer'], 'reinterpret_cast<Diligent::IObject*>(pointer)->Release();')
        if any(d["kind"] == "interface" for d in self.types.values()):
            self.export('binding_AddRef', 'void', ['uintptr_t pointer'], 'reinterpret_cast<Diligent::IObject*>(pointer)->AddRef();')
        for name, d in self.types.items():
            if name in self.native_only:
                continue
            kind = d["kind"]
            if kind == "external":
                if name != "Diligent::IObject":
                    raise ModelError(f"{name}: no JavaScript runtime implementation")
                continue
            self.automatic(d, name)
            if kind == "enum":
                if self.scalar(d["underlying_type"]) in ("i64", "u64"):
                    raise ModelError(f"{name}: 64-bit enums need an explicit adapter")
                self.schema["enums"][short(name)] = {v["name"]: v["value"] for v in d["values"]}
                self.ts.append(f'export const {short(name)}: {{')
                self.ts += [f'  readonly {v["name"]}: {v["value"]};' for v in d["values"]]
                self.ts.append('};')
            elif kind == "constant":
                value, ts_type = self.constant_value(d["type"], d["value"])
                self.constants[short(name)] = value
                self.ts.append(f'export const {short(name)}: {ts_type};')
            elif kind == "alias":
                self.ts.append(f'export type {short(name)} = {self.alias_type(d["type"])};')
            elif kind == "record":
                self.record(d)
            elif kind == "interface":
                if len(d["bases"]) != 1 or not d.get("policy", {}).get("iid") or d["fields"]:
                    raise ModelError(f"{name}: interfaces need one base, an IID, and no data fields")
                if d.get("policy", {}).get("ownership") != "reference_counted":
                    raise ModelError(f"{name}: interfaces must be reference counted")
                if any(m["name"] in ("dispose", "disposed", "QueryInterface") for m in d["methods"]):
                    raise ModelError(f"{name}: method conflicts with the runtime")
                query = self.export(symbol(name) + '_Query', 'uintptr_t', ['uintptr_t pointer'],
                    'Diligent::IObject* result = nullptr;\n    '
                    f'reinterpret_cast<Diligent::IObject*>(pointer)->QueryInterface({d["policy"]["iid"]}, &result);\n    '
                    f'return reinterpret_cast<uintptr_t>(static_cast<{name}*>(result));')
                methods = {m["name"]: self.callable(m, name) for m in d["methods"]
                           if name + "::" + m["name"] not in self.releases}
                base = short(d["bases"][0])
                self.schema["interfaces"][short(name)] = {"base": base, "query": query, "methods": methods}
                self.ts.append(f'export interface {short(name)} extends {"NativeObject" if base == "IObject" else base} {{')
                self.ts += ['  ' + m['ts'] for m in methods.values()]
                self.ts.append('}')
            elif kind == "function":
                self.schema["functions"][short(name)] = self.callable(d)
            else:
                raise ModelError(f"{name}: JavaScript generation does not support {kind} declarations yet")
        self.cpp.append('}')
        self.ts.append('export interface Bindings {')
        self.ts += [f'  readonly {name}: typeof {name};' for name in (*self.schema['enums'], *self.constants)]
        self.ts += [f'  readonly {name}: NativeType<{name}>;' for name in self.schema['interfaces']]
        self.ts += ['  ' + f['ts'] for f in self.schema['functions'].values()]
        self.ts += ['}', 'export default function createBindings(options?: ModuleOptions): Promise<Bindings>;']
        # Type declarations are an emitter detail, not runtime metadata.
        for interface in self.schema['interfaces'].values():
            for method in interface['methods'].values():
                method.pop('ts')
        for function in self.schema['functions'].values():
            function.pop('ts')
        js = '// Generated file. Do not edit.\n'
        js += f'import createWasm from "./{self.module}Wasm.mjs";\n'
        js += 'import { createBindings, prepareModuleOptions } from "./runtime.mjs";\n'
        for name, values in self.schema['enums'].items():
            js += f'export const {name} = Object.freeze({json.dumps(values)});\n'
        for name, value in self.constants.items():
            js += f'export const {name} = {value};\n'
        js += 'const constants = {' + ', '.join(self.constants) + '};\n'
        js += 'const schema = ' + json.dumps(self.schema, indent=2) + ';\n'
        js += 'export default async function(options = {}) {\n'
        js += '  return createBindings(await createWasm(prepareModuleOptions(options)), schema, constants);\n}\n'
        return '\n'.join(self.cpp) + '\n', js, '\n'.join(self.ts) + '\n'


def write(path, content):
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--header", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--adapters", type=Path, help="JavaScript-specific scoped byte adapters")
    args = parser.parse_args()
    try:
        model = json.loads(args.model.read_text(encoding="utf-8"))
        adapters = json.loads(args.adapters.read_text(encoding="utf-8")) if args.adapters else None
        cpp, js, declarations = Generator(model, args.module, args.header, adapters).generate()
        args.output.mkdir(parents=True, exist_ok=True)
        write(args.output / (args.module + '.cpp'), cpp)
        write(args.output / (args.module + '.mjs'), js)
        write(args.output / (args.module + '.d.mts'), declarations)
        write(args.output / 'runtime.mjs', Path(__file__).with_name('runtime.mjs').read_text(encoding="utf-8"))
        write(args.output / 'byte_access.mjs', Path(__file__).with_name('byte_access.mjs').read_text(encoding="utf-8"))
    except (ModelError, OSError, ValueError) as error:
        parser.exit(1, f"JavaScript binding generation failed: {error}\n")
    print(f"Generated {args.module} JavaScript bindings")


if __name__ == "__main__":
    main()
