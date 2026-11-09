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


VALUE_KEYS = {"binding", "reason", "direction", "nullable", "ownership", "lifetime", "length", "written", "element_nullable", "length_method", "length_from"}


def validate_method_selection(names, context):
    if (not isinstance(names, list) or any(not isinstance(name, str) or not name for name in names) or
            len(set(names)) != len(names)):
        raise ModelError(f"{context}: include_methods must be a list of unique method names")


def inferred_value_policy(value, *, is_result=False, is_field=False, interfaces=()):
    """Derive ordinary value-reference marshalling from the native type.

    References are non-null and do not transfer ownership of their storage.
    Value references are marshalled during the call (results are snapshots).
    An API retaining an input reference must explicitly override that lifetime.
    Mutable value arguments default to outputs; interface arguments are inputs.
    Inout and mutable input storage require overrides. Record-usage analysis
    supplies field directions; API conventions can supply pointer contracts.
    """
    native_type = unalias(value["type"])
    if native_type["kind"] not in ("pointer", "reference"):
        return {}
    target = unalias(native_type["target"])
    policy = {}
    if is_result:
        policy["direction"] = "out"
    elif not is_field:
        # Inspect the immediate pointee/referent, after resolving typedefs.
        # const char** writes a pointer, while char* const* reads pointers.
        is_interface = target["kind"] == "named" and target["name"] in interfaces
        policy["direction"] = "in" if target["const"] or is_interface else "out"
    if native_type["kind"] == "reference":
        policy["nullable"] = False
        if target["kind"] not in ("pointer", "reference", "function"):
            policy.update(ownership="borrowed", lifetime="call")
    return policy


def record_usage(types, policies=None):
    """Propagate input/output use through records, bases, arrays, and pointers.

    Descriptor fields follow the enclosing descriptor's marshalling direction;
    indirect writable buffers can override that convention. The same analysis is
    used before policy validation and by emitters consuming a resolved model.
    """
    policies = policies or {}
    inputs, outputs, storage = set(), set(), set()

    def include(native, used):
        native = unalias(native)
        while native["kind"] in ("reference", "pointer", "array"):
            native = unalias(native["element"] if native["kind"] == "array" else native["target"])
        declaration = types.get(native.get("name"), {})
        if declaration.get("kind") != "record" or declaration["name"] in used:
            return
        used.add(declaration["name"])
        for base in declaration["bases"]:
            include({"kind": "named", "name": base, "const": False, "volatile": False}, used)
        for field in declaration["fields"]:
            include(field["type"], used)

    for name, declaration in types.items():
        policy = policies.get(name, {})
        if declaration["kind"] == "constant":
            include(declaration["type"], outputs)
        callables = [declaration] if declaration["kind"] == "function" else declaration.get("methods", [])
        for function in callables:
            if declaration["kind"] == "function":
                overrides = policy
            else:
                if "include_methods" in policy and function["name"] not in policy["include_methods"]:
                    continue
                overrides = policy.get("methods", {}).get(function["name"], {})
            include(function["result"]["type"], outputs)
            for parameter in function["parameters"]:
                effective = dict(inferred_value_policy(parameter), **parameter.get("policy", {}))
                effective.update(overrides.get("parameters", {}).get(parameter["name"], {}))
                direction = effective.get("direction", "in")
                if direction != "out":
                    include(parameter["type"], inputs)
                if direction in ("out", "inout"):
                    include(parameter["type"], outputs)
                    native = unalias(parameter["type"])
                    target = unalias(native["target"]) if native["kind"] in ("pointer", "reference") else {}
                    if types.get(target.get("name"), {}).get("kind") == "record":
                        storage.add(target["name"])
    # Standalone descriptors still need constructors and input marshalling.
    for name, declaration in types.items():
        if declaration["kind"] == "record" and name not in outputs:
            include({"kind": "named", "name": name, "const": False, "volatile": False}, inputs)
    return inputs, outputs, inputs | storage


def pointer_defaults(value, policy, *, is_result=False, is_field=False, interfaces=()):
    """Diligent conventions, enabled once per API and overridden per value.

    These are API contracts, not C++ ownership guarantees. Ordinary pointers
    borrow call-scoped storage. Interface output slots transfer a strong reference,
    while direct interface returns borrow one from the object. Optional pointers,
    retained storage, and exceptions to those conventions need explicit metadata.
    """
    native = unalias(value["type"])
    if native["kind"] not in ("pointer", "reference"):
        return {}
    target = unalias(native["target"])
    if target["kind"] == "function":
        return {}  # Callbacks still require an adapter contract.
    interface_result = is_result and target.get("name") in interfaces
    slot = unalias(target["target"]) if target["kind"] == "pointer" else {}
    owned_output = (not (is_result or is_field) and policy.get("direction") == "out" and
                    slot.get("name") in interfaces)
    ownership = policy.get("ownership", "owned" if owned_output else "borrowed")
    lifetime = "caller" if ownership == "owned" else "object" if interface_result else "call"
    return {"nullable": False, "ownership": ownership, "lifetime": lifetime}


def apply_value_policy(value, policy, context, *, is_result=False, is_field=False, interfaces=(), conventions=None, field_direction=None):
    validate_keys(policy, VALUE_KEYS, context)
    binding = validate_binding(policy, context)
    native_type = unalias(value["type"])
    if is_field and field_direction and native_type["kind"] in ("pointer", "reference"):
        policy = dict({"direction": field_direction}, **policy)
    if binding == "automatic":
        policy = dict(inferred_value_policy(value, is_result=is_result, is_field=is_field, interfaces=interfaces), **policy)
        if conventions == "diligent":
            policy = dict(pointer_defaults(value, policy, is_result=is_result, is_field=is_field, interfaces=interfaces), **policy)
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
    if "length_from" in policy:
        source = policy["length_from"]
        if (is_result or is_field or native_type["kind"] != "pointer" or
                policy.get("direction") != "out" or policy.get("ownership") != "borrowed" or
                policy.get("lifetime") != "call" or policy.get("nullable") is not False or
                {"length", "length_method", "written"} & policy.keys() or
                not isinstance(source, dict) or not {"parameter", "method"} <= source.keys() or
                source.keys() - {"parameter", "method", "field"} or
                any(not isinstance(v, str) or not v for v in source.values())):
            raise ModelError(f"{context}: length_from requires a borrowed output buffer and a parameter/method query with an optional field")
    if "length_method" in policy and (not is_result or "length" in policy or
            not isinstance(policy["length_method"], str) or not policy["length_method"]):
        raise ModelError(f"{context}: length_method requires a result and cannot be combined with length")
    if "element_nullable" in policy and (not isinstance(policy["element_nullable"], bool) or not ({"length", "length_method"} & policy.keys())):
        raise ModelError(f"{context}: element_nullable requires a span and a boolean value")
    if "ownership" in policy and policy["ownership"] not in ("borrowed", "owned"):
        raise ModelError(f"{context}: invalid ownership")
    if "lifetime" in policy and not policy["lifetime"]:
        raise ModelError(f"{context}: lifetime must not be empty")
    # Returned pointers and pointer-valued fields may refer to const data.
    # Only output parameters require caller-provided writable pointee storage.
    if (not (is_result or is_field) and policy.get("direction") in ("out", "inout") and
            native_type["kind"] in ("pointer", "reference") and unalias(native_type["target"])["const"]):
        raise ModelError(f"{context}: output cannot point to const data")
    if policy:
        value["policy"] = policy


def resolve_span_length(length, siblings, context, types=None, *, field=False):
    """Resolve a sibling count, a nested value-field count, or a fixed field extent."""
    if field and type(length) is int and 0 <= length <= 0xffffffff:
        return {"kind": "scalar", "name": "uint", "bits": 32, "const": False, "volatile": False}
    if not isinstance(length, str) or not length:
        raise ModelError(f"{context}: length must name an integer sibling")
    parts = length.split('.')
    if len(parts) > 1 and not field:
        raise ModelError(f"{context}: nested lengths require a descriptor field")
    members = siblings
    for index, part in enumerate(parts):
        value = members.get(part)
        native = unalias(value['type']) if value else {}
        if index + 1 < len(parts):
            declaration = (types or {}).get(native.get('name'), {})
            if native.get('kind') != 'named' or declaration.get('kind') != 'record':
                raise ModelError(f"{context}: length path must traverse embedded records")
            members = {f['name']: f for f in declaration['fields']}
    if native.get('kind') != 'scalar' or native['name'] in ('bool', 'float', 'double'):
        raise ModelError(f"{context}: length must name an integer sibling")
    return native


def validate_length_policy(value, siblings, context, types=None, *, is_field=False):
    policy = value.get("policy", {})
    if "length_from" in policy:
        source = siblings.get(policy["length_from"]["parameter"], {})
        source_type = unalias(source["type"]) if source else {}
        source_policy = source.get("policy", {})
        if (source_type.get("kind") not in ("pointer", "reference") or
                source_policy.get("direction") != "in" or source_policy.get("nullable") is not False or
                source_policy.get("ownership") != "borrowed" or source_policy.get("lifetime") != "call"):
            raise ModelError(f"{context}: length_from must query a non-null borrowed input parameter")
    if "written" in policy:
        count = siblings.get(policy["written"], {})
        count_type = unalias(count["type"]) if count else {}
        inner = unalias(count_type["target"]) if count_type.get("kind") in ("pointer", "reference") else {}
        if ("length" not in policy or policy.get("direction") != "out" or
                count.get("policy", {}).get("direction") != "out" or
                inner.get("kind") != "scalar" or inner.get("name") not in ("uint", "ulong", "ulonglong")):
            raise ModelError(f"{context}: written must name an unsigned scalar output")
    if "length" in policy:
        resolve_span_length(policy["length"], siblings, context, types, field=is_field)


def apply_values_policy(values, policies, context, *, is_field=False, inherited=(), types=None, interfaces=(), conventions=None, field_direction=None):
    by_name = {value["name"]: value for value in (*inherited, *values)}
    validate_keys(policies, {value["name"] for value in values}, context + " members")
    for value in values:
        policy = policies.get(value["name"], {})
        apply_value_policy(value, policy, f"{context}::{value['name']}", is_field=is_field, interfaces=interfaces, conventions=conventions, field_direction=field_direction)
    for value in values:
        validate_length_policy(value, by_name, f"{context}::{value['name']}", types, is_field=is_field)


def apply_callable_policy(callable_model, policy, context, interfaces=(), conventions=None):
    validate_keys(policy, {"binding", "reason", "parameters", "result"}, context)
    validate_binding(policy, context)
    apply_values_policy(callable_model["parameters"], policy.get("parameters", {}), context, interfaces=interfaces, conventions=conventions)
    apply_value_policy(callable_model["result"], policy.get("result", {}), context + " result", is_result=True, interfaces=interfaces, conventions=conventions)
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


def apply_declaration_policy(declaration, policy, inherited=(), types=None, interfaces=(), conventions=None, field_direction=None):
    name = declaration["name"]
    kind = declaration["kind"]
    if kind == "external":
        validate_keys(policy, {"binding", "reason", "ownership"}, name)
    elif kind == "enum":
        validate_keys(policy, {"binding", "reason", "role"}, name)
    elif kind in ("alias", "constant"):
        validate_keys(policy, {"binding", "reason"}, name)
    elif kind in ("record", "interface"):
        validate_keys(policy, {"binding", "reason", "ownership", "iid", "fields", "methods", "include_methods"}, name)
        if "include_methods" in policy:
            selection = policy["include_methods"]
            validate_method_selection(selection, name)
            if kind != "interface":
                raise ModelError(f"{name}: include_methods applies only to interfaces")
            missing = set(selection) - {method["name"] for method in declaration["methods"]}
            if missing:
                raise ModelError(f"{name}: unknown selected methods: {', '.join(sorted(missing))}")
            declaration["methods"] = [method for method in declaration["methods"] if method["name"] in selection]
        apply_values_policy(declaration["fields"], policy.get("fields", {}), name, is_field=True, inherited=inherited, types=types, interfaces=interfaces, conventions=conventions, field_direction=field_direction)
        methods = declaration["methods"]
        method_policies = policy.get("methods", {})
        validate_keys(method_policies, {method["name"] for method in methods}, name + " methods")
        for method in methods:
            apply_callable_policy(method, method_policies.get(method["name"], {}), name + "::" + method["name"], interfaces, conventions)
    elif kind == "function":
        apply_callable_policy(declaration, policy, name, interfaces, conventions)
        return
    else:
        raise ModelError(f"{name}: unsupported declaration kind '{kind}'")
    binding = validate_binding(policy, name)
    if (binding == "external") != (kind == "external"):
        raise ModelError(f"{name}: external policy does not match the extracted declaration")
    declaration["policy"] = {key: value for key, value in policy.items() if key not in ("fields", "methods")}


def apply_policies(model, policies, symbols, conventions=None):
    """Validate and attach policies to a freshly extracted or JSON-loaded model.

    Mutates the supplied model. The result contains only JSON-compatible data,
    so emitters and external generator adapters do not need Clang objects.
    """
    if model.get("schema_version") != 1:
        raise ModelError("Unsupported model schema_version")
    if conventions is None:
        conventions = model.get("conventions")
    if conventions not in (None, "diligent"):
        raise ModelError("Unsupported API conventions: " + str(conventions))
    if conventions:
        model["conventions"] = conventions
    for name, policy in policies.items():
        validate_binding(policy, name)
    declarations = model["declarations"]
    names = {declaration["name"] for declaration in declarations}
    unused = policies.keys() - names
    if unused:
        raise ModelError("Policies refer to declarations outside the selected API: " + ", ".join(sorted(unused)))
    by_name = {declaration["name"]: declaration for declaration in declarations}
    # Use declarations rather than interface-name prefixes. External interfaces
    # are identified by their reference-counting contract, before policy application
    # so inference does not depend on declaration order.
    interfaces = {name for name, declaration in by_name.items()
                  if declaration["kind"] == "interface" or
                  (declaration["kind"] == "external" and
                   policies.get(name, {}).get("ownership") == "reference_counted")}

    def base_fields(declaration):
        fields = []
        for base in declaration.get("bases", []):
            parent = by_name.get(base, {})
            fields.extend(base_fields(parent))
            fields.extend(parent.get("fields", []))
        return fields

    def reference_counted(name):
        return (policies.get(name, {}).get("ownership") == "reference_counted" or
                any(reference_counted(base) for base in by_name.get(name, {}).get("bases", [])))

    input_records, output_records, _ = record_usage(by_name, policies)
    for declaration in declarations:
        name = declaration["name"]
        policy = dict(policies.get(name, {}))
        if declaration["kind"] == "interface":
            if reference_counted(name):
                policy.setdefault("ownership", "reference_counted")
            # Diligent interfaces pair IFoo with the sibling IID_Foo constant.
            # Use only an actually declared symbol; other naming needs an override.
            namespace, separator, short_name = name.rpartition("::")
            iid = namespace + separator + "IID_" + short_name[1:]
            if short_name.startswith("I") and iid in symbols:
                policy.setdefault("iid", iid)
        field_direction = ("inout" if name in input_records and name in output_records else
                           "out" if name in output_records else "in")
        apply_declaration_policy(declaration, policy, base_fields(declaration), by_name, interfaces, conventions, field_direction)
        dependencies = set(referenced_types(declaration)) | set(declaration.get("bases", []))
        missing = dependencies - names
        if missing:
            raise ModelError(f"{declaration['name']}: referenced types are missing from the model: {', '.join(sorted(missing))}")
        iid = declaration["policy"].get("iid")
        if iid and iid not in symbols:
            raise ModelError(f"{declaration['name']}: interface ID not found: {iid}")
    return model
