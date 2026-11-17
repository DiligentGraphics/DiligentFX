#!/usr/bin/env python3
"""Generate Java/JNI bindings from the shared binding model."""

import argparse
import json
import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from binding_api import BindingAPI, cpp_type
from binding_model import ModelError, unalias
from configuration import load_config


# Java has no unsigned primitives. Widen through Uint32; preserve Uint64 bits.
SCALARS = {
    "bool": ("boolean", "jboolean", "Z", "Boolean"),
    "i8": ("byte", "jbyte", "B", "Byte"), "u8": ("short", "jshort", "S", "Short"),
    "i16": ("short", "jshort", "S", "Short"), "u16": ("int", "jint", "I", "Int"),
    "i32": ("int", "jint", "I", "Int"), "u32": ("long", "jlong", "J", "Long"),
    "i64": ("long", "jlong", "J", "Long"), "u64": ("long", "jlong", "J", "Long"),
    "float": ("float", "jfloat", "F", "Float"), "double": ("double", "jdouble", "D", "Double"),
    "void": ("void", "void", "V", "Void"),
}
KEYWORDS = set("abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for goto if implements import instanceof int interface long native new package private protected public return short static strictfp super switch synchronized this throw throws transient try void volatile while true false null record sealed permits var yield".split())


def identifier(name):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name) or name in KEYWORDS:
        raise ModelError(f"Java: unsupported identifier {name!r}")
    return name


def jni_name(name):
    return name.replace("_", "_1").replace(".", "_").replace("$", "_00024")


def mutable_type(t):
    return cpp_type(dict(t, const=False))


class Generator(BindingAPI):
    def __init__(self, model, module, package, library, headers, adapters=None):
        super().__init__(model, adapters)
        self.module = identifier(module)
        if module == "java":
            raise ModelError("Java: reserved module name java")
        self.package = ".".join(identifier(part) for part in package.split("."))
        self.library = library
        self.headers = headers
        self.path = self.package.replace(".", "/") + "/" + module
        self.state_signature = "L" + self.path + "$State;"
        self.names = {}
        self.java = []
        self.cpp = []
        self.native = []
        self.counter = 0
        if self.scopes or self.field_count_functions:
            raise ModelError("Java: scoped bytes and computed field sizes are not implemented yet")
        for name, declaration in self.types.items():
            if name in self.native_only or declaration["kind"] in ("external", "typedef"):
                continue
            short = identifier(name.rsplit("::", 1)[-1])
            if short in self.names or short in {module, "State", "NativeObject", "CLEANER", "java"}:
                raise ModelError(f"Java: ambiguous declaration name {short}")
            self.names[short] = name
        self.records = [d for d in self.types.values() if d["kind"] == "record"]
        self.interfaces = [d for d in self.types.values() if d["kind"] == "interface"]
        for d in self.interfaces:
            if len(d.get("bases", [])) != 1 or any(self.types.get(base, {}).get("kind") != "external" for base in d["bases"]):
                raise ModelError(f"Java: interface inheritance is not implemented yet: {d['name']}")

    def short(self, name):
        return identifier(name.rsplit("::", 1)[-1])

    def scalar_info(self, t):
        try:
            return SCALARS[self.scalar(t)]
        except (ModelError, KeyError):
            return None

    def shape(self, value):
        t = unalias(value["type"])
        p = value.get("policy", {})
        if t.get("volatile") or p.get("binding", "automatic") != "automatic":
            raise ModelError("Java: volatile and manual declarations require an adapter")
        if {"length_method", "length_from"} & p.keys():
            raise ModelError("Java: computed array lengths are not implemented yet")
        if t["kind"] == "array":
            if "length" in p:
                raise ModelError("Java: fixed arrays use their declared extent, not a length policy")
            if not isinstance(t["count"], int) or not 0 < t["count"] <= 0x7fffffff:
                raise ModelError("Java: fixed array extent must be positive and fit jsize")
            element = unalias(t["element"])
            if t["const"] or element["const"]:
                raise ModelError("Java: fixed array descriptor fields require writable elements")
            element_kind, _ = self.shape({"type": element})
            if element_kind not in ("scalar", "record", "fixed_array") or element["kind"] in ("pointer", "reference", "void"):
                raise ModelError("Java: fixed arrays support scalars, descriptors, and nested arrays")
            return "fixed_array", t
        if t["kind"] in ("pointer", "reference"):
            target = unalias(t["target"])
            if "length" in p:
                info = self.scalar_info(target)
                if not info or info[0] == "void":
                    raise ModelError("Java: arrays currently require scalar or enum elements")
                return "array", target
            if target.get("kind") == "scalar" and target.get("name") in ("char_s", "char_u") and target["const"]:
                return "string", target
            declaration = self.types.get(target.get("name"), {})
            if declaration.get("kind") == "interface":
                return "interface", target
            if declaration.get("kind") == "record":
                return "record", target
            if p.get("direction") == "out":
                return self.shape(dict(value, type=target, policy={k: v for k, v in p.items() if k != "direction"}))
            raise ModelError("Java: unsupported pointer or reference")
        declaration = self.types.get(t.get("name"), {})
        if declaration.get("kind") == "record":
            return "record", t
        if self.scalar_info(t):
            return "scalar", t
        raise ModelError(f"Java: unsupported type {cpp_type(t) if t['kind'] != 'array' else 'fixed array'}")

    def info(self, value):
        kind, t = self.shape(value)
        if kind == "scalar":
            return self.scalar_info(t)
        if kind == "fixed_array":
            java, jni, sig, suffix = self.info({"type": t["element"]})
            array_jni = jni + "Array" if self.shape({"type": t["element"]})[0] == "scalar" else "jobjectArray"
            return java + "[]", array_jni, "[" + sig, "Object"
        if kind == "array":
            java, jni, sig, suffix = self.scalar_info(t)
            return java + "[]", jni + "Array", "[" + sig, "Object"
        if kind == "string":
            return "java.lang.String", "jstring", "Ljava/lang/String;", "Object"
        name = self.short(t["name"])
        return name, "jobject", "L" + self.path + "$" + name + ";", "Object"

    def native_method(self, result, parameters, body):
        name = "n" + str(self.counter)
        self.counter += 1
        self.native.append(f"    private static native {result[0]} {name}({', '.join(j + ' ' + n for j, _, n in parameters)});")
        symbol = "Java_" + jni_name(self.package + "." + self.module) + "_" + name
        args = ", ".join(["JNIEnv* Env", "jclass"] + [jni + " " + n for _, jni, n in parameters])
        tail = "" if result[0] == "void" else "\n    return {};"
        self.cpp.append(f'''extern "C" JNIEXPORT {result[1]} JNICALL {symbol}({args})
{{
    try
    {{
        (void)Env;
{self.indent(body, 8)}
    }}
    catch (const JavaException&) {{}}
    catch (const std::bad_alloc&) {{ Throw(Env, "java/lang/OutOfMemoryError", "Native allocation failed"); }}
    catch (const std::exception&) {{ Throw(Env, "java/lang/RuntimeException", "Native binding call failed"); }}
    catch (...) {{ Throw(Env, "java/lang/RuntimeException", "Unknown native binding failure"); }}{tail}
}}
''')
        return name

    @staticmethod
    def indent(lines, count):
        return "\n".join(" " * count + line if line else "" for line in (lines.splitlines() if isinstance(lines, str) else lines))

    def validate_input(self, value, context):
        """Call-local copies cannot satisfy retention or ownership transfer."""
        native = unalias(value["type"])
        policy = value.get("policy", {})
        if native["kind"] in ("pointer", "reference"):
            if policy.get("lifetime") != "call":
                raise ModelError(f"Java: {context}: input storage requires call lifetime; retained inputs need an adapter")
            if policy.get("ownership") != "borrowed":
                raise ModelError(f"Java: {context}: owned inputs require a transfer adapter")
        kind, target = self.shape(value)
        if kind == "record":
            for field in self.record_fields(target["name"]):
                self.validate_input(field, target["name"] + "::" + field["name"])
        elif kind == "fixed_array":
            self.validate_input({"type": target["element"]}, context + "[]")

    def read(self, value, expr, var, count=None):
        kind, t = self.shape(value)
        nullable = str(value.get("policy", {}).get("nullable", False)).lower()
        if kind == "scalar":
            native = mutable_type(t)
            declaration = self.types.get(t.get("name"), {})
            underlying = mutable_type(declaration["underlying_type"]) if declaration.get("kind") == "enum" else native
            return [f"const {native} {var} = static_cast<{native}>(ReadScalar<{underlying}>(Env, {expr}));"], var
        if kind == "string":
            return [f'Require(Env, {expr} != nullptr || {nullable}, "Required string is null");',
                    f"const std::string {var}Storage = {expr} ? ReadString(Env, {expr}) : std::string{{}};",
                    f"const char* {var} = {expr} ? {var}Storage.c_str() : nullptr;"], var
        if kind == "record":
            short = self.short(t["name"])
            original = unalias(value["type"])
            if original["kind"] == "pointer":
                return [f'Require(Env, {expr} != nullptr || {nullable}, "Required descriptor is null");',
                        f"ValueStorage<Input{short}> {var}Storage;", f"if ({expr}) Read{short}(Env, {expr}, {var}Storage.Get());",
                        f"const {t['name']}* {var} = {expr} ? &{var}Storage.Get().Value : nullptr;"], var
            return [f"ValueStorage<Input{short}> {var}Storage;", f"Read{short}(Env, {expr}, {var}Storage.Get());"], var + "Storage.Get().Value"
        if kind == "interface":
            lines = [f"Owned<{t['name']}> {var};", f'ReadInterface(Env, {expr}, {var}, "{self.state_signature}", {nullable});']
            return lines, ("*" if unalias(value["type"])["kind"] == "reference" else "") + var + ".Ptr"
        if count is None:
            raise ModelError("Java: array requires an explicit count")
        java, jni, sig, suffix = self.scalar_info(t)
        native = mutable_type(t)
        # Required arrays normalize null with count zero to non-null empty storage.
        allocation = f"new {native}[static_cast<size_t>({var}Count)]{{}}"
        if value.get("policy", {}).get("nullable", False):
            allocation = f"{expr} ? {allocation} : nullptr"
        return [f'Require(Env, {expr} != nullptr || {nullable} || {count} == 0, "Nonempty array is null");',
                f"const jsize {var}Count = {expr} ? ArrayCount(Env, {count}) : 0;",
                f'if ({expr}) Require(Env, Env->GetArrayLength({expr}) >= {var}Count, "Array is shorter than its count");',
                f"std::vector<{jni}> {var}Java(static_cast<size_t>({var}Count));",
                f"if ({var}Count) Env->Get{suffix}ArrayRegion({expr}, 0, {var}Count, {var}Java.data());", "Check(Env);",
                f"std::unique_ptr<{native}[]> {var}({allocation});",
                f"for (jsize Index = 0; Index < {var}Count; ++Index)",
                f"    {var}[Index] = ReadScalar<{native}>(Env, {var}Java[static_cast<size_t>(Index)]);"], var + ".get()"

    def read_fixed_array(self, t, expr, destination, var, storage):
        count = t["count"]
        element = {"type": t["element"]}
        kind, native = self.shape(element)
        _, jni, _, suffix = self.info(element)
        index = var + "Index"
        lines = [f'Require(Env, {expr} != nullptr, "Required fixed array is null");',
                 f'Require(Env, Env->GetArrayLength({expr}) == {count}, "Fixed array has incorrect length");']
        if kind == "scalar":
            # Bound temporary stack storage independently of the native array size.
            # Small arrays still use one stack buffer and a single JNI region call.
            capacity = min(count, 256)
            offset, size = var + "Offset", var + "Size"
            body = [f"const jsize {size} = std::min<jsize>({count} - {offset}, {capacity});",
                    f"Env->Get{suffix}ArrayRegion({expr}, {offset}, {size}, {var}Data.data());", "Check(Env);",
                    f"for (jsize {index} = 0; {index} < {size}; ++{index})", "{",
                    f"    {destination}[{offset} + {index}] = ReadScalar<{mutable_type(native)}>(Env, {var}Data[{index}]);", "}",
                    f"{offset} += {size};"]
            return lines + [f"std::array<{jni}, {capacity}> {var}Data{{}};",
                            f"for (jsize {offset} = 0; {offset} < {count};)", "{", self.indent(body, 4), "}"]
        else:
            item = var + "Item"
            body = [f"const LocalRef<{jni}> {item}{{Env, static_cast<{jni}>(Env->GetObjectArrayElement({expr}, {index}))}};", "Check(Env);"]
            if kind == "fixed_array":
                body += self.read_fixed_array(native, item + ".Get()", destination + f"[{index}]", item, storage + f"[{index}]")
            else:
                body += [f"Read{self.short(native['name'])}(Env, {item}.Get(), {storage}[{index}]);",
                         f"{destination}[{index}] = {storage}[{index}].Value;"]
        return lines + [f"for (jsize {index} = 0; {index} < {count}; ++{index})", "{", self.indent(body, 4), "}"]

    def write_fixed_array(self, t, expr, var, validate_required):
        count = t["count"]
        element = {"type": t["element"]}
        kind, native = self.shape(element)
        _, jni, sig, suffix = self.info(element)
        array_jni = self.info({"type": t})[1]
        index = var + "Index"
        if kind == "scalar":
            # Match input conversion: widening elements must not exhaust the JVM stack.
            capacity = min(count, 256)
            offset, size = var + "Offset", var + "Size"
            body = [f"const jsize {size} = std::min<jsize>({count} - {offset}, {capacity});",
                    f"for (jsize {index} = 0; {index} < {size}; ++{index})", "{",
                    f"    {var}Data[{index}] = WriteScalar<{jni}>({expr}[{offset} + {index}]);", "}",
                    f"Env->Set{suffix}ArrayRegion({var}.Get(), {offset}, {size}, {var}Data.data());", "Check(Env);",
                    f"{offset} += {size};"]
            lines = [f"const LocalRef<{array_jni}> {var}{{Env, Env->New{suffix}Array({count})}};", "Check(Env);",
                     f"std::array<{jni}, {capacity}> {var}Data{{}};",
                     f"for (jsize {offset} = 0; {offset} < {count};)", "{", self.indent(body, 4), "}"]
        else:
            class_name = sig[1:-1] if kind == "record" else sig
            lines = [f'const LocalRef<jclass> {var}Class{{Env, Env->FindClass("{class_name}")}};', "Check(Env);",
                     f"const LocalRef<jobjectArray> {var}{{Env, Env->NewObjectArray({count}, {var}Class.Get(), nullptr)}};", "Check(Env);"]
            body, converted = self.write(element, expr + f"[{index}]", var + "Item", validate_required=validate_required)
            body += [f"Env->SetObjectArrayElement({var}.Get(), {index}, {converted});", "Check(Env);"]
            lines += [f"for (jsize {index} = 0; {index} < {count}; ++{index})", "{", self.indent(body, 4), "}"]
        return lines, var + ".Get()"

    def write(self, value, expr, var, count=None, owner=None, validate_required="true"):
        kind, t = self.shape(value)
        java, jni, sig, suffix = self.info(value)
        raw_storage = kind in ("string", "array") or (kind == "record" and unalias(value["type"])["kind"] in ("pointer", "reference"))
        if raw_storage and value.get("policy", {}).get("ownership") == "owned":
            raise ModelError("Java: owned raw storage requires a release adapter")
        if kind == "scalar":
            return [f"const {jni} {var} = WriteScalar<{jni}>({expr});"], var
        if kind == "fixed_array":
            return self.write_fixed_array(t, expr, var, validate_required)
        if kind == "string":
            lines = []
            if not value.get("policy", {}).get("nullable", False):
                lines.append(f'Require(Env, !{validate_required} || {expr} != nullptr, "Native function returned a null string");')
            return lines + [f"const LocalRef<jstring> {var}{{Env, WriteString(Env, {expr})}};"], var + ".Get()"
        if kind == "record":
            short = self.short(t["name"])
            if unalias(value["type"])["kind"] == "pointer":
                lines = []
                if not value.get("policy", {}).get("nullable", False):
                    lines.append(f'Require(Env, {expr} != nullptr, "Native function returned a null descriptor");')
                lines.append(f"const LocalRef<jobject> {var}{{Env, {expr} ? Write{short}(Env, *{expr}, {validate_required}) : nullptr}};")
            else:
                lines = [f"const LocalRef<jobject> {var}{{Env, Write{short}(Env, {expr}, {validate_required})}};"]
            return lines, var + ".Get()"
        if kind == "interface":
            if t.get("const"):
                raise ModelError("Java: const interface outputs are not implemented yet")
            if owner is None:
                raise ModelError("Java: interface output must have reference ownership")
            return [f'const LocalRef<jobject> {var}{{Env, Pending.Wrap({owner}, "{self.path}${self.short(t["name"])}", "{self.state_signature}")}};'], var + ".Get()"
        if count is None:
            raise ModelError("Java: returned array requires an explicit count")
        elem = self.scalar_info(t)
        nullable = value.get("policy", {}).get("nullable", False)
        allocation = f"Env->New{elem[3]}Array({var}Count)"
        if nullable:
            allocation = f"{expr} ? {allocation} : nullptr"
        return [f'Require(Env, {expr} != nullptr || {str(nullable).lower()} || {count} == 0, "Native function returned a null array");',
                f"const jsize {var}Count = {expr} ? ArrayCount(Env, {count}) : 0;",
                f"std::vector<{elem[1]}> {var}Data(static_cast<size_t>({var}Count));",
                f"for (jsize Index = 0; Index < {var}Count; ++Index)",
                f"    {var}Data[static_cast<size_t>(Index)] = WriteScalar<{elem[1]}>({expr}[Index]);",
                f"const LocalRef<{jni}> {var}{{Env, {allocation}}};",
                f"if ({var}Count) Env->Set{elem[3]}ArrayRegion({var}.Get(), 0, {var}Count, {var}Data.data());", "Check(Env);"], var + ".Get()"

    def emit_record(self, d):
        name, short = d["name"], self.short(d["name"])
        fields = self.record_fields(name)
        read, write, storage = [], [], []
        for index, field in enumerate(fields):
            member = identifier(field["name"])
            kind, t = self.shape(field)
            java, jni, sig, suffix = self.info(field)
            if kind not in ("scalar", "string", "record", "fixed_array") or unalias(field["type"])["kind"] == "reference":
                raise ModelError(f"Java: unsupported descriptor field {name}::{member}")
            if kind == "record" and unalias(field["type"])["kind"] == "pointer":
                raise ModelError("Java: descriptor pointer fields are not implemented yet")
            read += [f'const jfieldID Field{index} = Field(Env, Object, "{member}", "{sig}");']
            getter = f"Env->Get{suffix}Field(Object, Field{index})"
            if kind == "scalar":
                lines, expr = self.read(field, getter, "FieldValue" + str(index))
                read += lines + [f"Out.Value.{member} = {expr};"]
            else:
                read += [f"const LocalRef<{jni}> FieldValue{index}{{Env, static_cast<{jni}>({getter})}};"]
                if kind == "string":
                    storage.append(f"std::string Storage{index};")
                    nullable = str(field.get("policy", {}).get("nullable", False)).lower()
                    read += [f'Require(Env, FieldValue{index}.Get() != nullptr || {nullable}, "Required string field is null");',
                             f"Out.Storage{index} = FieldValue{index}.Get() ? ReadString(Env, FieldValue{index}.Get()) : std::string{{}};",
                             f"Out.Value.{member} = FieldValue{index}.Get() ? Out.Storage{index}.c_str() : nullptr;"]
                elif kind == "fixed_array":
                    # Keep each descriptor element's strings alive through the native call.
                    # Heap-back the entire element storage so its duplicate native values
                    # do not add another potentially large array to the JVM stack.
                    leaf, dimensions = t, []
                    while leaf["kind"] == "array":
                        dimensions.append(leaf["count"])
                        leaf = unalias(leaf["element"])
                    if self.shape({"type": leaf})[0] == "record":
                        storage_type = f"Input{self.short(leaf['name'])}[]" + "".join(f"[{count}]" for count in dimensions[1:])
                        storage.append(f"std::unique_ptr<{storage_type}> Storage{index} = std::make_unique<{storage_type}>({dimensions[0]});")
                    read += self.read_fixed_array(t, f"FieldValue{index}.Get()", "Out.Value." + member,
                                                  "Array" + str(index), f"Out.Storage{index}")
                else:
                    child = self.short(t["name"])
                    # As with descriptor arrays, keep the child's conversion storage
                    # alive without duplicating its native value on the JVM stack.
                    storage.append(f"std::unique_ptr<Input{child}> Storage{index} = std::make_unique<Input{child}>();")
                    read += [f"Read{child}(Env, FieldValue{index}.Get(), *Out.Storage{index});", f"Out.Value.{member} = Out.Storage{index}->Value;"]
            lines, expr = self.write(field, "Value." + member, "FieldValue" + str(index), validate_required="ValidateRequiredFields")
            write += [f'const jfieldID Field{index} = Field(Env, Object, "{member}", "{sig}");'] + lines
            write += [f"Env->Set{suffix}Field(Object, Field{index}, {expr});", "Check(Env);"]
        self.cpp.append(f'''struct Input{short}
{{
    {name} Value{{}};
{self.indent(storage, 4)}
}};
[[maybe_unused]] static inline void Read{short}(JNIEnv* Env, jobject Object, Input{short}& Out)
{{
    Require(Env, Object != nullptr, "Required descriptor is null");
{self.indent(read, 4)}
}}
static inline void Write{short}Fields(JNIEnv* Env, jobject Object, const {name}& Value, bool ValidateRequiredFields)
{{
    (void)ValidateRequiredFields;
{self.indent(write, 4)}
}}
[[maybe_unused]] static inline jobject Write{short}(JNIEnv* Env, const {name}& Value, bool ValidateRequiredFields)
{{
    jobject Object = NewObject(Env, "{self.path}${short}", true);
    Write{short}Fields(Env, Object, Value, ValidateRequiredFields);
    return Object;
}}
''')
        # Native defaults may be incomplete until the caller fills in required
        # fields. Ordinary inputs and returned snapshots remain strict.
        default = self.native_method(SCALARS["void"], [(short, "jobject", "Object")],
                                     [f"ValueStorage<{name}> Defaults;", f"Write{short}Fields(Env, Object, Defaults.Get(), false);"])
        self.java.append(f'''    public static final class {short} {{
{self.indent([f"public {self.info(f)[0]} {f['name']};" for f in fields], 8)}
        public {short}() {{ {default}(this); }}
        private {short}(boolean unused) {{}}
    }}
''')

    def emit_function(self, fn, owner=None):
        name = self.short(fn["name"])
        qualified = owner + "::" + name if owner else fn["name"]
        if fn.get("policy", {}).get("binding", "automatic") != "automatic":
            raise ModelError(f"Java: manual function requires an adapter: {qualified}")
        parameters, inputs, outputs, lines, args, locals_ = [], [], [], [], [], {}
        if any(p.get("policy", {}).get("direction") == "inout" for p in fn["parameters"]):
            raise ModelError("Java: inout parameters are not implemented yet")
        for parameter in fn["parameters"]:
            if parameter.get("policy", {}).get("direction", "in") != "out":
                self.validate_input(parameter, qualified + "::" + parameter["name"])
        if any(self.shape(value)[0] == "fixed_array" for value in [fn["result"], *fn["parameters"]]):
            raise ModelError("Java: fixed arrays are supported in descriptor fields")
        fixed = self.fixed_arguments.get(qualified, {})
        if owner:
            short = self.short(owner)
            parameters.append((short, "jobject", "Self"))
            lines += [f"Owned<{owner}> SelfNative;", f'ReadInterface(Env, Self, SelfNative, "{self.state_signature}", false);']
        # Read scalar counts first, independent of their position in the signature.
        for p in fn["parameters"]:
            if p["name"] in fixed:
                continue
            if self.shape(p)[0] == "scalar" and p.get("policy", {}).get("direction", "in") != "out":
                read, expr = self.read(p, "Arg" + str(len(locals_)), "Native" + str(len(locals_)))
                info = self.info(p)
                parameters.append((info[0], info[1], "Arg" + str(len(locals_))))
                inputs.append(p)
                locals_[p["name"]] = expr
                lines += read

        def length(p, output=False):
            spec = p.get("policy", {}).get("written" if output and "written" in p.get("policy", {}) else "length")
            if isinstance(spec, int):
                return str(spec)
            if isinstance(spec, str) and spec in locals_:
                return locals_[spec]
            raise ModelError(f"Java: array count must name a scalar parameter: {qualified}")

        # All out parameters have native storage before invoking the API.
        for p in fn["parameters"]:
            if p["name"] in fixed:
                locals_[p["name"]] = fixed[p["name"]]
                continue
            if p["name"] in locals_:
                continue
            kind, t = self.shape(p)
            var = "Native" + str(len(locals_))
            if p.get("policy", {}).get("direction", "in") == "out":
                original = unalias(p["type"])
                if kind == "interface":
                    lines += [f"Owned<{t['name']}> {var};"]
                    if p.get("policy", {}).get("ownership") == "borrowed":
                        lines += [f"{t['name']}* {var}Borrowed = nullptr;"]
                        expr = var + "Borrowed"
                    else:
                        expr = var + ".Ptr"
                elif kind == "array":
                    lines += [f"const jsize {var}Count = ArrayCount(Env, {length(p)});",
                              f"std::unique_ptr<{mutable_type(t)}[]> {var}(new {mutable_type(t)}[static_cast<size_t>({var}Count)]{{}});"]
                    expr = var + ".get()"
                elif kind == "record":
                    lines += [f"ValueStorage<{mutable_type(original['target'])}> {var};"]
                    expr = var + ".Get()"
                else:
                    target = original["target"]
                    lines += [f"{mutable_type(target)} {var}{{}};"]
                    expr = var
                locals_[p["name"]] = expr
                output = p if kind == "array" else dict(p, type=original["target"])
                outputs.append((output, expr, var if kind == "interface" else None))
            else:
                arg = "Arg" + str(len(locals_))
                info = self.info(p)
                parameters.append((info[0], info[1], arg))
                inputs.append(p)
                read, expr = self.read(p, arg, var, length(p) if kind == "array" else None)
                locals_[p["name"]] = expr
                lines += read
        for p in fn["parameters"]:
            expr = locals_[p["name"]]
            if p.get("policy", {}).get("direction") == "out" and unalias(p["type"])["kind"] == "pointer" and self.shape(p)[0] != "array":
                expr = "&" + expr
            args.append(expr)
        result = fn["result"]
        result_info = self.info(result)
        invocation = ("SelfNative.Ptr->" + name if owner else qualified) + "(" + ", ".join(args) + ")"
        result_kind, result_type = self.shape(result)
        if result_kind == "interface":
            if unalias(result["type"])["kind"] == "reference":
                raise ModelError("Java: interface reference returns are not implemented yet")
            lines += [f"Owned<{result_type['name']}> ReturnOwner;", f"ReturnOwner.Ptr = {invocation};"]
            if result.get("policy", {}).get("ownership") == "borrowed":
                lines.append("if (ReturnOwner.Ptr) ReturnOwner.Ptr->AddRef();")
            return_expr = "ReturnOwner.Ptr"
        elif result_info[0] == "void":
            lines.append(invocation + ";")
            return_expr = None
        else:
            lines.append(f"{cpp_type(result['type'])} ReturnValue = {invocation};")
            return_expr = "ReturnValue"
        for p, expr, out_owner in outputs:
            if out_owner and p.get("policy", {}).get("ownership") == "borrowed":
                lines += [f"{out_owner}.Ptr = {expr};", f"if ({expr}) {expr}->AddRef();"]
        lines.append("Transfers Pending{Env};")
        returns = ([] if return_expr is None else [(dict(result, name="returnValue"), return_expr, "ReturnOwner" if result_kind == "interface" else None)]) + outputs
        if outputs:
            result_name = (self.short(owner) + "_" if owner else "") + name + "Result"
            if result_name in self.names:
                raise ModelError(f"Java: result name collision: {result_name}")
            self.names[result_name] = qualified
            fields = []
            if len({p["name"] for p, _, _ in returns}) != len(returns):
                raise ModelError(f"Java: result field name collision: {qualified}")
            for p, _, _ in returns:
                fields.append(f"public {self.info(p)[0]} {identifier(p['name'])};")
            self.java.append(f"    public static final class {result_name} {{\n{self.indent(fields, 8)}\n        private {result_name}() {{}}\n    }}\n")
            lines.append(f'jobject Result = NewObject(Env, "{self.path}${result_name}");')
            result_info = (result_name, "jobject", "L" + self.path + "$" + result_name + ";", "Object")
        for index, (p, expr, out_owner) in enumerate(returns):
            if self.shape(p)[0] == "array" and "written" in p.get("policy", {}):
                lines.append(f'Require(Env, {length(p, True)} <= {length(p)}, "Native written count exceeds capacity");')
            lines_out, converted = self.write(p, expr, "Output" + str(index), length(p, True) if self.shape(p)[0] == "array" else None, out_owner)
            lines += lines_out
            if outputs:
                _, _, sig, suffix = self.info(p)
                lines += [f'const jfieldID OutputField{index} = Field(Env, Result, "{p["name"]}", "{sig}");',
                          f"Env->Set{suffix}Field(Result, OutputField{index}, {converted});", "Check(Env);"]
            elif self.shape(p)[0] == "scalar":
                lines.append(f"{result_info[1]} Result = {converted};")
            else:
                lines += [f"{result_info[1]} Result = static_cast<{result_info[1]}>(Env->NewLocalRef({converted}));", "Check(Env);"]
        lines.append("Pending.Commit();")
        if result_info[0] != "void":
            lines.append("return Result;")
        native = self.native_method(result_info, parameters, lines)
        # Public argument order follows the C++ declaration, even though native
        # conversion reads counts before dependent arrays.
        ordered = [p for p in fn["parameters"] if p in inputs]
        public_args = ", ".join(self.info(p)[0] + " " + identifier(p["name"]) for p in ordered)
        call_args = (["this"] if owner else []) + [p["name"] for p in inputs]
        signature = f"public {'static ' if not owner else ''}{result_info[0]} {name}({public_args})"
        return signature + " { " + ("return " if result_info[0] != "void" else "") + native + "(" + ", ".join(call_args) + "); }"

    def scalar_literal(self, t, value):
        java = self.scalar_info(t)[0]
        if java == "boolean":
            return "true" if value else "false"
        if java in ("float", "double"):
            value = float(value)
            if not math.isfinite(value):
                member = "NaN" if math.isnan(value) else "POSITIVE_INFINITY" if value > 0 else "NEGATIVE_INFINITY"
                return f"java.lang.{'Float' if java == 'float' else 'Double'}.{member}"
            # Hexadecimal literals preserve the extracted value, including signed
            # zero and subnormals, without depending on decimal formatting.
            return value.hex() + ("f" if java == "float" else "")
        if java == "long":
            if value >= 2 ** 63:
                value -= 2 ** 64
            return str(value) + "L"
        return str(value)

    def generate(self):
        includes = "\n".join('#include "' + h + '"' for h in self.headers)
        self.cpp.append('// Generated; do not edit.\n#include "JNIRuntime.hpp"\n#include <algorithm>\n#include <array>\n' + includes + '\nusing namespace DiligentBindings;\n')
        scalar_types = {}
        def collect(value):
            if isinstance(value, dict):
                if value.get("kind") == "scalar":
                    scalar_types[mutable_type(value)] = value["bits"]
                for child in value.values():
                    collect(child)
            elif isinstance(value, list):
                for child in value:
                    collect(child)
        collect(list(self.types.values()))
        for name, bits in sorted(scalar_types.items()):
            self.cpp.append(f'static_assert(sizeof({name}) * 8 == {bits}, "Binding model ABI differs from the native compiler");')
        # Dependencies precede enclosing records; Clang declaration order is not
        # guaranteed to be dependency order in a selected API model.
        visited = set()
        def record(d):
            if d["name"] in visited:
                return
            visited.add(d["name"])
            for f in self.record_fields(d["name"]):
                kind, t = self.shape(f)
                while kind == "fixed_array":
                    kind, t = self.shape({"type": t["element"]})
                if kind == "record":
                    record(self.types[t["name"]])
            self.emit_record(d)
        for d in self.records:
            record(d)
        for d in self.types.values():
            if d["kind"] == "enum" and d["name"] not in self.native_only:
                info = self.scalar_info(d["underlying_type"])
                values = []
                for value in d["values"]:
                    literal = self.scalar_literal(d["underlying_type"], value["value"])
                    values.append(f"public static final {info[0]} {identifier(value['name'])} = {literal};")
                short = self.short(d["name"])
                self.java.append(f"    public static final class {short} {{\n        private {short}() {{}}\n{self.indent(values, 8)}\n    }}\n")
            elif d["kind"] == "constant":
                if d.get("policy", {}).get("binding", "automatic") != "automatic":
                    raise ModelError(f"Java: manual constant requires an adapter: {d['name']}")
                info = self.scalar_info(d["type"])
                if not info or info[0] == "void":
                    raise ModelError(f"Java: only scalar constants are supported: {d['name']}")
                self.java.append(f"    public static final {info[0]} {self.short(d['name'])} = {self.scalar_literal(d['type'], d['value'])};\n")
            elif d["kind"] == "function":
                self.java.append("    " + self.emit_function(d) + "\n")
        for index, d in enumerate(self.interfaces):
            methods = [self.emit_function(m, d["name"]) for m in self.interface_methods(d["name"]).values()]
            short = self.short(d["name"])
            self.java.append(f"    public static final class {short} extends NativeObject {{\n        private {short}() {{ super({index}); }}\n{self.indent(methods, 8)}\n    }}\n")
        release = ["switch (Kind)", "{"]
        for index, d in enumerate(self.interfaces):
            release.append(f"case {index}: reinterpret_cast<{d['name']}*>(static_cast<std::uintptr_t>(Pointer))->Release(); break;")
        release += ['default: throw std::runtime_error{"Unknown native interface kind"};', "}"]
        release_method = self.native_method(SCALARS["void"], [("long", "jlong", "Pointer"), ("int", "jint", "Kind")], release)
        java = f'''// Generated; do not edit.
package {self.package};

public final class {self.module} {{
    private {self.module}() {{}}
    static {{ java.lang.System.loadLibrary({json.dumps(self.library)}); }}
    private static final java.lang.ref.Cleaner CLEANER = java.lang.ref.Cleaner.create();

    private static final class State implements java.lang.Runnable {{
        private long pointer;
        private final int kind;
        private State(int kind) {{ this.kind = kind; }}
        @java.lang.Override public synchronized void run() {{
            if (pointer != 0) {{
                long value = pointer;
                pointer = 0;
                {release_method}(value, kind);
            }}
        }}
    }}

    public abstract static class NativeObject implements java.lang.AutoCloseable {{
        private final State state;
        private final java.lang.ref.Cleaner.Cleanable cleanable;
        private NativeObject(int kind) {{
            state = new State(kind);
            cleanable = CLEANER.register(this, state);
        }}
        @java.lang.Override public final void close() {{ cleanable.clean(); }}
    }}

{chr(10).join(self.java)}
{chr(10).join(self.native)}
}}
'''
        return java, "\n".join(self.cpp)


def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--library", required=True)
    parser.add_argument("--header", action="append", required=True)
    parser.add_argument("--adapters", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        model = json.loads(args.model.read_text(encoding="utf-8"))
        adapters = load_config(args.adapters) if args.adapters else None
        java, cpp = Generator(model, args.module, args.package, args.library, args.header, adapters).generate()
        write_if_changed(args.output / (args.module + ".java"), java)
        write_if_changed(args.output / (args.module + ".cpp"), cpp)
    except (ModelError, OSError, ValueError) as error:
        parser.exit(1, f"Java binding generation failed: {error}\n")
    print(f"Generated {args.module} Java/JNI bindings")


if __name__ == "__main__":
    main()
