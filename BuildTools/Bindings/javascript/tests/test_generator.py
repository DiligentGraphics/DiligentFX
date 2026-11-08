"""Emitter tests over a serialized model, with no libclang imports."""

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from javascript.generate import Generator, write
from binding_model import ModelError

MODEL = json.loads(Path(sys.argv.pop(1)).read_text(encoding="utf-8"))
ADAPTERS = json.loads(Path(__file__).with_name("Adapters.json").read_text(encoding="utf-8"))


class GeneratorTest(unittest.TestCase):
    def emit(self, model=None, adapters=None):
        return Generator(model or MODEL, "BindingTests", ["Interfaces.h"],
                         ADAPTERS if adapters is None else adapters).generate()

    def declaration(self, model, name):
        return next(d for d in model["declarations"] if d["name"] == "Diligent::" + name)

    def test_deterministic_generation_and_unchanged_output(self):
        output = self.emit()
        self.assertEqual(output, self.emit(json.loads(json.dumps(MODEL))))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "generated.cpp"
            write(path, output[0])
            before = path.stat().st_mtime_ns
            write(path, output[0])
            self.assertEqual(before, path.stat().st_mtime_ns)

    def test_unselected_methods_are_not_exported(self):
        cpp, js, types = self.emit()
        for output in (cpp, js, types):
            self.assertNotIn("GetNativeDesc", output)
            self.assertNotIn("MarshalNativeOnlyDesc", output)

    def test_native_defaults_and_opaque_dispatch(self):
        cpp, js, declarations = self.emit()
        self.assertIn("new (std::nothrow) Diligent::MarshalDesc{}", cpp)
        self.assertIn("reinterpret_cast<Diligent::IMarshalObject*>(self)->GetSize()", cpp)
        self.assertNotIn("vtable", js)
        self.assertNotIn("clang", sys.modules)
        self.assertIn("Size?: bigint", declarations)
        self.assertIn("Enabled?: boolean", declarations)
        self.assertIn("IMarshalMutableObject extends IMarshalObject", declarations)
        self.assertIn("Result<IMarshalObject | null>", declarations)

    def test_interface_references_preserve_constness_and_use_nonnull_wrappers(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        self.assertIn("ReadObjectReference(*reinterpret_cast<const Diligent::IMarshalObject*>(Object))", cpp)
        self.assertIn("WriteObjectReference(*reinterpret_cast<Diligent::IMarshalMutableObject*>(Object)", cpp)
        self.assertIn("->CopySizeFrom(*reinterpret_cast<const Diligent::IMarshalObject*>(Source))", cpp)
        self.assertIn("->CopySizeTo(*reinterpret_cast<Diligent::IMarshalMutableObject*>(Destination))", cpp)
        self.assertIn("ReadObjectReference(Object: IMarshalObject): bigint;", types)
        self.assertIn("WriteObjectReference(Object: IMarshalMutableObject, Desc: MarshalDesc): number;", types)
        self.assertIn("CopySizeFrom(Source: IMarshalObject): number;", types)
        self.assertIn("CopySizeTo(Destination: IMarshalMutableObject): number;", types)
        for name, interface in (("ReadObjectReference", "IMarshalObject"),
                                ("WriteObjectReference", "IMarshalMutableObject")):
            self.assertEqual(generator.schema["functions"][name]["parameters"][0],
                             {"name": "Object", "interface": interface, "nullable": False})

    def test_interface_references_require_nonnull_borrowed_inputs(self):
        for name in ("ReadObjectReference", "WriteObjectReference"):
            for key, value in (("nullable", True), ("direction", "out"),
                               ("ownership", "owned"), ("lifetime", "object")):
                with self.subTest(function=name, key=key):
                    model = deepcopy(MODEL)
                    self.declaration(model, name)["parameters"][0]["policy"][key] = value
                    with self.assertRaises(ModelError):
                        self.emit(model)

    def test_computed_outputs_query_native_objects_and_keep_counts_out_of_js_api(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        self.assertIn("reinterpret_cast<Diligent::IMarshalComputedOutputs*>(source)->GetDesc().Count", cpp)
        self.assertIn("reinterpret_cast<const Diligent::IMarshalComputedOutputs*>(source)->GetValueCount()", cpp)
        self.assertIn("FillMatrices(pSource: IMarshalComputedOutputs, Fail?: boolean, Grow?: boolean): Result<Array<StructSnapshot<MarshalComputedMatrix>>>;", types)
        self.assertIn("FillComputedValues(Source: IMarshalComputedOutputs, Options: MarshalDesc): Uint32Array;", types)
        buffer = generator.schema["interfaces"]["IMarshalComputedOutputs"]["methods"]["FillMatrices"]["parameters"][1]["output"]["buffer"]
        self.assertEqual(buffer["lengthQuery"]["parameter"], "pSource")
        self.assertNotIn("length", buffer)
        self.assertIn("create", buffer)
        self.assertIn("destroy", buffer)

    def test_computed_outputs_reject_invalid_native_queries(self):
        for change in ("source", "nullable", "method", "nonconst", "field", "count_type"):
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                interface = self.declaration(model, "IMarshalComputedOutputs")
                methods = {m["name"]: m for m in interface["methods"]}
                parameters = methods["FillMatrices"]["parameters"]
                query = parameters[1]["policy"]["length_from"]
                if change == "source":
                    query["parameter"] = "missing"
                elif change == "nullable":
                    parameters[0]["policy"]["nullable"] = True
                elif change == "method":
                    query["method"] = "SetCount"
                elif change == "nonconst":
                    methods["GetDesc"]["const"] = False
                elif change == "field":
                    query["field"] = "missing"
                else:
                    self.declaration(model, "MarshalComputedDesc")["fields"][0]["type"] = {
                        "kind": "scalar", "name": "int", "bits": 32, "const": False, "volatile": False}
                with self.assertRaises(ModelError):
                    self.emit(model)

    def test_nested_fields_use_native_subobjects_and_types(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        fields = generator.schema["records"]["MarshalNestedDesc"]["fields"]
        self.assertEqual(fields["Transform"], {
            "record": "MarshalNestedTransform",
            "address": "_binding_Diligent_MarshalNestedDesc_Field_Transform"})
        self.assertIn("&reinterpret_cast<Diligent::MarshalNestedDesc*>(pointer)->Transform", cpp)
        self.assertIn("Transform?: MarshalNestedTransform;", types)
        # A typedef to a child struct uses the same canonical descriptor.
        scale = generator.schema["records"]["MarshalNestedTransform"]["fields"]["Scale"]
        self.assertEqual(scale["record"], "MarshalNestedVector")
        self.assertIn("Scale?: MarshalNestedVector;", types)

    def test_nested_fields_reject_const_and_volatile(self):
        for qualifier, message in [("const", "const fields"), ("volatile", "Volatile values")]:
            with self.subTest(qualifier=qualifier):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalNestedDesc")["fields"][1]
                field["type"][qualifier] = True
                with self.assertRaisesRegex(ModelError, message):
                    self.emit(model)

    def test_struct_results_have_readers_and_required_snapshot_fields(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        self.assertIn("GetDesc(): StructSnapshot<MarshalSnapshotDesc>;", types)
        self.assertIn("GetStaticDescriptor(): StructSnapshot<MarshalDesc>;", types)
        self.assertIn("EchoStringDescriptor(Desc: MarshalStringDesc, InvalidUTF8: boolean): StructSnapshot<MarshalStringDesc>;", types)
        self.assertIn("{ [K in keyof T]-?: StructSnapshot<Exclude<T[K], undefined>> }", types)
        self.assertIn("Tag: number;", types)
        self.assertIn("Name: string;", types)
        self.assertIn("return reinterpret_cast<uintptr_t>(&reinterpret_cast<Diligent::IMarshalSnapshots*>(self)->GetDesc());", cpp)
        record = generator.schema["records"]["MarshalSnapshotDesc"]
        self.assertNotIn("create", record)
        self.assertNotIn("destroy", record)
        self.assertIn("get", record["fields"]["Name"])
        self.assertNotIn("set", record["fields"]["Name"])
        self.assertIn("get", generator.schema["records"]["MarshalStringDesc"]["fields"]["Name"])
        self.assertIn("set", generator.schema["records"]["MarshalStringDesc"]["fields"]["Name"])

    def test_struct_results_reject_unsupported_reference_contracts(self):
        for key, value in [("direction", "in"), ("nullable", True), ("ownership", "owned"),
                           ("lifetime", "read_scope"), ("const", False), ("volatile", True)]:
            with self.subTest(key=key):
                model = deepcopy(MODEL)
                method = self.declaration(model, "IMarshalSnapshots")["methods"][0]
                if key in ("const", "volatile"):
                    method["result"]["type"]["target"][key] = value
                else:
                    method["result"]["policy"][key] = value
                with self.assertRaisesRegex(ModelError, "struct results require a borrowed const reference"):
                    self.emit(model)

    def test_struct_results_reject_unannotated_pointer_fields(self):
        model = deepcopy(MODEL)
        field = self.declaration(model, "MarshalSnapshotChild")["fields"][0]
        field["type"] = {"kind": "pointer", "target": field["type"], "const": False, "volatile": False}
        with self.assertRaisesRegex(ModelError, "expected a scalar"):
            self.emit(model)

    def test_struct_results_validate_nested_string_ownership(self):
        for key, value in [("ownership", "owned"), ("lifetime", "read_scope"), ("direction", "in")]:
            with self.subTest(key=key):
                model = deepcopy(MODEL)
                self.declaration(model, "MarshalSnapshotChild")["fields"][1]["policy"][key] = value
                with self.assertRaisesRegex(ModelError, "strings require borrowed const char"):
                    self.emit(model)

    def test_struct_snapshots_do_not_require_iobject(self):
        model = deepcopy(MODEL)
        names = {"MarshalDesc", "MARSHAL_MODE", "GetStaticDescriptor"}
        model["declarations"] = [d for d in model["declarations"] if d["name"].removeprefix("Diligent::") in names]
        cpp, _, _ = self.emit(model, {})
        self.assertNotIn("IObject", cpp)
        self.assertNotIn("binding_Release", cpp)

    def test_input_arrays_have_element_types_and_contiguous_native_storage(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        self.assertIn("CheckArrayFloat32(pValues: Float32Array, Count: number)", types)
        self.assertIn("CheckArrayUint64(pValues: BigUint64Array, Count: bigint)", types)
        self.assertIn("pItems?: ReadonlyArray<MarshalArrayItem> | null", types)
        self.assertIn("pSamples?: Uint16Array | null", types)
        self.assertIn("new (std::nothrow) Diligent::MarshalArrayItem[count]{}", cpp)
        self.assertIn("delete[] reinterpret_cast<Diligent::MarshalArrayItem*>(pointer)", cpp)
        self.assertIn("PTRDIFF_MAX / sizeof(Diligent::MarshalArrayItem)", cpp)
        self.assertEqual(cpp.count("uintptr_t binding_Diligent_MarshalArrayItem_ArrayCreate("), 1)
        span = generator.schema["records"]["MarshalArrayDesc"]["fields"]["pItems"]["span"]
        self.assertEqual(span["record"], "MarshalArrayItem")
        self.assertEqual(span["length"], "ItemCount")
        self.assertEqual(span["lengthType"], "u32")

    def test_input_arrays_require_supported_counts_and_call_scoped_const_elements(self):
        for key, value in [("direction", "out"), ("ownership", "owned"), ("lifetime", "object"),
                           ("const", False), ("volatile", True), ("count", "int")]:
            with self.subTest(key=key):
                model = deepcopy(MODEL)
                function = self.declaration(model, "CheckArrayFloat32")
                if key in ("const", "volatile"):
                    function["parameters"][0]["type"]["target"][key] = value
                elif key == "count":
                    function["parameters"][1]["type"] = {"kind": "scalar", "name": value, "bits": 32, "const": False, "volatile": False}
                else:
                    function["parameters"][0]["policy"][key] = value
                with self.assertRaisesRegex(ModelError, "input spans require|output buffers require|span length must be"):
                    self.emit(model)

    def test_interface_array_types_and_snapshot_ownership(self):
        _, _, types = self.emit()
        self.assertIn("ppObjects?: ReadonlyArray<IMarshalObject | null> | null", types)
        self.assertIn("T extends ArrayBufferView | NativeObject ? T", types)

    def test_array_elements_used_as_results_still_have_input_marshalling(self):
        model = deepcopy(MODEL)
        function = self.declaration(model, "ReadArrayItem")
        function["result"] = {
            "type": {"kind": "reference", "const": False, "volatile": False,
                     "target": {"kind": "named", "name": "Diligent::MarshalNestedVector", "const": True, "volatile": False}},
            "policy": {"direction": "out", "ownership": "borrowed", "nullable": False, "lifetime": "call"}}
        function["parameters"][0]["type"]["target"] = function["result"]["type"]["target"]
        model["declarations"] = [function, self.declaration(model, "MarshalNestedVector")]
        generator = Generator(model, "BindingTests", ["Interfaces.h"], {})
        generator.generate()
        fields = generator.schema["records"]["MarshalNestedVector"]["fields"]
        self.assertIn("get", fields["x"])
        self.assertIn("set", fields["x"])

    def test_output_arrays_have_typed_snapshots_and_native_layout_queries(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        self.assertIn("GetValues(Count: number): Float32Array;", types)
        self.assertIn("GetItems(Count: number): Array<StructSnapshot<MarshalOutputArrayItem>>;", types)
        self.assertIn("GetStaticOutputArray(Count: bigint): Float64Array | null;", types)
        self.assertIn("pGroups: Array<StructSnapshot<MarshalOutputArrayGroup>>;", types)
        self.assertIn("T extends ArrayBufferView | NativeObject ? T", types)
        self.assertIn("T extends ReadonlyArray<infer E> ? Array<StructSnapshot<E>>", types)
        self.assertIn("return sizeof(Diligent::MarshalOutputArrayGroup);", cpp)
        self.assertIn("return alignof(Diligent::MarshalOutputArrayGroup);", cpp)
        self.assertNotIn("new (std::nothrow) Diligent::MarshalOutputArrayGroup", cpp)
        field = generator.schema["records"]["MarshalOutputArrayGroup"]["fields"]["pItems"]
        self.assertIn("get", field)
        self.assertNotIn("set", field)
        fields = generator.schema["records"]["MarshalArrayRoundTrip"]["fields"]
        self.assertIn("get", fields["pItems"])
        self.assertIn("set", fields["pItems"])

    def test_output_arrays_reject_unsupported_element_and_storage_contracts(self):
        for key, value in [("direction", "in"), ("ownership", "owned"), ("lifetime", "scope"),
                           ("const", False), ("volatile", True), ("length", "Missing")]:
            with self.subTest(key=key):
                model = deepcopy(MODEL)
                result = self.declaration(model, "GetStaticOutputArray")["result"]
                if key in ("const", "volatile"):
                    result["type"]["target"][key] = value
                else:
                    result["policy"][key] = value
                with self.assertRaisesRegex(ModelError, "output spans require"):
                    self.emit(model)
        model = deepcopy(MODEL)
        result = self.declaration(model, "GetStaticOutputArray")["result"]
        result["type"]["target"] = {"kind": "pointer", "const": True, "volatile": False,
            "target": {"kind": "named", "name": "Diligent::IMarshalObject", "const": False, "volatile": False}}
        result["type"]["target"]["target"]["name"] = "Diligent::MarshalDesc"
        with self.assertRaisesRegex(ModelError, "pointer arrays require interface elements"):
            self.emit(model)

    def test_output_array_fields_require_out_or_inout_policies(self):
        model = deepcopy(MODEL)
        field = self.declaration(model, "MarshalOutputArrayGroup")["fields"][0]
        field["policy"]["direction"] = "in"
        with self.assertRaisesRegex(ModelError, "output spans require"):
            self.emit(model)

    def test_fixed_array_fields_use_parent_storage_and_native_row_strides(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        cpp, _, types = generator.generate()
        self.assertIn("Data?: Float32Array;", types)
        self.assertIn("Rows?: ReadonlyArray<Float32Array>;", types)
        self.assertIn("Cube?: ReadonlyArray<ReadonlyArray<Float32Array>>;", types)
        self.assertIn("Children?: ReadonlyArray<MarshalFixedChild>;", types)
        self.assertIn("Items: Array<StructSnapshot<MarshalFixedReadOnlyElement>>;", types)
        self.assertIn("sizeof(reinterpret_cast<const Diligent::MarshalFixedArrays*>(pointer)->Rows[0])", cpp)
        self.assertNotIn("binding_Diligent_MarshalFixedChild_ArrayCreate", cpp)
        self.assertNotIn("binding_Diligent_MarshalFixedMatrix_ArrayCreate", cpp)
        self.assertNotIn("new (std::nothrow) Diligent::MarshalFixedReadOnlyElement", cpp)
        array = generator.schema["records"]["MarshalFixedArrays"]["fields"]["Rows"]["array"]
        self.assertEqual(array["count"], 2)
        self.assertEqual(array["array"]["count"], 4)
        self.assertEqual(array["array"]["type"], "float")
        fields = generator.schema["records"]["MarshalFixedChild"]["fields"]
        self.assertIn("get", fields["Id"])
        self.assertIn("set", fields["Id"])

    def test_fixed_array_fields_reject_invalid_extents_and_element_types(self):
        for change in ["zero", "negative", "pointer", "volatile", "const", "length"]:
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalFixedMatrix")["fields"][0]
                array = field["type"]
                if change in ("zero", "negative"):
                    array["count"] = 0 if change == "zero" else -1
                elif change == "pointer":
                    array["element"] = {"kind": "pointer", "const": False, "volatile": False, "target": array["element"]}
                elif change == "length":
                    field["policy"] = {"length": "Data"}
                else:
                    array["element"][change] = True
                with self.assertRaisesRegex(ModelError, "fixed arrays"):
                    self.emit(model)

    def test_nested_array_helpers_do_not_collide_with_field_names(self):
        model = deepcopy(MODEL)
        declaration = self.declaration(model, "MarshalFixedArrays")
        cube = next(f for f in declaration["fields"] if f["name"] == "Cube")
        # Cube's second dimension must not use the name of a Cube_Row field.
        declaration["fields"].append(dict(deepcopy(cube), name="Cube_Row", type=deepcopy(cube["type"]["element"])))
        cpp, _, _ = self.emit(model)
        self.assertEqual(cpp.count("uintptr_t binding_Diligent_MarshalFixedArrays_Cube_ArrayDimension1_Element("), 1)
        self.assertEqual(cpp.count("uintptr_t binding_Diligent_MarshalFixedArrays_Cube_Row_ArrayDimension0_Element("), 1)

    def test_fixed_arrays_alone_classify_nested_const_output_records(self):
        model = deepcopy(MODEL)
        names = {"MarshalFixedReadOnly", "MarshalFixedReadOnlyElement", "GetFixedReadOnlyArrays"}
        model["declarations"] = [d for d in model["declarations"] if d["name"].removeprefix("Diligent::") in names]
        generator = Generator(model, "BindingTests", ["Interfaces.h"], {})
        cpp, _, _ = generator.generate()
        self.assertNotIn("new (std::nothrow)", cpp)
        field = generator.schema["records"]["MarshalFixedReadOnlyElement"]["fields"]["Value"]
        self.assertIn("get", field)
        self.assertNotIn("set", field)

    def test_scalar_references_and_struct_output_parameters(self):
        cpp, _, types = self.emit()
        self.assertIn("ReadReferenceOutput(Status: number): Result<bigint>;", types)
        self.assertIn("MakeRecordOutput(): StructSnapshot<MarshalOutputRecord>;", types)
        self.assertIn("*reinterpret_cast<Diligent::Uint64*>(Value)", cpp)
        self.assertIn("new (std::nothrow) Diligent::MarshalOutputRecord{}", cpp)

    def test_output_buffer_and_multiple_output_declarations(self):
        _, _, types = self.emit()
        self.assertIn("FillIds(Capacity: number, InvalidCount: boolean, Grow: boolean): Result<{ pIds: BigUint64Array; Written: number }>", types)
        self.assertIn("FillRecords(Capacity: number): Array<StructSnapshot<MarshalOutputRecord>>", types)

    def test_borrowed_interface_returns_retain_ownership_metadata(self):
        generator = Generator(MODEL, "BindingTests", ["Interfaces.h"], ADAPTERS)
        _, _, types = generator.generate()
        self.assertEqual(generator.schema["functions"]["BorrowObject"]["result"], {"interface": "IMarshalObject", "nullable": True, "borrowed": True})
        self.assertIn("BorrowObject(pObject: IMarshalObject, Null: boolean): IMarshalObject | null", types)

    def test_optional_struct_pointer_declarations(self):
        _, _, types = self.emit()
        self.assertIn("ReadStructPointer(pData: MarshalDesc | null): bigint", types)
        self.assertIn("EchoStructPointer(pData: MarshalDesc | null): StructSnapshot<MarshalDesc> | null", types)

    def test_inherited_descriptors_use_native_derived_access(self):
        cpp, _, types = self.emit()
        self.assertIn("reinterpret_cast<Diligent::MarshalLeafDesc*>(pointer)->Tag = value", cpp)
        self.assertIn("new (std::nothrow) Diligent::MarshalLeafDesc{}", cpp)
        self.assertIn("EchoInherited(Desc: MarshalLeafDesc): StructSnapshot<MarshalLeafDesc>", types)

    def test_array_counts_from_methods_are_validated(self):
        cpp, _, types = self.emit()
        self.assertIn("GetAllValues(): Float32Array", types)
        self.assertIn("reinterpret_cast<Diligent::IMarshalArraySnapshots*>(self)->GetValueCount()", cpp)
        for name in ("Missing", "SetValueCount", "Update"):
            model = deepcopy(MODEL)
            methods = self.declaration(model, "IMarshalArraySnapshots")["methods"]
            next(m for m in methods if m["name"] == "GetAllValues")["result"]["policy"]["length_method"] = name
            with self.assertRaisesRegex(ModelError, "length_method"):
                self.emit(model)

    def test_named_constant_and_typedef_exports(self):
        _, js, types = self.emit()
        self.assertIn("export const MarshalInvalidID = 18446744073709551615n;", js)
        self.assertIn("export type MarshalEntityID = bigint;", types)
        self.assertIn("export type MarshalEntityAlias = bigint;", types)
        self.assertIn("export type MarshalDescriptorAlias = MarshalDesc;", types)
        self.assertIn("export type MarshalPropertyName = string;", types)
        self.assertIn("readonly MarshalInvalidID: typeof MarshalInvalidID", types)

    def test_rejects_wrong_model_target(self):
        model = deepcopy(MODEL)
        model["target"] = "x86_64-pc-windows-msvc"
        with self.assertRaisesRegex(ModelError, "Wasm32"):
            self.emit(model)

    def test_scalar_only_api_has_no_iobject_dependency(self):
        model = deepcopy(MODEL)
        model["declarations"] = [self.declaration(model, "EchoInt32")]
        cpp, _, _ = self.emit(model, {})
        self.assertNotIn("IObject", cpp)
        self.assertNotIn("binding_Release", cpp)

    def test_rejects_manual_members_without_an_adapter(self):
        model = deepcopy(MODEL)
        self.declaration(model, "MarshalDesc")["fields"][0]["policy"] = {"binding": "manual", "reason": "test"}
        with self.assertRaisesRegex(ModelError, "explicit JavaScript adapter"):
            self.emit(model)

    def test_rejects_unannotated_descriptor_pointers(self):
        model = deepcopy(MODEL)
        field = self.declaration(model, "MarshalDesc")["fields"][0]
        field["type"] = {"kind": "pointer", "target": field["type"], "const": False, "volatile": False}
        with self.assertRaisesRegex(ModelError, "expected a scalar"):
            self.emit(model)

    def test_rejects_unsupported_ownership(self):
        model = deepcopy(MODEL)
        self.declaration(model, "CreateMarshalObject")["parameters"][-1]["policy"]["ownership"] = "borrowed"
        with self.assertRaisesRegex(ModelError, "unsupported interface ownership"):
            self.emit(model)

    def test_default_arguments_use_native_overloads(self):
        cpp, _, types = self.emit()
        self.assertIn("Diligent::WithDefaults(Value);", cpp)
        self.assertIn("Diligent::WithDefaults(Value, Add);", cpp)
        self.assertIn("WithDefaults(Value: bigint, Add?: boolean, Amount?: number)", types)
        self.assertIn("SetValueCount(Count?: bigint)", types)
        self.assertNotIn("NextDefaultValue()", cpp[cpp.index("binding_Diligent_WithDefaults"):])

    def test_rejects_runtime_name_collisions(self):
        model = deepcopy(MODEL)
        self.declaration(model, "EchoInt32")["name"] = "Diligent::Result"
        with self.assertRaisesRegex(ModelError, "conflicting JavaScript name"):
            self.emit(model)

    def test_byte_spans_and_scopes_have_explicit_types(self):
        cpp, js, declarations = self.emit()
        self.assertIn("pBytes?: Uint8Array | null", declarations)
        self.assertIn("ChecksumBytes(pBytes: Uint8Array, ByteCount: bigint, Invert: boolean)", declarations)
        self.assertIn("MapRead(): Result<ReadByteAccess | null>", declarations)
        self.assertIn("MapWrite(): Result<WriteByteAccess | null>", declarations)
        self.assertNotIn("UnmapRead()", declarations)
        self.assertNotIn("UnmapWrite()", declarations)
        self.assertIn("->UnmapRead()", cpp)
        self.assertIn("MapRead(reinterpret_cast<const void**>(output))", cpp)
        self.assertIn('"success": 3', js)

    def test_strings_have_explicit_types_and_support_both_output_forms(self):
        cpp, _, types = self.emit()
        self.assertIn("Name?: string;", types)
        self.assertIn("Label?: string | null;", types)
        self.assertIn("EchoString(Text: string, GrowHeap: boolean): string;", types)
        self.assertIn("EchoNullableString(Text: string | null): string | null;", types)
        self.assertIn("GetText(): string | null;", types)
        self.assertIn("ReadText(Fail: boolean): Result<string | null>;", types)
        self.assertIn("ReadTextPointer(Fail: boolean): Result<string | null>;", types)
        self.assertIn("CheckCharacterBytes(ByteCount: number, Bytes: Uint8Array): boolean;", types)
        self.assertIn("*reinterpret_cast<const Diligent::Char**>(Text)", cpp)
        self.assertIn("reinterpret_cast<const Diligent::Char**>(ppText)", cpp)

    def test_strings_reject_mutable_owned_and_retained_inputs(self):
        for change in ["mutable", "ownership", "lifetime", "direction"]:
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalStringDesc")["fields"][1]
                if change == "mutable":
                    field["type"]["target"]["const"] = False
                else:
                    field["policy"][change] = {"ownership": "owned", "lifetime": "object", "direction": "out"}[change]
                with self.assertRaisesRegex(ModelError, "strings require borrowed const char"):
                    self.emit(model)

    def test_strings_reject_unsupported_output_ownership_and_lifetimes(self):
        for name, key, value in [("GetStaticString", "ownership", "owned"),
                                 ("GetStaticString", "lifetime", "read_scope"),
                                 ("EchoString", "direction", "in")]:
            with self.subTest(name=name, key=key):
                model = deepcopy(MODEL)
                self.declaration(model, name)["result"]["policy"][key] = value
                with self.assertRaisesRegex(ModelError, "strings require borrowed const char"):
                    self.emit(model)

    def test_scope_size_query_can_be_inherited(self):
        model = deepcopy(MODEL)
        interface = self.declaration(model, "IMarshalBytes")
        interface["bases"] = ["Diligent::IMarshalObject"]
        adapters = deepcopy(ADAPTERS)
        for scope in adapters["scoped_bytes"].values():
            scope["size"] = "GetSize"
        cpp, _, _ = self.emit(model, adapters)
        self.assertIn("reinterpret_cast<Diligent::IMarshalBytes*>(self)->GetSize()", cpp)

    def test_scope_methods_require_an_explicit_adapter(self):
        with self.assertRaisesRegex(ModelError, "explicit JavaScript adapter"):
            self.emit(adapters={})

    def test_scope_adapter_rejects_wrong_methods_types_and_modes(self):
        for key, value, error in [
            ("size", "NoSuchMethod", "must belong"),
            ("release", "NoSuchMethod", "must belong"),
            ("writable", True, "matching constness"),
            ("success", 2**32, "32-bit signed status"),
            ("writable", "false", "invalid scoped byte"),
        ]:
            with self.subTest(key=key, value=value):
                adapters = deepcopy(ADAPTERS)
                adapters["scoped_bytes"]["Diligent::IMarshalBytes::MapRead"][key] = value
                with self.assertRaisesRegex(ModelError, error):
                    self.emit(adapters=adapters)

    def test_input_spans_reject_retention_and_mutable_data(self):
        for change in ["lifetime", "type", "length"]:
            with self.subTest(change=change):
                model = deepcopy(MODEL)
                field = self.declaration(model, "MarshalBufferDesc")["fields"][1]
                if change == "lifetime":
                    field["policy"]["lifetime"] = "object"
                elif change == "type":
                    field["type"]["target"]["const"] = False
                else:
                    field["policy"]["length"] = "NoSuchField"
                with self.assertRaisesRegex(ModelError, "input spans require"):
                    self.emit(model)

    def test_native_only_fields_and_fixed_arguments_preserve_the_model(self):
        adapters = deepcopy(ADAPTERS)
        adapters["native_only"] = ["Diligent::MarshalDesc::Weight"]
        adapters["fixed_arguments"] = {"Diligent::CreateMarshalObject": {"Status": "Diligent::MARSHAL_STATUS_OK"}}
        original = deepcopy(MODEL)
        cpp, _, types = self.emit(adapters=adapters)
        self.assertEqual(MODEL, original)
        self.assertNotIn("Weight?:", types)
        self.assertIn("CreateMarshalObject(Desc: MarshalDesc, Mutable: boolean)", types)
        self.assertIn("Diligent::MARSHAL_STATUS_OK, Mutable,", cpp)

    def test_native_only_external_types_require_explicit_selection(self):
        model = deepcopy(MODEL)
        model["declarations"].append({"name": "Diligent::NativeConfiguration", "kind": "external"})
        with self.assertRaisesRegex(ModelError, "no JavaScript runtime implementation"):
            self.emit(model)
        adapters = deepcopy(ADAPTERS)
        adapters["native_only"] = ["Diligent::NativeConfiguration"]
        cpp, js, types = self.emit(model, adapters)
        for output in (cpp, js, types):
            self.assertNotIn("NativeConfiguration", output)

    def test_field_policy_override_is_local_to_the_backend(self):
        model = deepcopy(MODEL)
        field = self.declaration(model, "MarshalBufferDesc")["fields"][1]
        adapters = deepcopy(ADAPTERS)
        adapters["field_policies"] = {"Diligent::MarshalBufferDesc": {"pBytes": deepcopy(field["policy"])}}
        field["policy"] = {"binding": "manual", "reason": "Backend chooses lifetime"}
        _, _, types = self.emit(model, adapters)
        self.assertIn("pBytes?: Uint8Array | null", types)
        self.assertEqual(field["policy"]["binding"], "manual")

    def test_invalid_subset_configuration_is_rejected(self):
        for section, config, error in [
            ("native_only", ["Diligent::MarshalDesc::NoSuchField"], "native-only selection"),
            ("fixed_arguments", {"Diligent::CreateMarshalObject": {"Status": "Diligent::MARSHAL_MODE_FIRST"}}, "enumerator"),
            ("field_policies", {"Diligent::MarshalDesc": {"NoSuchField": {}}}, "unknown metadata keys"),
        ]:
            with self.subTest(section=section):
                adapters = deepcopy(ADAPTERS)
                adapters[section] = config
                with self.assertRaisesRegex(ModelError, error):
                    self.emit(adapters=adapters)


if __name__ == "__main__":
    unittest.main()
