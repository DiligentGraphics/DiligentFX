import assert from "node:assert/strict";
import {afterEach, test} from "node:test";
import {pathToFileURL} from "node:url";

const moduleURL = pathToFileURL(process.argv[2]);
const {default: loadBindings, MARSHAL_STATUS} = await import(moduleURL);
let wasm;
const api = await loadBindings({onRuntimeInitialized() { wasm = this; }});
const {MARSHAL_STATUS_OK: OK, MARSHAL_STATUS_FAILED: FAILED, MARSHAL_STATUS_PENDING: PENDING} = MARSHAL_STATUS;
const text = "Aé水😀";

// Observe allocations in the real generated wrappers, including failure paths.
// Native object storage is independent of these temporary marshalling buffers.
const allocations = new Set();
const descriptors = new Set();
function track(createName, destroyName, pending) {
    const create = wasm[createName];
    const destroy = wasm[destroyName];
    wasm[createName] = (...args) => {
        const address = create(...args) >>> 0;
        if (address) {
            assert.equal(pending.has(address), false);
            pending.add(address);
        }
        return address;
    };
    wasm[destroyName] = address => {
        assert.equal(pending.delete(address >>> 0), true, `Unpaired ${destroyName}`);
        destroy(address);
    };
}
track("_malloc", "_free", allocations);
track("_binding_Diligent_MarshalStringDesc_Create", "_binding_Diligent_MarshalStringDesc_Destroy", descriptors);
afterEach(() => {
    assert.equal(allocations.size, 0, "Temporary string/output storage leaked");
    assert.equal(descriptors.size, 0, "Temporary descriptor leaked");
});

function object(t, value = text) {
    const result = api.CreateMarshalStrings(value);
    assert.equal(result.status, OK);
    assert.ok(result.value);
    t.after(() => result.value.dispose());
    return result.value;
}

test("UTF-8 input and output match independently authored native bytes", () => {
    assert.equal(api.CheckStringBytes(text), true);
    assert.equal(api.CheckStringBytes("different"), false);
    assert.equal(api.GetStaticString(), text);
    for (const value of ["", "ASCII", text, "e\u0301", "\ufeffcontent", "x".repeat(40000)]) {
        assert.equal(api.EchoString(value, false), value);
    }
});

test("nullable strings distinguish null from an allocated empty string", t => {
    for (const value of [null, "", text]) {
        assert.equal(api.EchoNullableString(value), value);
        const strings = object(t, value);
        assert.equal(strings.GetText(), value);
        assert.deepEqual(strings.ReadText(false), {status: PENDING, value});
    }
    assert.throws(() => api.EchoString(null, false), TypeError);
});

test("character pointers with an explicit length remain byte spans", () => {
    assert.equal(api.CheckCharacterBytes(3, new Uint8Array([65, 0, 90])), true);
    assert.throws(() => api.CheckCharacterBytes(3, "A\0Z"), TypeError);
});

test("descriptor strings preserve native defaults and copy before temporary cleanup", () => {
    assert.equal(api.GetStringDescName({}), "native default");
    assert.equal(api.GetStringDescName({Name: undefined}), "native default");
    assert.equal(api.GetStringDescLabel({}), null);
    assert.equal(api.GetStringDescLabel({Label: undefined}), null);
    for (const value of ["", text]) {
        assert.equal(api.GetStringDescName({Name: value, Label: "other"}), value);
        assert.equal(api.GetStringDescLabel({Name: "other", Label: value}), value);
    }
    assert.equal(api.GetStringDescLabel({Label: null}), null);
    assert.throws(() => api.GetStringDescName({Name: null}), TypeError);
});

test("borrowed outputs are copied and remain valid after native mutation and destruction", t => {
    const strings = object(t);
    const direct = strings.GetText();
    const reference = strings.ReadText(false);
    const pointer = strings.ReadTextPointer(false);
    strings.SetText("replacement".repeat(100));
    assert.equal(strings.GetText(), "replacement".repeat(100));
    strings.dispose();
    assert.equal(direct, text);
    assert.deepEqual(reference, {status: PENDING, value: text});
    assert.deepEqual(pointer, reference);
    assert.throws(() => strings.GetText(), /disposed/);
});

test("failed string outputs are initialized to null and preserve native status", t => {
    const strings = object(t);
    assert.deepEqual(strings.ReadText(true), {status: FAILED, value: null});
    assert.deepEqual(strings.ReadTextPointer(true), {status: FAILED, value: null});
    strings.SetText(null);
    assert.equal(strings.GetText(), null);
    strings.SetText("");
    assert.equal(strings.GetText(), "");
});

test("invalid string arguments and embedded nulls never enter native code", () => {
    const calls = api.GetStringCallCount();
    for (const value of [undefined, 42, true, {}, new String("text"), new Uint8Array(2), "a\0b"]) {
        assert.throws(() => api.EchoString(value, false), TypeError);
        assert.throws(() => api.EchoNullableString(value), TypeError);
        // Undefined descriptor fields intentionally preserve native defaults.
        if (value !== undefined) assert.throws(() => api.GetStringDescName({Name: value}), TypeError);
    }
    assert.equal(api.GetStringCallCount(), calls);
});

test("temporary strings are released when later arguments or descriptor getters fail", () => {
    assert.throws(() => api.EchoString(text, "not a boolean"), TypeError);
    assert.throws(() => api.GetStringDescName({Name: text, Enabled: 1}), TypeError);
    assert.throws(() => api.GetStringDescName({Name: text, Unknown: 1}), TypeError);
    const error = new Error("descriptor getter failed");
    assert.throws(() => api.GetStringDescName({Name: text, get Label() { throw error; }}), error);
});

test("allocation and decoding failures release temporary strings and descriptors", () => {
    const malloc = wasm._malloc;
    try {
        wasm._malloc = () => 0;
        assert.throws(() => api.GetStringDescName({Name: text}), /allocate string/);
    } finally {
        wasm._malloc = malloc;
    }
    assert.throws(() => api.GetInvalidUTF8(), TypeError);
    const echo = wasm._binding_Diligent_EchoString;
    try {
        // Simulate a native contract violation after the input is allocated.
        wasm._binding_Diligent_EchoString = () => wasm._binding_Diligent_GetInvalidUTF8();
        assert.throws(() => api.EchoString(text, false), TypeError);
        wasm._binding_Diligent_EchoString = () => 0;
        assert.throws(() => api.EchoString(text, false), /null non-nullable string/);
    } finally {
        wasm._binding_Diligent_EchoString = echo;
    }
});

test("unpaired JavaScript surrogates follow TextEncoder replacement semantics", () => {
    assert.equal(api.EchoString("a\ud800b\udc00", false), "a\ufffdb\ufffd");
});

test("returned strings use the current heap after native memory growth", () => {
    const size = api.GetTestHeapSize();
    assert.equal(api.EchoString(text, true), text);
    assert.ok(api.GetTestHeapSize() > size);
    assert.equal(api.GetStringDescLabel({Label: text}), text);
});

test("shared Wasm memory is decoded without exposing or retaining native bytes", async () => {
    // Exercise the shared-memory runtime branch even in the single-threaded
    // conformance build. The returned pointer and bytes use Wasm32 conventions.
    const {createBindings} = await import(new URL("./runtime.mjs", moduleURL));
    const memory = new WebAssembly.Memory({initial: 1, maximum: 2, shared: true});
    const heap = new Uint8Array(memory.buffer);
    heap.set(new TextEncoder().encode(text + "\0"), 16);
    let address = 16;
    const bindings = createBindings({HEAPU8: heap, getText: () => address}, {
        records: {}, interfaces: {}, enums: {},
        functions: {GetText: {export: "getText", parameters: [], result: {string: {nullable: false}}}},
    });
    const value = bindings.GetText();
    heap.fill(0);
    assert.equal(value, text);
    for (address of [heap.byteLength, -1]) {
        assert.throws(() => bindings.GetText(), /outside Wasm memory/);
    }
    address = heap.byteLength - 1;
    heap[address] = 65;
    assert.throws(() => bindings.GetText(), /no null terminator/);
});

test("all native string owners have been released", () => {
    assert.equal(api.GetLiveStringCount(), 0);
});
