"""API selection, adapter contracts, and native type helpers for language backends."""

from copy import deepcopy
import re

from binding_model import ModelError, apply_values_policy, unalias


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
    raise ModelError(f"Unsupported native type {t['kind']}")


class BindingAPI:
    """A private, adapted view of a validated model, independent of its emitter.

    Backends share selection and adapter validation while supplying their own
    language types, call glue, storage, and runtime behavior.
    """

    def __init__(self, model, adapters=None):
        if model.get("schema_version") != 1:
            raise ModelError("Unsupported binding model schema_version")
        self.conventions = model.get("conventions")
        # Backend selection and policies must not modify the language-neutral model.
        self.types = {d["name"]: deepcopy(d) for d in model["declarations"]}
        if adapters and (not isinstance(adapters, dict) or adapters.get("schema_version") != 1 or
                         adapters.keys() - {"schema_version", "scoped_bytes", "native_only", "field_policies", "fixed_arguments", "field_count_functions"}):
            raise ModelError("Invalid binding adapter configuration")
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

    def configure_selection(self, field_policies):
        """Apply backend selections without modifying the source model."""
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
        raise ModelError(f"Binding API: expected a scalar, got {t['kind']}")

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
