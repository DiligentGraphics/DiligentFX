#!/usr/bin/env python3
"""Generate Wasm32 call glue, JavaScript wrappers, and TypeScript declarations."""

import argparse
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from binding_model import ModelError, unalias


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
        return "void"
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

    def __init__(self, model, module, headers):
        if model.get("schema_version") != 1 or not model.get("target", "").startswith("wasm32-"):
            raise ModelError("JavaScript generation requires a validated Wasm32 model")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", module) or not headers:
            raise ModelError("A module identifier and at least one entry header are required")
        self.module = module
        self.types = {d["name"]: d for d in model["declarations"]}
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

    @staticmethod
    def automatic(value, context):
        if value.get("policy", {}).get("binding", "automatic") != "automatic":
            raise ModelError(f"{context}: an explicit JavaScript adapter is required")

    def export(self, name, result, parameters, body):
        self.cpp += [f'EMSCRIPTEN_KEEPALIVE {result} {name}({", ".join(parameters)})', '{', f'    {body}', '}']
        return "_" + name

    def record(self, d):
        name = d["name"]
        if d["bases"] or d["methods"]:
            raise ModelError(f"{name}: only plain input descriptors are supported")
        rec = {"fields": {}}
        rec["create"] = self.export(symbol(name) + "_Create", "uintptr_t", [],
                                    f'return reinterpret_cast<uintptr_t>(new (std::nothrow) {name}{{}});')
        rec["destroy"] = self.export(symbol(name) + "_Destroy", "void", ['uintptr_t pointer'],
                                     f'delete reinterpret_cast<{name}*>(pointer);')
        self.ts.append(f'export interface {short(name)} {{')
        for field in d["fields"]:
            self.automatic(field, name + "::" + field["name"])
            kind = self.scalar(field["type"])
            if unalias(field["type"])["const"]:
                raise ModelError(f"{name}: const fields need an explicit adapter")
            setter = self.export(symbol(name) + "_Set_" + field["name"], "void",
                                 ['uintptr_t pointer', cpp_type(field["type"]) + ' value'],
                                 f'reinterpret_cast<{name}*>(pointer)->{field["name"]} = value;')
            rec["fields"][field["name"]] = {"type": kind, "set": setter}
            self.ts.append(f'  {field["name"]}?: {ts_scalar(kind)};')
        self.ts.append('}')
        self.schema["records"][short(name)] = rec

    def callable(self, d, owner=None):
        name = owner + "::" + d["name"] if owner else d["name"]
        self.automatic(d, name)
        self.automatic(d["result"], name + " result")
        result = self.scalar(d["result"]["type"])
        native_params, native_args, parameters, ts_params = [], [], [], []
        if owner:
            native_params.append('uintptr_t self')
        output_type = None
        for p in d["parameters"]:
            pname = p["name"]
            self.automatic(p, name + "::" + pname)
            if p.get("default") is not None:
                raise ModelError(f"{name}::{pname}: default arguments need an explicit adapter")
            t, policy = unalias(p["type"]), p.get("policy", {})
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
                elif (t["kind"] == "pointer" and policy.get("direction") == "out"
                        and policy.get("nullable") is False):
                    if output_type is not None:
                        raise ModelError(f"{name}: multiple outputs need an explicit adapter")
                    target = unalias(inner["target"]) if inner["kind"] == "pointer" else {}
                    if (target.get("kind") == "named" and self.types.get(target["name"], {}).get("kind") == "interface"
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
        body = ('' if result == "void" else 'return ') + target + '(' + ', '.join(native_args) + ');'
        export = self.export(symbol(name), cpp_type(d["result"]["type"]), native_params, body)
        ts_return = f'Result<{output_type}>' if output_type else ts_scalar(result)
        return {"export": export, "parameters": parameters, "result": result,
                "ts": f'{short(d["name"])}({", ".join(ts_params)}): {ts_return};'}

    def generate(self):
        used_names = {"NativeObject", "NativeType", "Result", "ModuleOptions", "Bindings"}
        for name in self.types:
            public_name = short(name)
            if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", public_name) or public_name in used_names:
                raise ModelError(f"{name}: invalid or conflicting JavaScript name")
            used_names.add(public_name)
        if any(d["kind"] == "interface" for d in self.types.values()):
            self.export('binding_Release', 'void', ['uintptr_t pointer'], 'reinterpret_cast<Diligent::IObject*>(pointer)->Release();')
        for name, d in self.types.items():
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
                methods = {m["name"]: self.callable(m, name) for m in d["methods"]}
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
    args = parser.parse_args()
    try:
        model = json.loads(args.model.read_text(encoding="utf-8"))
        cpp, js, declarations = Generator(model, args.module, args.header).generate()
        args.output.mkdir(parents=True, exist_ok=True)
        write(args.output / (args.module + '.cpp'), cpp)
        write(args.output / (args.module + '.mjs'), js)
        write(args.output / (args.module + '.d.mts'), declarations)
        write(args.output / 'runtime.mjs', Path(__file__).with_name('runtime.mjs').read_text(encoding="utf-8"))
    except (ModelError, OSError, ValueError) as error:
        parser.exit(1, f"JavaScript binding generation failed: {error}\n")
    print(f"Generated {args.module} JavaScript bindings")


if __name__ == "__main__":
    main()
