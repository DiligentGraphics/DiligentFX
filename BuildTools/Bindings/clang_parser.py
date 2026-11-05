"""Extract native API declarations with Clang; no binding policies or language emission."""

from pathlib import Path
from functools import lru_cache
from importlib.metadata import PackageNotFoundError, version
import os

from clang import cindex

from binding_model import ModelError


@lru_cache(maxsize=1)
def configure_clang():
    """Use matching Python bindings and libclang without changing the host compiler."""
    try:
        bindings_version = version("clang")
    except PackageNotFoundError:
        bindings_version = "not installed"
    if bindings_version.split(".")[0] != "20":
        raise ModelError(
            f"Clang 20 Python bindings are required (found {bindings_version}). "
            "Install BuildTools/Bindings/requirements.txt in the generator's Python environment.")
    library = os.environ.get("LIBCLANG_LIBRARY_FILE")
    if library:
        cindex.Config.set_library_file(library)


def qualified_name(cursor):
    names = [cursor.spelling]
    parent = cursor.semantic_parent
    while parent and parent.kind != cindex.CursorKind.TRANSLATION_UNIT:
        if parent.spelling:
            names.append(parent.spelling)
        parent = parent.semantic_parent
    return "::".join(reversed(names))

def describe_type(native_type):
    """Keep aliases and qualifiers, but also expose their machine-readable types."""
    kind = native_type.kind
    result = {"const": native_type.is_const_qualified(), "volatile": native_type.is_volatile_qualified()}
    if kind == cindex.TypeKind.ELABORATED:
        named_type = describe_type(native_type.get_named_type())
        named_type["const"] |= result["const"]
        named_type["volatile"] |= result["volatile"]
        return named_type
    if kind == cindex.TypeKind.TYPEDEF:
        declaration = native_type.get_declaration()
        result.update(kind="alias", name=qualified_name(declaration),
                      target=describe_type(declaration.underlying_typedef_type))
    elif kind in (cindex.TypeKind.POINTER, cindex.TypeKind.LVALUEREFERENCE):
        result.update(kind="pointer" if kind == cindex.TypeKind.POINTER else "reference",
                      target=describe_type(native_type.get_pointee()))
    elif kind in (cindex.TypeKind.RECORD, cindex.TypeKind.ENUM):
        result.update(kind="named", name=qualified_name(native_type.get_declaration()))
    elif kind == cindex.TypeKind.CONSTANTARRAY:
        result.update(kind="array", count=native_type.element_count,
                      element=describe_type(native_type.element_type))
    elif kind == cindex.TypeKind.FUNCTIONPROTO:
        if native_type.is_function_variadic():
            raise ModelError(f"Variadic function type is not supported: {native_type.spelling}")
        result.update(kind="function", result=describe_type(native_type.get_result()),
                      parameters=[describe_type(arg) for arg in native_type.argument_types()])
    elif kind == cindex.TypeKind.VOID:
        result.update(kind="void")
    elif kind in (
        cindex.TypeKind.BOOL, cindex.TypeKind.CHAR_S, cindex.TypeKind.CHAR_U,
        cindex.TypeKind.SCHAR, cindex.TypeKind.UCHAR, cindex.TypeKind.SHORT,
        cindex.TypeKind.USHORT, cindex.TypeKind.INT, cindex.TypeKind.UINT,
        cindex.TypeKind.LONG, cindex.TypeKind.ULONG, cindex.TypeKind.LONGLONG,
        cindex.TypeKind.ULONGLONG, cindex.TypeKind.FLOAT, cindex.TypeKind.DOUBLE,
    ):
        result.update(kind="scalar", name=kind.name.lower(), bits=native_type.get_size() * 8)
    else:
        raise ModelError(f"Unsupported type: {native_type.spelling} ({kind.name})")
    return result

def describe_callable(cursor, documentation):
    result = {
        "name": cursor.spelling,
        "documentation": documentation(cursor),
        "result": {"type": describe_type(cursor.result_type)},
        "parameters": [{"name": arg.spelling, "type": describe_type(arg.type)}
                       for arg in cursor.get_arguments()],
    }
    if cursor.type.is_function_variadic():
        raise ModelError(f"{qualified_name(cursor)}: variadic functions are not supported")
    if cursor.kind == cindex.CursorKind.CXX_METHOD:
        result.update(const=cursor.is_const_method(), virtual=cursor.is_virtual_method())
    return result

def describe_declaration(cursor, root, external, documentation):
    name = qualified_name(cursor)
    result = {
        "name": name,
        "source": {"file": Path(os.path.relpath(cursor.location.file.name, root)).as_posix(),
                   "line": cursor.location.line},
        "documentation": documentation(cursor),
    }
    if external:
        # A foreign base interface is named, but its API is supplied by the runtime.
        result["kind"] = "external"
        return result

    kind = cursor.kind
    if kind == cindex.CursorKind.ENUM_DECL:
        result.update(kind="enum", underlying_type=describe_type(cursor.enum_type),
                      values=[{"name": child.spelling, "value": child.enum_value,
                               "documentation": documentation(child)}
                              for child in cursor.get_children()
                              if child.kind == cindex.CursorKind.ENUM_CONSTANT_DECL])
    elif kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.CLASS_DECL):
        fields, methods, bases = [], [], []
        for child in cursor.get_children():
            if child.kind == cindex.CursorKind.CXX_BASE_SPECIFIER:
                bases.append(qualified_name(child.type.get_declaration()))
            elif child.kind == cindex.CursorKind.FIELD_DECL:
                # Clang expands DEFAULT_INITIALIZER; its expression child retains
                # the written default. Backends must still initialize native structs.
                expressions = [node for node in child.get_children() if node.kind.is_expression()]
                if child.type.get_canonical().kind == cindex.TypeKind.CONSTANTARRAY:
                    # Array bounds also appear as expression children; they are
                    # not initializers. Only the array-valued expression is one.
                    expressions = [node for node in expressions
                                   if node.type.get_canonical().kind == cindex.TypeKind.CONSTANTARRAY]
                fields.append({"name": child.spelling, "type": describe_type(child.type),
                               "documentation": documentation(child),
                               "default": " ".join(token.spelling for token in expressions[-1].get_tokens())
                               if expressions else None})
            elif child.kind == cindex.CursorKind.CXX_METHOD:
                # Only virtual methods belong to the C-compatible interface.
                # Nonvirtual overloads and operators are C++ convenience helpers.
                if child.is_virtual_method():
                    methods.append(describe_callable(child, documentation))
            elif child.kind in (
                cindex.CursorKind.CONSTRUCTOR, cindex.CursorKind.DESTRUCTOR,
                cindex.CursorKind.CONVERSION_FUNCTION, cindex.CursorKind.FUNCTION_TEMPLATE,
                cindex.CursorKind.CXX_ACCESS_SPEC_DECL,
            ):
                continue
            else:
                raise ModelError(f"{name}: unsupported member {child.kind.name} '{child.spelling}'")
        if len({method["name"] for method in methods}) != len(methods):
            raise ModelError(f"{name}: overloaded methods require explicit support")
        # DILIGENT_DERIVE also gives ordinary descriptors a base class.
        # Inheritance alone does not make a record an interface.
        result.update(kind="interface" if methods or cursor.is_abstract_record() else "record",
                      bases=bases, fields=fields, methods=methods)
    elif kind == cindex.CursorKind.FUNCTION_DECL:
        result.update(describe_callable(cursor, documentation), kind="function", name=name)
    else:
        raise ModelError(f"{name}: unsupported declaration {kind.name}")
    return result

def extract_api(source_root, headers, declaration_names, target, clang_args=(), *, external_declarations=()):
    """Return a plain-data API model and available symbol names for policy validation.

    This stage reads native declarations only. External declarations remain opaque;
    their implementation is supplied by a language runtime or another binding.
    """
    root = Path(source_root).resolve()
    configure_clang()
    headers = [root / header for header in headers]
    for header in headers:
        if not header.is_file():
            raise ModelError(f"Header does not exist: {header}")
    args = ["-x", "c++", "-std=c++17", f"--target={target}"] + list(clang_args)
    # Check the native library too: installing the Python package does not replace
    # an older libclang found on the system. Keep incompatible SDK headers out of
    # that diagnostic so the actual toolchain problem is immediately visible.
    source = '#if __clang_major__ != 20\n#error Binding extraction requires libclang 20.\n#else\n'
    source += "".join(f'#include "{header.as_posix()}"\n' for header in headers)
    source += "#endif\n"
    try:
        unit = cindex.Index.create().parse(
            "binding_api.cpp", args=args,
            unsaved_files=[("binding_api.cpp", source)],
            options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    except (cindex.LibclangError, cindex.TranslationUnitLoadError) as error:
        raise ModelError(
            "Could not load or run libclang 20. Set LIBCLANG_LIBRARY_FILE to its shared library "
            f"(RADIENT_BINDINGS_LIBCLANG in CMake).\n{error}") from error
    errors = [str(diagnostic) for diagnostic in unit.diagnostics if diagnostic.severity >= cindex.Diagnostic.Error]
    if errors:
        raise ModelError("Clang rejected the API headers:\n" + "\n".join(errors))

    selected = set(declaration_names)
    external = set(external_declarations)
    declarations = {}
    symbols = set()

    def visit(parent):
        for cursor in parent.get_children():
            name = qualified_name(cursor)
            symbols.add(name)
            if name in selected and cursor.kind != cindex.CursorKind.TYPEDEF_DECL:
                if cursor.kind == cindex.CursorKind.FUNCTION_DECL or cursor.is_definition():
                    if name in declarations and declarations[name].get_usr() != cursor.get_usr():
                        raise ModelError(f"{name}: overloaded declarations require explicit support")
                    declarations[name] = cursor
            if cursor.kind in (cindex.CursorKind.NAMESPACE, cindex.CursorKind.LINKAGE_SPEC):
                visit(cursor)

    visit(unit.cursor)
    missing = selected - declarations.keys()
    if missing:
        raise ModelError("Declarations not found: " + ", ".join(sorted(missing)))

    source_lines = {}

    def documentation(cursor):
        if cursor.raw_comment:
            return cursor.raw_comment
        # Clang does not attach preceding comments when the declaration starts
        # inside a macro (DILIGENT_TYPED_ENUM, DILIGENT_BEGIN_INTERFACE, VIRTUAL).
        # Read the adjacent /// block at its expansion location instead. Keep the
        # source cache local to this extraction so later calls see header edits.
        start = cursor.extent.start
        if not start.file:
            return None
        path = start.file.name
        if path not in source_lines:
            source_lines[path] = Path(path).read_text(encoding="utf-8").splitlines()
        lines = source_lines[path]
        if lines[start.line - 1][:start.column - 1].strip():
            return None
        index = start.line - 2
        comments = []
        while index >= 0:
            line = lines[index].strip()
            if line.startswith("///"):
                comments.append(line)
            elif line:
                break
            index -= 1
        return "\n".join(reversed(comments)) or None

    models = [describe_declaration(declarations[name], root, name in external, documentation)
              for name in sorted(selected)]
    return {"schema_version": 1, "target": target, "declarations": models}, symbols
