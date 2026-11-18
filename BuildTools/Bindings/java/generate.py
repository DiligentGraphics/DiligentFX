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
        self.constant_counter = 0
        for name, function in self.field_count_functions.items():
            owner, _, member = name.rpartition("::")
            field = next(f for f in self.types[owner]["fields"] if f["name"] == member)
            # Annotate our private field copy so inherited fields and nested input
            # validation use the same array shape without altering the shared model.
            field["count_function"] = function
        for name, declaration in self.types.items():
            if name in self.native_only or declaration["kind"] in ("external", "typedef"):
                continue
            short = identifier(name.rsplit("::", 1)[-1])
            if short in self.names or short in {module, "State", "NativeObject", "CLEANER", "java", "ReadByteScope", "WriteByteScope"}:
                raise ModelError(f"Java: ambiguous declaration name {short}")
            self.names[short] = name
        self.records = [d for d in self.types.values() if d["kind"] == "record"]
        self.interfaces = [d for d in self.types.values() if d["kind"] == "interface"]
        self.interface_results = {}
        self.record_bases = {base for d in self.records for base in d.get("bases", [])}
        if any(len(d.get("bases", [])) > 1 for d in self.records):
            raise ModelError("Java: descriptors support only single inheritance")
        self.interface_ancestors = {}
        def ancestors(name, visiting):
            if name in self.interface_ancestors:
                return self.interface_ancestors[name]
            if name in visiting:
                raise ModelError(f"Java: cyclic interface inheritance: {name}")
            d = self.types.get(name, {})
            if d.get("kind") == "external" and d.get("policy", {}).get("ownership") == "reference_counted":
                chain = [name]
            elif d.get("kind") == "interface":
                if len(d.get("bases", [])) != 1 or d.get("fields"):
                    raise ModelError(f"Java: interfaces need one base and no data fields: {name}")
                if d.get("policy", {}).get("ownership") != "reference_counted":
                    raise ModelError(f"Java: interfaces must be reference counted: {name}")
                if any(m["name"] in ("close", "QueryInterface") for m in d["methods"]):
                    raise ModelError(f"Java: interface method conflicts with the runtime: {name}")
                chain = [name, *ancestors(d["bases"][0], visiting | {name})]
            else:
                raise ModelError(f"Java: interface base must be a selected interface or reference-counted external type: {name}")
            self.interface_ancestors[name] = chain
            return chain
        for d in self.interfaces:
            ancestors(d["name"], set())
        self.interface_casts = {name: f"CastInterface{index}" for index, name in enumerate(self.interface_ancestors)}

    def short(self, name):
        return identifier(name.rsplit("::", 1)[-1])

    def scalar_info(self, t):
        try:
            return SCALARS[self.scalar(t)]
        except (ModelError, KeyError):
            return None

    def interface_element(self, t):
        pointer = unalias(t)
        if pointer["kind"] == "pointer":
            target = unalias(pointer["target"])
            if self.types.get(target.get("name"), {}).get("kind") == "interface":
                return target
        return None

    def shape(self, value):
        t = unalias(value["type"])
        p = value.get("policy", {})
        if t.get("volatile") or p.get("binding", "automatic") != "automatic":
            raise ModelError("Java: volatile and manual declarations require an adapter")
        if {"length_method", "length_from"} & p.keys() and t["kind"] != "pointer":
            raise ModelError("Java: computed array lengths require a pointer")
        if t["kind"] == "array":
            if "length" in p:
                raise ModelError("Java: fixed arrays use their declared extent, not a length policy")
            if not isinstance(t["count"], int) or not 0 < t["count"] <= 0x7fffffff:
                raise ModelError("Java: fixed array extent must be positive and fit jsize")
            element = unalias(t["element"])
            element_kind, _ = self.shape({"type": element})
            if element_kind not in ("scalar", "record", "fixed_array") or element["kind"] in ("pointer", "reference", "void"):
                raise ModelError("Java: fixed arrays support scalars, descriptors, and nested arrays")
            return "fixed_array", t
        if t["kind"] in ("pointer", "reference"):
            target = unalias(t["target"])
            if value.get("count_function") or {"length", "length_method", "length_from"} & p.keys():
                info = self.scalar_info(target)
                record = self.types.get(target.get("name"), {}).get("kind") == "record"
                if target["kind"] == "void":
                    if t["kind"] != "pointer" or target["volatile"]:
                        raise ModelError("Java: byte spans require non-volatile void pointers")
                elif not info and not self.interface_element(target) and not record:
                    raise ModelError("Java: arrays require scalar, enum, interface, descriptor, or byte elements")
                return "array", target
            if target.get("kind") == "scalar" and target.get("name") in ("char_s", "char_u") and target["const"]:
                return "string", target
            declaration = self.types.get(target.get("name"), {})
            if declaration.get("kind") == "interface":
                return "interface", target
            if declaration.get("kind") == "record":
                return "record", target
            if target["kind"] == "void":
                raise ModelError("Java: raw byte pointers require a length")
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
            if t["kind"] == "void":
                return "byte[]", "jbyteArray", "[B", "Object"
            java, jni, sig, suffix = self.info({"type": t})
            array_jni = jni + "Array" if self.scalar_info(t) else "jobjectArray"
            return java + "[]", array_jni, "[" + sig, "Object"
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
        if kind == "array" and target["kind"] == "void" and not target["const"]:
            raise ModelError(f"Java: {context}: writable byte inputs require a copyback adapter")
        if kind == "record":
            if self.record_has_const_fields(target["name"]):
                raise ModelError(f"Java: {context}: descriptors with const fields are output-only")
            for field in self.record_fields(target["name"]):
                self.validate_input(field, target["name"] + "::" + field["name"])
        elif kind == "fixed_array":
            self.validate_input({"type": target["element"]}, context + "[]")
        elif kind == "array" and self.shape({"type": target})[0] == "record":
            self.validate_input({"type": target}, context + "[]")

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
            lines = [f"Owned<{t['name']}> {var};", f'ReadInterface(Env, {expr}, {var}, "{self.state_signature}", {nullable}, {self.interface_casts[t["name"]]});']
            return lines, ("*" if unalias(value["type"])["kind"] == "reference" else "") + var + ".Ptr"
        lines = [f"{self.array_storage(t)} {var};"] + self.read_array(value, expr, var, count, var)
        return lines, self.array_data(t, var)

    @staticmethod
    def array_native_type(element):
        # Byte storage preserves object representations without numeric widening.
        return "unsigned char" if element["kind"] == "void" else mutable_type(element)

    def array_storage(self, element):
        interface = self.interface_element(element)
        if interface:
            return f"InterfaceArray<{interface['name']}>"
        if self.scalar_info(element):
            return f"std::unique_ptr<{self.array_native_type(element)}[]>"
        return f"DescriptorArray<{mutable_type(element)}, Input{self.short(element['name'])}>"

    def array_data(self, element, storage):
        if self.interface_element(element):
            return storage + ".Data()"
        if self.scalar_info(element):
            return storage + ".get()"
        return storage + ".Values.get()"

    def read_array(self, value, expr, storage, count, var):
        if count is None:
            raise ModelError("Java: array requires an explicit count")
        _, t = self.shape(value)
        interface = self.interface_element(t)
        if interface:
            return [self.read_interface_array(value, interface, expr, count, storage)]
        nullable = str(value.get("policy", {}).get("nullable", False)).lower()
        native = self.array_native_type(t)
        # Required arrays normalize null with count zero to non-null empty storage.
        allocation = f"new {native}[static_cast<size_t>({var}Count)]{{}}"
        if value.get("policy", {}).get("nullable", False):
            allocation = f"{expr} ? {allocation} : nullptr"
        lines = [f'Require(Env, {expr} != nullptr || {nullable} || {count} == 0, "Nonempty array is null");',
                 f"const jsize {var}Count = {expr} ? ArrayCount(Env, {count}) : 0;",
                 f'if ({expr}) Require(Env, Env->GetArrayLength({expr}) >= {var}Count, "Array is shorter than its count");']
        if t["kind"] == "void":
            return lines + [f"{storage}.reset({allocation});",
                            f"if ({var}Count) Env->GetByteArrayRegion({expr}, 0, {var}Count, reinterpret_cast<jbyte*>({storage}.get()));", "Check(Env);"]
        scalar = self.scalar_info(t)
        if scalar:
            _, jni, _, suffix = scalar
            return lines + [f"std::vector<{jni}> {var}Java(static_cast<size_t>({var}Count));",
                            f"if ({var}Count) Env->Get{suffix}ArrayRegion({expr}, 0, {var}Count, {var}Java.data());", "Check(Env);",
                            f"{storage}.reset({allocation});",
                            f"for (jsize Index = 0; Index < {var}Count; ++Index)",
                            f"    {storage}[Index] = ReadScalar<{native}>(Env, {var}Java[static_cast<size_t>(Index)]);"]
        child = self.short(t["name"])
        return lines + [f"{storage}.Values.reset({allocation});",
                        f"if ({var}Count) {storage}.Elements = std::make_unique<Input{child}[]>({var}Count);",
                        f"for (jsize Index = 0; Index < {var}Count; ++Index)", "{",
                        f"    const LocalRef<jobject> Item{{Env, Env->GetObjectArrayElement({expr}, Index)}};",
                        f"    Read{child}(Env, Item.Get(), {storage}.Elements[Index]);",
                        f"    {storage}.Values[Index] = {storage}.Elements[Index].Value;", "}"]

    def read_interface_array(self, value, element, expr, count, storage):
        nullable = str(value.get("policy", {}).get("nullable", False)).lower()
        element_nullable = str(value.get("policy", {}).get("element_nullable", False)).lower()
        return (f'ReadInterfaceArray(Env, {expr}, {count}, {storage}, "{self.state_signature}", '
                f'{nullable}, {element_nullable}, {self.interface_casts[element["name"]]});')

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
                    lines.append(f'Require(Env, !{validate_required} || {expr} != nullptr, "Native function returned a null descriptor");')
                lines.append(f"const LocalRef<jobject> {var}{{Env, {expr} ? Write{short}(Env, *{expr}, {validate_required}, Pending) : nullptr}};")
            else:
                lines = [f"const LocalRef<jobject> {var}{{Env, Write{short}(Env, {expr}, {validate_required}, Pending)}};"]
            return lines, var + ".Get()"
        if kind == "interface":
            if t.get("const"):
                raise ModelError("Java: const interface outputs are not implemented yet")
            lines = []
            if owner is None:
                if value.get("policy", {}).get("ownership") != "borrowed":
                    raise ModelError("Java: interface fields must borrow their native references")
                nullable = str(value.get("policy", {}).get("nullable", False)).lower()
                owner = var + "Owner"
                lines = [f'Require(Env, !{validate_required} || {expr} != nullptr || {nullable}, "Native interface field is null");',
                         f"Owned<{t['name']}> {owner};", f"{owner}.Ptr = {expr};",
                         f"if ({owner}.Ptr) {owner}.Ptr->AddRef();"]
            return lines + [f'const LocalRef<jobject> {var}{{Env, Pending.Wrap({owner}, "{self.path}${self.short(t["name"])}", "{self.state_signature}")}};'], var + ".Get()"
        if count is None:
            raise ModelError("Java: returned array requires an explicit count")
        element = self.interface_element(t)
        if element:
            if element["const"]:
                raise ModelError("Java: const interface outputs are not implemented yet")
            nullable = str(value.get("policy", {}).get("nullable", False)).lower()
            element_nullable = str(value.get("policy", {}).get("element_nullable", False)).lower()
            return [f'const LocalRef<jobjectArray> {var}{{Env, WriteInterfaceArray(Env, {expr}, {count}, Pending, '
                    f'"{self.path}${self.short(element["name"])}", "{self.state_signature}", '
                    f'{nullable}, {element_nullable}, {validate_required})}};'], var + ".Get()"
        elem = self.scalar_info(t)
        nullable = str(value.get("policy", {}).get("nullable", False)).lower()
        lines = [f'Require(Env, !{validate_required} || {expr} != nullptr || {nullable} || {count} == 0, "Native function returned a null array");',
                 f"const jsize {var}Count = {expr} ? ArrayCount(Env, {count}) : 0;"]
        # Constructors preserve incomplete defaults, just as interface-array fields do.
        absent = f"({expr} == nullptr && ({nullable} || !{validate_required}))"
        if t["kind"] == "void":
            return lines + [f"const LocalRef<jbyteArray> {var}{{Env, {absent} ? nullptr : Env->NewByteArray({var}Count)}};",
                            f"if ({var}Count) Env->SetByteArrayRegion({var}.Get(), 0, {var}Count, reinterpret_cast<const jbyte*>({expr}));", "Check(Env);"], var + ".Get()"
        if not elem:
            lines += [f'const LocalRef<jclass> {var}Class{{Env, Env->FindClass("{self.path}${self.short(t["name"])}")}};',
                      f"const LocalRef<jobjectArray> {var}{{Env, {absent} ? nullptr : Env->NewObjectArray({var}Count, {var}Class.Get(), nullptr)}};"]
            body, item = self.write({"type": t}, f"{expr}[Index]", var + "Item", validate_required=validate_required)
            body += [f"Env->SetObjectArrayElement({var}.Get(), Index, {item});", "Check(Env);"]
            return lines + [f"for (jsize Index = 0; Index < {var}Count; ++Index)", "{", self.indent(body, 4), "}"], var + ".Get()"
        allocation = f"{absent} ? nullptr : Env->New{elem[3]}Array({var}Count)"
        return lines + [
                f"std::vector<{elem[1]}> {var}Data(static_cast<size_t>({var}Count));",
                f"for (jsize Index = 0; Index < {var}Count; ++Index)",
                f"    {var}Data[static_cast<size_t>(Index)] = WriteScalar<{elem[1]}>({expr}[Index]);",
                f"const LocalRef<{jni}> {var}{{Env, {allocation}}};",
                f"if ({var}Count) Env->Set{elem[3]}ArrayRegion({var}.Get(), 0, {var}Count, {var}Data.data());", "Check(Env);"], var + ".Get()"

    def record_has_const_fields(self, name):
        # Output snapshots may contain const members; assigning them during
        # input conversion is invalid, including within embedded records/arrays.
        for field in self.record_fields(name):
            t = unalias(field["type"])
            while t["kind"] == "array" and not t["const"]:
                t = unalias(t["element"])
            if t["const"]:
                return True
            if t["kind"] == "pointer":
                # Descriptor pointers and pointer/count arrays require their own
                # input converters too. Const pointees can be constructed in
                # temporary storage, but their const members cannot be assigned.
                t = unalias(t["target"])
            if self.types.get(t.get("name"), {}).get("kind") == "record" and self.record_has_const_fields(t["name"]):
                return True
        return False

    def emit_record(self, d):
        name, short = d["name"], self.short(d["name"])
        fields = self.record_fields(name)
        bases = d.get("bases", [])
        base_count = len(self.record_fields(bases[0])) if bases else 0
        cache_setup, field_ids = [], []
        if base_count:
            cache_setup.append(f"const auto& BaseFields = Get{self.short(bases[0])}Fields(Env);")
            field_ids.extend(f"BaseFields[{index}]" for index in range(base_count))
        if d["fields"]:
            cache_setup.append(f'const LocalRef<jclass> Class{{Env, Env->FindClass("{self.path}${short}")}};')
        read_first, read_fields, read_computed, write, storage = [], [], [], [], []
        for index, field in enumerate(fields):
            member = identifier(field["name"])
            kind, t = self.shape(field)
            java, jni, sig, suffix = self.info(field)
            if index >= base_count:
                field_ids.append(f'Field(Env, Class.Get(), "{member}", "{sig}")')
            if kind not in ("scalar", "string", "record", "fixed_array", "interface", "array") or unalias(field["type"])["kind"] == "reference":
                raise ModelError(f"Java: unsupported descriptor field {name}::{member}")
            if kind == "array" and t["kind"] == "void" and not t["const"]:
                raise ModelError("Java: writable byte fields require a copyback adapter")
            pointer_field = kind == "record" and unalias(field["type"])["kind"] == "pointer"
            if pointer_field and not t["const"]:
                raise ModelError("Java: descriptor pointer fields require const data; mutable pointees need a copyback adapter")
            # Read scalar counts and embedded descriptors before dependent array fields,
            # even when the count is declared after the pointer in the native struct.
            count_function = field.get("count_function")
            read = read_computed if count_function else read_first if kind in ("scalar", "record") else read_fields
            count = field.get("policy", {}).get("length")
            read_count = str(count) if isinstance(count, int) else "Out.Value." + str(count)
            write_count = str(count) if isinstance(count, int) else "Value." + str(count)
            if count_function:
                # Query once after ordinary fields are populated. Size calculations
                # remain native and use the same array bounds checks and ownership.
                read_count = write_count = f"FieldCount{index}"
                read.append(f"const std::uint64_t {read_count} = {count_function}(Out.Value);")
                write.append(f"const std::uint64_t {write_count} = {count_function}(Value);")
            read += [f"const jfieldID Field{index} = Fields[{index}];"]
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
                elif kind == "interface":
                    nullable = str(field.get("policy", {}).get("nullable", False)).lower()
                    storage.append(f"Owned<{t['name']}> Storage{index};")
                    read += [f'ReadInterface(Env, FieldValue{index}.Get(), Out.Storage{index}, "{self.state_signature}", {nullable}, {self.interface_casts[t["name"]]});',
                             f"Out.Value.{member} = Out.Storage{index}.Ptr;"]
                elif kind == "array":
                    storage.append(f"{self.array_storage(t)} Storage{index};")
                    read += self.read_array(field, f"FieldValue{index}.Get()", f"Out.Storage{index}", read_count, f"Array{index}")
                    read.append(f"Out.Value.{member} = {self.array_data(t, f'Out.Storage{index}')};")
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
                    if pointer_field:
                        # Null optional fields allocate no pointee storage. For present
                        # fields, retain all recursive conversion data through the call.
                        nullable = str(field.get("policy", {}).get("nullable", False)).lower()
                        storage.append(f"std::unique_ptr<Input{child}> Storage{index};")
                        read += [f'Require(Env, FieldValue{index}.Get() != nullptr || {nullable}, "Required descriptor field is null");',
                                 f"if (FieldValue{index}.Get())", "{",
                                 f"    Out.Storage{index} = std::make_unique<Input{child}>();",
                                 f"    Read{child}(Env, FieldValue{index}.Get(), *Out.Storage{index});", "}",
                                 f"Out.Value.{member} = Out.Storage{index} ? &Out.Storage{index}->Value : nullptr;"]
                    else:
                        storage.append(f"std::unique_ptr<Input{child}> Storage{index} = std::make_unique<Input{child}>();")
                        read += [f"Read{child}(Env, FieldValue{index}.Get(), *Out.Storage{index});", f"Out.Value.{member} = Out.Storage{index}->Value;"]
            lines, expr = self.write(field, "Value." + member, "FieldValue" + str(index), count=write_count if kind == "array" else None, validate_required="ValidateRequiredFields")
            write += [f"const jfieldID Field{index} = Fields[{index}];"] + lines
            write += [f"Env->Set{suffix}Field(Object, Field{index}, {expr});", "Check(Env);"]
        # Only IDs survive initialization, not JNI class references. The generated
        # classes share the native entry point's class loader, so their IDs remain
        # valid for the library's lifetime without pinning that loader. Function-local
        # static initialization publishes the complete cache safely to all threads;
        # a failed JNI lookup throws and leaves initialization available for retry.
        self.cpp.append(f'''static const std::array<jfieldID, {len(fields)}>& Get{short}Fields(JNIEnv* Env)
{{
    static const std::array<jfieldID, {len(fields)}> Fields = [Env]()
    {{
        (void)Env;
{self.indent(cache_setup, 8)}
        return std::array<jfieldID, {len(fields)}>{{
{self.indent([entry + "," for entry in field_ids], 12)}
        }};
    }}();
    return Fields;
}}
''')
        cached_fields = f"    const auto& Fields = Get{short}Fields(Env);" if fields else f"    (void)Get{short}Fields(Env);"
        reader = f'''struct Input{short}
{{
    {name} Value{{}};
{self.indent(storage, 4)}
}};
[[maybe_unused]] static inline void Read{short}(JNIEnv* Env, jobject Object, Input{short}& Out)
{{
    Require(Env, Object != nullptr, "Required descriptor is null");
{cached_fields}
{self.indent(read_first + read_fields + read_computed, 4)}
}}
''' if not self.record_has_const_fields(name) else ''
        self.cpp.append(f'''{reader}static inline void Write{short}Fields(JNIEnv* Env, jobject Object, const {name}& Value, bool ValidateRequiredFields, Transfers& Pending)
{{
    (void)ValidateRequiredFields;
    (void)Pending;
{cached_fields}
{self.indent(write, 4)}
}}
[[maybe_unused]] static inline jobject Write{short}(JNIEnv* Env, const {name}& Value, bool ValidateRequiredFields, Transfers& Pending)
{{
    jobject Object = NewObject(Env, "{self.path}${short}", true);
    Write{short}Fields(Env, Object, Value, ValidateRequiredFields, Pending);
    return Object;
}}
''')
        # Native defaults may be incomplete until the caller fills in required
        # fields. Ordinary inputs and returned snapshots remain strict.
        default = self.native_method(SCALARS["void"], [(short, "jobject", "Object")],
                                     [f"ValueStorage<{name}> Defaults;", "Transfers Pending{Env};",
                                      f"Write{short}Fields(Env, Object, Defaults.Get(), false, Pending);", "Pending.Commit();"])
        parent = " extends " + self.short(bases[0]) if bases else ""
        final = "" if name in self.record_bases else "final "
        # Initialize inherited fields from this descriptor's native defaults once.
        # Snapshot construction also skips all base default-initialization calls.
        base_init = "super(false); " if bases else ""
        self.java.append(f'''    public static {final}class {short}{parent} {{
{self.indent([f"public {self.info(f)[0]} {f['name']};" for f in d["fields"]], 8)}
        public {short}() {{ {base_init}{default}(this); }}
        private {short}(boolean unused) {{ {base_init}}}
    }}
''')

    def emit_result_class(self, fn, owner, inputs, returns, types=None):
        name = self.short(fn["name"])
        qualified = owner + "::" + name if owner else fn["name"]
        signature = (name, tuple(self.info(p)[0] for p in fn["parameters"] if p in inputs))
        types = types if types is not None else [self.info(p) for p, _, _ in returns]
        fields = [identifier(p["name"]) for p, _, _ in returns]
        if len(set(fields)) != len(fields):
            raise ModelError(f"Java: result field name collision: {qualified}")
        inherited = None
        if owner:
            for base in self.interface_ancestors[owner][1:]:
                inherited = self.interface_results.get((base, signature))
                if inherited:
                    break
        if inherited:
            result_name, fields, base_types = inherited
            if types != base_types:
                raise ModelError(f"Java: incompatible output result in override: {qualified}")
            # Parameter renaming does not change the inherited Java result API.
            # Each wrapper still marshals using its own native parameter policies.
        else:
            result_name = (self.short(owner) + "_" if owner else "") + name + "Result"
            if result_name in self.names:
                raise ModelError(f"Java: result name collision: {result_name}")
            self.names[result_name] = qualified
            members = [f"public {info[0]} {field};" for field, info in zip(fields, types)]
            self.java.append(f"    public static final class {result_name} {{\n{self.indent(members, 8)}\n        private {result_name}() {{}}\n    }}\n")
        if owner:
            self.interface_results[(owner, signature)] = result_name, fields, types
        return (result_name, "jobject", "L" + self.path + "$" + result_name + ";", "Object"), fields

    def emit_function(self, fn, owner=None):
        name = self.short(fn["name"])
        qualified = owner + "::" + name if owner else fn["name"]
        if qualified in self.scopes:
            return self.emit_scoped_bytes(fn, owner, self.scopes[qualified])
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
            lines += [f"Owned<{owner}> SelfNative;", f'ReadInterface(Env, Self, SelfNative, "{self.state_signature}", false, {self.interface_casts[owner]});']
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

        computed_counts = {}

        def length(p, output=False):
            policy = p.get("policy", {})
            if "length_from" in policy:
                return computed_counts[p["name"]]
            if "length_method" in policy:
                return "ResultCount"
            spec = policy.get("written" if output and "written" in policy else "length")
            if isinstance(spec, int):
                return str(spec)
            if isinstance(spec, str) and spec in locals_:
                return locals_[spec]
            raise ModelError(f"Java: array count must name a scalar parameter: {qualified}")

        # Retain and convert every input before querying computed output sizes.
        # A count source may follow its output buffer in the native signature.
        remaining = sorted(fn["parameters"], key=lambda p: p.get("policy", {}).get("direction") == "out")
        for p in remaining:
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
                    query = p.get("policy", {}).get("length_from")
                    if query:
                        _, source, member = self.resolve_output_length(query, fn["parameters"], qualified + "::" + p["name"])
                        access = "->" if unalias(source["type"])["kind"] == "pointer" else "."
                        count = f'({locals_[source["name"]]}){access}{member}'
                        # Reuse this pre-call count for output conversion, even if
                        # the native operation changes the source's descriptor.
                        computed_counts[p["name"]] = var + "Count"
                    else:
                        count = length(p)
                    lines += [f"const jsize {var}Count = ArrayCount(Env, {count});",
                              f"std::unique_ptr<{self.array_native_type(t)}[]> {var}(new {self.array_native_type(t)}[static_cast<size_t>({var}Count)]{{}});"]
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
                if kind == "string":
                    # Nullability describes the caller's output slot, not its
                    # returned pointer. A string output may be null on success
                    # or failure, just like an interface output.
                    output = dict(output, policy=dict(output.get("policy", {}), nullable=True))
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
        count_method = result.get("policy", {}).get("length_method")
        if count_method is not None:
            if result_kind != "array":
                raise ModelError(f"Java: length_method requires an array result: {qualified}")
            method = self.result_length_method(owner, count_method, qualified)
            # The owner stays retained until both its pointer and count have been
            # queried and the borrowed data has been copied to Java.
            lines.append(f'const jsize ResultCount = ArrayCount(Env, SelfNative.Ptr->{method["name"]}());')
        lines.append("Transfers Pending{Env};")
        returns = ([] if return_expr is None else [(dict(result, name="returnValue"), return_expr, "ReturnOwner" if result_kind == "interface" else None)]) + outputs
        if outputs:
            result_info, result_fields = self.emit_result_class(fn, owner, inputs, returns)
            lines.append(f'jobject Result = NewObject(Env, "{self.path}${result_info[0]}");')
        for index, (p, expr, out_owner) in enumerate(returns):
            if self.shape(p)[0] == "array" and "written" in p.get("policy", {}):
                lines.append(f'Require(Env, {length(p, True)} <= {length(p)}, "Native written count exceeds capacity");')
            lines_out, converted = self.write(p, expr, "Output" + str(index), length(p, True) if self.shape(p)[0] == "array" else None, out_owner)
            lines += lines_out
            if outputs:
                _, _, sig, suffix = self.info(p)
                lines += [f'const jfieldID OutputField{index} = Field(Env, Result, "{result_fields[index]}", "{sig}");',
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


    def emit_scoped_bytes(self, fn, owner, spec):
        name = self.short(fn["name"])
        scope_class = "WriteByteScope" if spec["writable"] else "ReadByteScope"
        scope_info = (scope_class, "jobject", "L" + self.path + "$" + scope_class + ";", "Object")
        returns = [(dict(fn["result"], name="returnValue"), None, None), (fn["parameters"][0], None, None)]
        result, fields = self.emit_result_class(fn, owner, [], returns, [SCALARS["i32"], scope_info])
        writable = str(spec["writable"]).lower()
        data_type = "void*" if spec["writable"] else "const void*"
        lines = [f"Owned<{owner}> SelfNative;",
                 f'ReadInterface(Env, Self, SelfNative, "{self.state_signature}", false, {self.interface_casts[owner]});',
                 "std::unique_ptr<ByteScope> Scope{new ByteScope{SelfNative.Ptr,",
                 f'    [](void* Owner) -> std::int32_t {{ return static_cast<{owner}*>(Owner)->{spec["release"]}(); }},',
                 f"    [](void* Owner) {{ static_cast<{owner}*>(Owner)->Release(); }}, {writable}" + "}};",
                 f"{owner}* Owner = SelfNative.Ptr;", "SelfNative.Ptr = nullptr;",
                 f"{data_type} Data = nullptr;", f"const jint Status = Owner->{name}(&Data);",
                 f'Scope->Acquired = Status == {spec["success"]};',
                 f'jobject Result = NewObject(Env, "{self.path}${result[0]}");',
                 f'Env->SetIntField(Result, Field(Env, Result, "{fields[0]}", "I"), Status);', "Check(Env);",
                 "if (Scope->Acquired)", "{",
                 f'    Scope->SetData(Env, Data, Owner->{spec["size"]}());',
                 f'    const LocalRef<jobject> Access{{Env, NewObject(Env, "{self.path}${scope_class}")}};',
                 '    const jfieldID PointerField = Field(Env, Access.Get(), "pointer", "J");',
                 f'    Env->SetObjectField(Result, Field(Env, Result, "{fields[1]}", "{scope_info[2]}"), Access.Get());',
                 "    Check(Env);",
                 "    // Transfer only after validation and Java result construction succeed.",
                 "    Env->SetLongField(Access.Get(), PointerField, static_cast<jlong>(reinterpret_cast<std::uintptr_t>(Scope.get())));",
                 "    Check(Env);", "    Scope.release();", "}",
                 "return Result;"]
        native = self.native_method(result, [(self.short(owner), "jobject", "Self")], lines)
        return f"public {result[0]} {name}() {{ return {native}(this); }}"

    def emit_byte_scope_runtime(self):
        pointer = [("long", "jlong", "Pointer")]
        scope = "ByteScope& Scope = *reinterpret_cast<ByteScope*>(static_cast<std::uintptr_t>(Pointer));"
        size = self.native_method(SCALARS["i64"], pointer, [scope, "return static_cast<jlong>(Scope.Size);"])
        read = self.native_method(SCALARS["i32"], pointer + [("long", "jlong", "Offset")],
                                 [scope, "Scope.CheckRange(Env, Offset, 1);", "return Scope.Data[static_cast<size_t>(Offset)];"])
        write = self.native_method(SCALARS["void"], pointer + [("long", "jlong", "Offset"), ("int", "jint", "Value")],
                                  [scope, 'Require(Env, Scope.Writable, "Byte scope is read-only");',
                                   'Require(Env, Value >= 0 && Value <= 255, "Expected an unsigned byte value");',
                                   "Scope.CheckRange(Env, Offset, 1);",
                                   "const_cast<unsigned char*>(Scope.Data)[static_cast<size_t>(Offset)] = static_cast<unsigned char>(Value);"])
        copy = self.native_method(SCALARS["void"], pointer + [("byte[]", "jbyteArray", "Bytes"), ("long", "jlong", "Offset"), ("boolean", "jboolean", "Write")],
                                 [scope, 'Require(Env, Bytes != nullptr, "Byte array is null");',
                                  'Require(Env, !Write || Scope.Writable, "Byte scope is read-only");',
                                  "const jsize Count = Env->GetArrayLength(Bytes);", "Check(Env);",
                                  "Scope.CheckRange(Env, Offset, static_cast<size_t>(Count));", "if (Count != 0)", "{",
                                  "    const unsigned char* Data = Scope.Data + static_cast<size_t>(Offset);",
                                  "    if (Write)", "        Env->GetByteArrayRegion(Bytes, 0, Count, reinterpret_cast<jbyte*>(const_cast<unsigned char*>(Data)));",
                                  "    else", "        Env->SetByteArrayRegion(Bytes, 0, Count, reinterpret_cast<const jbyte*>(Data));", "    Check(Env);", "}"])
        release = self.native_method(SCALARS["i32"], pointer,
                                     ["const std::unique_ptr<ByteScope> Scope{reinterpret_cast<ByteScope*>(static_cast<std::uintptr_t>(Pointer))};",
                                      "return Scope->Release();"])
        self.java.append(f'''    /** Explicitly closed native access; no native address or buffer view is exposed. */
    public static class ReadByteScope implements java.lang.AutoCloseable {{
        private long pointer;
        private int releaseStatus;
        private ReadByteScope() {{}}
        private long requireOpen() {{
            if (pointer == 0) throw new java.lang.IllegalStateException("Byte scope is closed");
            return pointer;
        }}
        public final synchronized boolean isClosed() {{ return pointer == 0; }}
        public final synchronized long byteLength() {{ return {size}(requireOpen()); }}
        /** Returns an unsigned byte in the range 0..255. */
        public final synchronized int readByte(long index) {{ return {read}(requireOpen(), index); }}
        public final void copyTo(byte[] destination) {{ copyTo(destination, 0); }}
        public final synchronized void copyTo(byte[] destination, long sourceOffset) {{
            {copy}(requireOpen(), destination, sourceOffset, false);
        }}
        public final synchronized byte[] toByteArray() {{
            long length = byteLength();
            if (length > java.lang.Integer.MAX_VALUE) throw new java.lang.IllegalArgumentException("Byte scope is too large for a Java array");
            byte[] bytes = new byte[(int)length];
            copyTo(bytes);
            return bytes;
        }}
        /** Consumes the scope and returns the native release status, even on failure. */
        public final synchronized int release() {{
            if (pointer != 0) {{
                long value = pointer;
                pointer = 0;
                releaseStatus = {release}(value);
            }}
            return releaseStatus;
        }}
        @java.lang.Override public final void close() {{ release(); }}
    }}

    public static final class WriteByteScope extends ReadByteScope {{
        private WriteByteScope() {{}}
        public synchronized void writeByte(long index, int value) {{
            {write}(((ReadByteScope)this).requireOpen(), index, value);
        }}
        public void copyFrom(byte[] source) {{ copyFrom(source, 0); }}
        public synchronized void copyFrom(byte[] source, long destinationOffset) {{
            {copy}(((ReadByteScope)this).requireOpen(), source, destinationOffset, true);
        }}
    }}
''')

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

    def constant_helper(self, result, parameters, lines):
        while True:
            name = "constant" + str(self.constant_counter)
            self.constant_counter += 1
            if name not in self.names:
                break
        self.names[name] = "constant initializer"
        self.java.append(f"    private static {result} {name}({parameters}) {{\n{self.indent(lines, 8)}\n    }}\n")
        return name

    def constant_aggregate(self, java, allocation, assignments):
        # Java initializes arrays to zero. Avoid emitting an assignment for each
        # zero in a potentially large table, but preserve negative floating zero.
        if not assignments:
            return allocation
        # Aggregate expressions refer to child factories, so each assignment has
        # bounded bytecode. Bound both leaf helpers and their callers: even large
        # arrays of descriptors must stay below Java's 64 KiB method limit.
        batch_size = 128
        while len(assignments) > batch_size:
            calls = []
            for offset in range(0, len(assignments), batch_size):
                helper = self.constant_helper("void", java + " Value", assignments[offset:offset + batch_size])
                calls.append(helper + "(Value);")
            assignments = calls
        factory = self.constant_helper(java, "", [f"{java} Value = {allocation};", *assignments, "return Value;"])
        return factory + "()"

    def constant_value(self, declaration, value):
        """Build independent Java values from the language-neutral constant model."""
        t = unalias(declaration["type"])
        if t.get("volatile"):
            raise ModelError("Java: volatile constants require an adapter")
        if t["kind"] in ("array", "pointer"):
            element = unalias(t["element"] if t["kind"] == "array" else t["target"])
            if element.get("volatile"):
                raise ModelError("Java: volatile constants require an adapter")
            ordinary_char = element.get("name") in ("char_s", "char_u")
            if ordinary_char and t["kind"] == "pointer" and element["const"]:
                if isinstance(value, str):
                    return "java.lang.String", json.dumps(value)
                if value is None:
                    return "java.lang.String", "null"
            if (t["kind"] == "array" and isinstance(value, str) and
                    element.get("name") in ("char_s", "char_u", "schar", "uchar")):
                # Only standalone plain-char arrays represent text. Explicit
                # signed/unsigned byte arrays (including Int8/Uint8 aliases) and
                # all descriptor array fields retain their numeric array type.
                if ordinary_char and declaration.get("kind") == "constant":
                    return "java.lang.String", json.dumps(value)
                value = list(value.encode("utf-8"))
                value += [0] * max(0, t["count"] - len(value))
                if self.scalar(element) == "i8":
                    value = [byte if byte < 128 else byte - 256 for byte in value]
        info = self.scalar_info(t)
        if info and info[0] != "void":
            return info[0], self.scalar_literal(t, value)
        if t["kind"] == "array":
            if (not isinstance(value, list) or len(value) != t["count"] or
                    not 0 < t["count"] <= 0x7fffffff):
                raise ModelError("Java: constant array values must match their declared extent and fit jsize")
            elements = [self.constant_value({"type": t["element"]}, item) for item in value]
            java = elements[0][0] + "[]"
            # Insert the outer extent before any inner unsized dimensions.
            allocation = "new " + java.replace("[]", f"[{t['count']}]", 1)
            default_literals = {"0", "0L", "false", "null", "0x0.0p+0", "0x0.0p+0f"}
            assignments = [f"Value[{index}] = {expr};" for index, (_, expr) in enumerate(elements)
                           if expr not in default_literals]
            return java, self.constant_aggregate(java, allocation, assignments)
        if t["kind"] == "pointer" and value is None:
            # Null pointer fields carry no native storage or ownership. Preserve
            # their declared Java type, including nullable arrays and descriptors.
            return self.info(declaration)[0], "null"
        if self.types.get(t.get("name"), {}).get("kind") == "record":
            fields = self.record_fields(t["name"])
            if not isinstance(value, dict) or value.keys() != {f["name"] for f in fields}:
                raise ModelError("Java: constant descriptor values must include every inherited and direct field")
            java = self.short(t["name"])
            assignments = []
            for field in fields:
                _, expr = self.constant_value(field, value[field["name"]])
                assignments.append(f"Value.{identifier(field['name'])} = {expr};")
            # Skip the native-default constructor: the extracted constant already
            # contains all defaults, and every mutable child is rebuilt above.
            return java, self.constant_aggregate(java, f"new {java}(false)", assignments)
        raise ModelError("Java: unsupported constant type; native addresses require an adapter")

    def emit_constant(self, d):
        if d.get("policy", {}).get("binding", "automatic") != "automatic":
            raise ModelError(f"Java: manual constant requires an adapter: {d['name']}")
        java, expr = self.constant_value(d, d["value"])
        name = self.short(d["name"])
        if java == "java.lang.String" or self.scalar_info(d["type"]):
            self.java.append(f"    public static final {java} {name} = {expr};\n")
        else:
            # Java final protects the reference, not the contents. An accessor
            # avoids exposing shared mutable descriptor or array constants.
            self.java.append(f"    /** Returns an independent snapshot of the native constant. */\n"
                             f"    public static {java} {name}() {{ return {expr}; }}\n")

    def emit_interface_casts(self):
        # State.kind identifies the pointer's original interface type. Use C++
        # conversions for inherited calls and derived arguments; never reinterpret
        # a derived pointer directly as a base interface.
        for target, cast in self.interface_casts.items():
            cases = []
            for index, d in enumerate(self.interfaces):
                if target in self.interface_ancestors[d["name"]]:
                    cases.append(f"case {index}: return static_cast<{target}*>(reinterpret_cast<{d['name']}*>(Pointer));")
            self.cpp.append(f'''[[maybe_unused]] static inline {target}* {cast}(std::uintptr_t Pointer, jint Kind)
{{
    switch (Kind)
    {{
{self.indent(cases, 8)}
        default: return nullptr;
    }}
}}
''')

    def emit_interface_query(self, d):
        name = d["name"]
        root = self.interface_ancestors[name][-1]
        lines = [f"Owned<{root}> Source;",
                 f'ReadInterface(Env, Self, Source, "{self.state_signature}", false, {self.interface_casts[root]});',
                 f"Owned<{root}> Queried;",
                 f"Source.Ptr->QueryInterface({d['policy']['iid']}, &Queried.Ptr);",
                 f"Owned<{name}> ResultOwner;",
                 f"ResultOwner.Ptr = static_cast<{name}*>(Queried.Ptr);",
                 "Queried.Ptr = nullptr;",
                 "Transfers Pending{Env};",
                 f'jobject Result = Pending.Wrap(ResultOwner, "{self.path}${self.short(name)}", "{self.state_signature}");',
                 "Pending.Commit();", "return Result;"]
        native = self.native_method((self.short(name), "jobject", "", "Object"), [("NativeObject", "jobject", "Self")], lines)
        return f"if (type == {self.short(name)}.class) return type.cast({native}(this));"

    def generate(self):
        includes = "\n".join('#include "' + h + '"' for h in self.headers)
        self.cpp.append('// Generated; do not edit.\n#include "JNIRuntime.hpp"\n#include <algorithm>\n#include <array>\n' + includes + '\nusing namespace DiligentBindings;\n')
        self.emit_interface_casts()
        if self.scopes:
            self.emit_byte_scope_runtime()
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
        visited, visiting = set(), set()
        def record(d):
            if d["name"] in visited:
                return
            if d["name"] in visiting:
                raise ModelError(f"Java: recursive descriptor storage requires an adapter: {d['name']}")
            visiting.add(d["name"])
            for base in d.get("bases", []):
                record(self.types[base])
            for f in self.record_fields(d["name"]):
                kind, t = self.shape(f)
                while kind in ("fixed_array", "array"):
                    kind, t = self.shape({"type": t["element"] if kind == "fixed_array" else t})
                if kind == "record":
                    record(self.types[t["name"]])
            self.emit_record(d)
            visiting.remove(d["name"])
            visited.add(d["name"])
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
                self.emit_constant(d)
            elif d["kind"] == "function":
                self.java.append("    " + self.emit_function(d) + "\n")
        queries = []
        bases = {d["bases"][0] for d in self.interfaces}
        interface_indices = {d["name"]: index for index, d in enumerate(self.interfaces)}
        emitted = set()
        def interface(d):
            if d["name"] in emitted:
                return
            base = d["bases"][0]
            if self.types[base]["kind"] == "interface":
                interface(self.types[base])
            # Emit base methods first so overrides can reuse their result classes.
            methods = [self.emit_function(m, d["name"]) for m in d["methods"]
                       if d["name"] + "::" + m["name"] not in self.releases]
            short = self.short(d["name"])
            index = interface_indices[d["name"]]
            parent = self.short(base) if self.types[base]["kind"] == "interface" else "NativeObject"
            final = "" if d["name"] in bases else "final "
            self.java.append(f"    public static {final}class {short} extends {parent} {{\n        private {short}() {{ super({index}); }}\n        private {short}(int kind) {{ super(kind); }}\n{self.indent(methods, 8)}\n    }}\n")
            if d.get("policy", {}).get("iid"):
                queries.append(self.emit_interface_query(d))
            emitted.add(d["name"])
        for d in self.interfaces:
            interface(d)
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
        public final <T extends NativeObject> T QueryInterface(java.lang.Class<T> type) {{
            java.util.Objects.requireNonNull(type, "type");
{self.indent(queries, 12)}
            throw new java.lang.IllegalArgumentException("QueryInterface requires a generated interface class with an IID");
        }}
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
    parser.add_argument("--surface", type=Path, help="Write the compact binding contract for API baseline checks")
    args = parser.parse_args()
    try:
        model = json.loads(args.model.read_text(encoding="utf-8"))
        adapters = load_config(args.adapters) if args.adapters else None
        generator = Generator(model, args.module, args.package, args.library, args.header, adapters)
        java, cpp = generator.generate()
        if args.surface:
            from api_surface import build_surface, format_surface
            write_if_changed(args.surface, format_surface(build_surface(generator, target="native")))
        write_if_changed(args.output / (args.module + ".java"), java)
        write_if_changed(args.output / (args.module + ".cpp"), cpp)
    except (ModelError, OSError, ValueError) as error:
        parser.exit(1, f"Java binding generation failed: {error}\n")
    print(f"Generated {args.module} Java/JNI bindings")


if __name__ == "__main__":
    main()
