#!/usr/bin/env python3
"""Generate Wasm32 call glue, JavaScript wrappers, and TypeScript declarations."""

import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from binding_model import ModelError, apply_values_policy, unalias


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
        # Backend selection and policies must not modify the language-neutral model.
        self.types = {d["name"]: deepcopy(d) for d in model["declarations"]}
        if adapters and (not isinstance(adapters, dict) or adapters.get("schema_version") != 1 or
                         adapters.keys() - {"schema_version", "scoped_bytes", "native_only", "field_policies", "fixed_arguments"}):
            raise ModelError("Invalid JavaScript adapter configuration")
        self.native_only = set((adapters or {}).get("native_only", []))
        self.fixed_arguments = (adapters or {}).get("fixed_arguments", {})
        self.configure_selection((adapters or {}).get("field_policies", {}))
        self.scopes = (adapters or {}).get("scoped_bytes", {})
        if not isinstance(self.scopes, dict):
            raise ModelError("scoped_bytes must map qualified method names to adapters")
        self.releases = set()
        for name, scope in self.scopes.items():
            self.validate_scope(name, scope)
            self.releases.add(name.rsplit("::", 1)[0] + "::" + scope["release"])
        self.record_arrays = {}
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
            if declaration and declaration["kind"] == "enum":
                continue
            owner, _, member = name.rpartition("::")
            record = self.types.get(owner, {})
            field = next((f for f in record.get("fields", []) if f["name"] == member), None)
            if record.get("kind") != "record" or field is None or field.get("default") is None:
                raise ModelError(f"{name}: native-only selection requires an enum or a descriptor field with a native default")
            record["fields"].remove(field)
        for owner, overrides in field_policies.items():
            record = self.types.get(owner, {})
            if record.get("kind") != "record" or not isinstance(overrides, dict):
                raise ModelError(f"{owner}: field policies require a selected descriptor")
            policies = {field["name"]: field.get("policy", {}) for field in record["fields"]}
            policies.update(overrides)
            apply_values_policy(record["fields"], policies, owner, is_field=True)
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
        if not isinstance(spec, dict) or spec.keys() != {"release", "size", "writable", "success"}:
            raise ModelError(f"{name}: scoped bytes require release, size, writable, and success")
        if not isinstance(spec["writable"], bool) or type(spec["success"]) is not int:
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
        if (byte.get("kind") != "void" or byte.get("const") == spec["writable"] or
                inner.get("const") or policy.get("direction") != "out" or
                policy.get("ownership") != "borrowed" or policy.get("nullable") is not False):
            raise ModelError(f"{name}: acquire must return a borrowed void pointer with matching constness")
        if not -(2 ** 31) <= spec["success"] < 2 ** 31:
            raise ModelError(f"{name}: scoped access requires a 32-bit signed status")

    def interface_methods(self, name):
        declaration = self.types.get(name, {})
        methods = {}
        for base in declaration.get("bases", []):
            methods.update(self.interface_methods(base))
        methods.update((m["name"], m) for m in declaration.get("methods", []))
        return methods

    def record_array(self, name, *, output):
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
        if not output and "create" not in info:
            info.update(
                maxCount=self.export(prefix + 'MaxCount', 'uint32_t', [], f'return PTRDIFF_MAX / sizeof({name});'),
                create=self.export(prefix + 'Create', 'uintptr_t', ['uint32_t count'],
                    f'return reinterpret_cast<uintptr_t>(new (std::nothrow) {name}[count]{{}});'),
                destroy=self.export(prefix + 'Destroy', 'void', ['uintptr_t pointer'],
                    f'delete[] reinterpret_cast<{name}*>(pointer);'))
        return info

    def span(self, value, siblings, context, *, output=False, field=False):
        policy = value.get("policy", {})
        if "length" not in policy:
            return None
        t = unalias(value["type"])
        element = unalias(t["target"]) if t["kind"] == "pointer" else {}
        length = next((s for s in siblings if s["name"] == policy["length"]), None)
        directions = {"out" if output else "in"}
        if field:
            directions.add("inout")
        lifetimes = ("call", "object", "static") if output else ("call",)
        if (not length or (t["const"] and not output) or t["volatile"] or
                not element.get("const") or element.get("volatile") or
                policy.get("direction") not in directions or policy.get("ownership") != "borrowed" or
                policy.get("lifetime") not in lifetimes):
            raise ModelError(f"{context}: {'output' if output else 'input'} spans require borrowed const elements with a supported lifetime and direction")
        length_type = self.scalar(length["type"])
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
        else:
            raise ModelError(f"{context}: {'output' if output else 'input'} spans support only scalars and plain structs")
        return span

    @staticmethod
    def span_ts(span, *, output=False):
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

    def record_usage(self):
        """Find input and returned records, including their embedded value structs."""
        inputs, outputs = set(), set()

        def include(t, used):
            t = unalias(t)
            if t["kind"] in ("reference", "pointer"):
                t = unalias(t["target"])
            d = self.types.get(t.get("name"), {})
            if d.get("kind") != "record" or d["name"] in used:
                return
            used.add(d["name"])
            for field in d["fields"]:
                include(field["type"], used)

        for d in self.types.values():
            callables = [d] if d["kind"] == "function" else d.get("methods", [])
            for function in callables:
                include(function["result"]["type"], outputs)
                for parameter in function["parameters"]:
                    include(parameter["type"], inputs)
        # Preserve input descriptor generation for explicitly selected records
        # that are not used as results (including standalone descriptor models).
        for name, d in self.types.items():
            if d["kind"] == "record" and name not in outputs:
                include({"kind": "named", "name": name, "const": False, "volatile": False}, inputs)
        return inputs, outputs

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
        if d["bases"] or d["methods"]:
            raise ModelError(f"{name}: only plain descriptors are supported")
        as_input, as_output = name in self.input_records, name in self.output_records
        rec = {"fields": {}}
        if as_input:
            rec["create"] = self.export(symbol(name) + "_Create", "uintptr_t", [],
                                        f'return reinterpret_cast<uintptr_t>(new (std::nothrow) {name}{{}});')
            rec["destroy"] = self.export(symbol(name) + "_Destroy", "void", ['uintptr_t pointer'],
                                         f'delete reinterpret_cast<{name}*>(pointer);')
        self.ts.append(f'export interface {short(name)} {{')
        for field in d["fields"]:
            context = name + "::" + field["name"]
            self.automatic(field, context)
            field_type = unalias(field["type"])
            if as_input and field_type["const"]:
                raise ModelError(f"{name}: const fields need an explicit adapter")
            span = None
            if as_input:
                span = self.span(field, d["fields"], context, field=True)
            if as_output:
                span = self.span(field, d["fields"], context, output=True, field=True)
            if span:
                span["getLength"] = self.export(symbol(name) + "_Length_" + field["name"],
                    'uint64_t', ['uintptr_t pointer'],
                    f'return reinterpret_cast<const {name}*>(pointer)->{span["length"]};')
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
            spec = {}
            if string:
                spec["string"] = string
                field_ts = 'string' + (' | null' if string['nullable'] else '')
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

    def callable(self, d, owner=None):
        name = owner + "::" + d["name"] if owner else d["name"]
        if name in self.scopes:
            return self.scoped_bytes(d, owner, self.scopes[name])
        self.automatic(d, name)
        self.automatic(d["result"], name + " result")
        result_span = self.span(d["result"], d["parameters"], name + " result", output=True)
        result_string = self.string(d["result"], name + " result", output=True) if not result_span else None
        result_record = self.record_result(d["result"], name + " result") if not result_span else None
        result = ({"span": result_span} if result_span else {"record": result_record} if result_record else
                  {"string": result_string} if result_string else self.scalar(d["result"]["type"]))
        native_params, native_args, parameters, ts_params = [], [], [], []
        if owner:
            native_params.append('uintptr_t self')
        output_type = None
        for p in d["parameters"]:
            pname = p["name"]
            if pname in self.fixed_arguments.get(name, {}):
                native_args.append(self.fixed_arguments[name][pname])
                continue
            self.automatic(p, name + "::" + pname)
            if p.get("default") is not None:
                raise ModelError(f"{name}::{pname}: default arguments need an explicit adapter")
            t, policy = unalias(p["type"]), p.get("policy", {})
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
                if (t["kind"] == "reference" and inner["const"] and declaration.get("kind") == "record"
                        and policy == {"direction": "in", "nullable": False, "ownership": "borrowed", "lifetime": "call"}):
                    parameters.append({"name": pname, "record": short(inner["name"])})
                    ts_params.append(f'{pname}: {short(inner["name"])}')
                elif (t["kind"] == "pointer" and declaration.get("kind") == "interface"
                        and policy.get("direction") == "in" and policy.get("ownership") == "borrowed"
                        and policy.get("lifetime") == "call"):
                    parameters.append({"name": pname, "interface": short(inner["name"]), "nullable": policy["nullable"]})
                    ts_params.append(f'{pname}: {short(inner["name"])}' + (' | null' if policy['nullable'] else ''))
                elif (policy.get("direction") == "out"
                        and policy.get("nullable") is False):
                    if output_type is not None:
                        raise ModelError(f"{name}: multiple outputs need an explicit adapter")
                    target = unalias(inner["target"]) if inner["kind"] == "pointer" else {}
                    string = self.string({"type": inner, "policy": policy}, name + "::" + pname, output=True)
                    if string and not inner["const"]:
                        # nullable describes the caller-provided output slot. Its
                        # string value may be null, including on failure.
                        output_type = 'string | null'
                        parameters.append({"name": pname, "output": {"string": {"nullable": True}}})
                    elif t["kind"] != "pointer":
                        raise ModelError(f"{name}::{pname}: unsupported output reference")
                    elif (target.get("kind") == "named" and self.types.get(target["name"], {}).get("kind") == "interface"
                            and policy.get("ownership") == "owned" and policy.get("lifetime") == "caller"):
                        output_type = short(target["name"]) + ' | null'
                        parameters.append({"name": pname, "output": {"interface": short(target["name"])}})
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
            else:
                kind = self.scalar(t)
                native_params.append(cpp_type(p["type"]) + ' ' + pname)
                native_args.append(pname)
                parameters.append({"name": pname, "type": kind})
                ts_params.append(f'{pname}: {ts_scalar(kind)}')
        if output_type:
            result_decl = self.types.get(unalias(d["result"]["type"]).get("name"), {})
            if result_decl.get("policy", {}).get("role") != "status":
                raise ModelError(f"{name}: output callables require a status result")
        target = f'reinterpret_cast<{owner}*>(self)->{d["name"]}' if owner else name
        call = target + '(' + ', '.join(native_args) + ')'
        if result_span:
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
        ts_return = f'Result<{output_type}>' if output_type else result_type
        return {"export": export, "parameters": parameters, "result": result,
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

    def generate(self):
        self.input_records, self.output_records = self.record_usage()
        if self.output_records:
            self.ts += ['export type StructSnapshot<T> = T extends ArrayBufferView ? T :',
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
        if self.scopes:
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
        self.ts += [f'  readonly {name}: typeof {name};' for name in self.schema['enums']]
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
        js += 'import { createBindings } from "./runtime.mjs";\n'
        for name, values in self.schema['enums'].items():
            js += f'export const {name} = Object.freeze({json.dumps(values)});\n'
        js += 'const schema = ' + json.dumps(self.schema, indent=2) + ';\n'
        js += 'export default async function(options = {}) {\n'
        js += '  return createBindings(await createWasm(options), schema);\n}\n'
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
