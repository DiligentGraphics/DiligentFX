"""Binding policies over the language-neutral API model; independent of Clang."""


class ModelError(ValueError):
    pass


def unalias(native_type):
    const = native_type["const"]
    volatile = native_type["volatile"]
    while native_type["kind"] == "alias":
        native_type = native_type["target"]
        const |= native_type["const"]
        volatile |= native_type["volatile"]
    return dict(native_type, const=const, volatile=volatile)


def validate_keys(policy, allowed, context):
    if not isinstance(policy, dict):
        raise ModelError(f"{context}: expected a metadata object")
    unknown = policy.keys() - allowed
    if unknown:
        raise ModelError(f"{context}: unknown metadata keys: {', '.join(sorted(unknown))}")


def validate_binding(policy, context):
    if not isinstance(policy, dict):
        raise ModelError(f"{context}: expected a metadata object")
    binding = policy.get("binding", "automatic")
    if binding not in ("automatic", "manual", "external"):
        raise ModelError(f"{context}: unknown binding mode '{binding}'")
    if binding != "automatic" and not policy.get("reason"):
        raise ModelError(f"{context}: {binding} binding requires a reason")
    return binding


VALUE_KEYS = {"binding", "reason", "direction", "nullable", "ownership", "lifetime", "length", "written", "element_nullable"}


def apply_value_policy(value, policy, context, *, is_result=False, is_field=False):
    validate_keys(policy, VALUE_KEYS, context)
    binding = validate_binding(policy, context)
    native_type = unalias(value["type"])
    if binding == "external":
        raise ModelError(f"{context}: external binding is only valid for declarations")
    if native_type["kind"] in ("pointer", "reference") and binding == "automatic":
        required = {"direction", "nullable", "ownership", "lifetime"}
        missing = required - policy.keys()
        if missing:
            raise ModelError(f"{context}: missing pointer metadata: {', '.join(sorted(missing))}")
    if "direction" in policy and policy["direction"] not in ("in", "out", "inout"):
        raise ModelError(f"{context}: invalid direction")
    if "nullable" in policy and not isinstance(policy["nullable"], bool):
        raise ModelError(f"{context}: nullable must be a boolean")
    if "element_nullable" in policy and (not isinstance(policy["element_nullable"], bool) or "length" not in policy):
        raise ModelError(f"{context}: element_nullable requires a span and a boolean value")
    if "ownership" in policy and policy["ownership"] not in ("borrowed", "owned"):
        raise ModelError(f"{context}: invalid ownership")
    if "lifetime" in policy and not policy["lifetime"]:
        raise ModelError(f"{context}: lifetime must not be empty")
    # Returned pointers and pointer-valued fields may refer to const data.
    # Only output parameters require caller-provided writable pointee storage.
    if (not (is_result or is_field) and policy.get("direction") in ("out", "inout") and
            native_type["kind"] in ("pointer", "reference") and native_type["target"]["const"]):
        raise ModelError(f"{context}: output cannot point to const data")
    if policy:
        value["policy"] = policy


def validate_length_policy(value, siblings, context):
    policy = value.get("policy", {})
    if "written" in policy:
        count = siblings.get(policy["written"], {})
        count_type = unalias(count["type"]) if count else {}
        inner = unalias(count_type["target"]) if count_type.get("kind") in ("pointer", "reference") else {}
        if ("length" not in policy or policy.get("direction") != "out" or
                count.get("policy", {}).get("direction") != "out" or
                inner.get("kind") != "scalar" or inner.get("name") not in ("uint", "ulong", "ulonglong")):
            raise ModelError(f"{context}: written must name an unsigned scalar output")
    if "length" in policy:
        length = siblings.get(policy["length"])
        length_type = unalias(length["type"]) if length else {}
        if (length_type.get("kind") != "scalar" or
                length_type["name"] in ("bool", "float", "double")):
            raise ModelError(f"{context}: length must name an integer sibling")


def apply_values_policy(values, policies, context, *, is_field=False, inherited=()):
    by_name = {value["name"]: value for value in (*inherited, *values)}
    validate_keys(policies, {value["name"] for value in values}, context + " members")
    for value in values:
        policy = policies.get(value["name"], {})
        apply_value_policy(value, policy, f"{context}::{value['name']}", is_field=is_field)
    for value in values:
        validate_length_policy(value, by_name, f"{context}::{value['name']}")


def apply_callable_policy(callable_model, policy, context):
    validate_keys(policy, {"binding", "reason", "parameters", "result"}, context)
    validate_binding(policy, context)
    apply_values_policy(callable_model["parameters"], policy.get("parameters", {}), context)
    apply_value_policy(callable_model["result"], policy.get("result", {}), context + " result", is_result=True)
    validate_length_policy(callable_model["result"],
                           {p["name"]: p for p in callable_model["parameters"]}, context + " result")
    callable_model["policy"] = {key: value for key, value in policy.items()
                                if key not in ("parameters", "result")}


def referenced_types(value):
    if isinstance(value, dict):
        if value.get("kind") == "named":
            yield value["name"]
        for child in value.values():
            yield from referenced_types(child)
    elif isinstance(value, list):
        for child in value:
            yield from referenced_types(child)


def apply_declaration_policy(declaration, policy, inherited=()):
    name = declaration["name"]
    kind = declaration["kind"]
    if kind == "external":
        validate_keys(policy, {"binding", "reason", "ownership"}, name)
    elif kind == "enum":
        validate_keys(policy, {"binding", "reason", "role"}, name)
    elif kind in ("alias", "constant"):
        validate_keys(policy, {"binding", "reason"}, name)
    elif kind in ("record", "interface"):
        validate_keys(policy, {"binding", "reason", "ownership", "iid", "fields", "methods"}, name)
        apply_values_policy(declaration["fields"], policy.get("fields", {}), name, is_field=True, inherited=inherited)
        methods = declaration["methods"]
        method_policies = policy.get("methods", {})
        validate_keys(method_policies, {method["name"] for method in methods}, name + " methods")
        for method in methods:
            apply_callable_policy(method, method_policies.get(method["name"], {}), name + "::" + method["name"])
    elif kind == "function":
        apply_callable_policy(declaration, policy, name)
        return
    else:
        raise ModelError(f"{name}: unsupported declaration kind '{kind}'")
    binding = validate_binding(policy, name)
    if (binding == "external") != (kind == "external"):
        raise ModelError(f"{name}: external policy does not match the extracted declaration")
    declaration["policy"] = {key: value for key, value in policy.items() if key not in ("fields", "methods")}


def apply_policies(model, policies, symbols):
    """Validate and attach policies to a freshly extracted or JSON-loaded model.

    Mutates the supplied model. The result contains only JSON-compatible data,
    so emitters and external generator adapters do not need Clang objects.
    """
    if model.get("schema_version") != 1:
        raise ModelError("Unsupported model schema_version")
    declarations = model["declarations"]
    names = {declaration["name"] for declaration in declarations}
    if names != policies.keys():
        raise ModelError("Policies must match the extracted declaration selection")
    by_name = {declaration["name"]: declaration for declaration in declarations}

    def base_fields(declaration):
        fields = []
        for base in declaration.get("bases", []):
            parent = by_name.get(base, {})
            fields.extend(base_fields(parent))
            fields.extend(parent.get("fields", []))
        return fields

    for declaration in declarations:
        apply_declaration_policy(declaration, policies[declaration["name"]], base_fields(declaration))
        dependencies = set(referenced_types(declaration)) | set(declaration.get("bases", []))
        missing = dependencies - names
        if missing:
            raise ModelError(f"{declaration['name']}: types need explicit policies: {', '.join(sorted(missing))}")
        iid = declaration["policy"].get("iid")
        if iid and iid not in symbols:
            raise ModelError(f"{declaration['name']}: interface ID not found: {iid}")
    return model
