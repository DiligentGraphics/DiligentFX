// Shared runtime for generated Wasm32 bindings. Native pointers stay private.
import {typedArray, byteLength, createByteAccess} from "./byte_access.mjs";

const scalarArrays = {
    bool: Uint8Array, i8: Int8Array, u8: Uint8Array, i16: Int16Array, u16: Uint16Array,
    i32: Int32Array, u32: Uint32Array, i64: BigInt64Array, u64: BigUint64Array,
    float: Float32Array, double: Float64Array,
};

function inputScalar(value, type, name) {
    if (type === "bool") {
        if (typeof value !== "boolean") throw new TypeError(`${name} must be boolean`);
        return value;
    }
    if (type === "float" || type === "double") {
        if (typeof value !== "number") throw new TypeError(`${name} must be a number`);
        return value;
    }
    const bits = Number(type.slice(1));
    const unsigned = type[0] === "u";
    if (bits === 64) {
        if (typeof value !== "bigint") throw new TypeError(`${name} must be bigint`);
        const minimum = unsigned ? 0n : -(1n << 63n);
        const maximum = unsigned ? (1n << 64n) - 1n : (1n << 63n) - 1n;
        if (value < minimum || value > maximum) throw new RangeError(`${name} is outside ${type}`);
    } else {
        if (typeof value !== "number" || !Number.isInteger(value)) {
            throw new TypeError(`${name} must be an integer`);
        }
        const minimum = unsigned ? 0 : -(2 ** (bits - 1));
        const maximum = unsigned ? 2 ** bits - 1 : 2 ** (bits - 1) - 1;
        if (value < minimum || value > maximum) throw new RangeError(`${name} is outside ${type}`);
    }
    return value;
}

function outputScalar(value, type) {
    // Wasm exports i32/i64 values as signed values even for unsigned C++ types.
    if (type === "u32") return value >>> 0;
    if (type === "u64") return BigInt.asUintN(64, value);
    if (type === "bool") return value !== 0;
    return value;
}

export function createBindings(wasm, schema) {
    const api = Object.create(null);
    const states = new WeakMap();
    const types = new Map();
    const typeNames = new Map();
    const constructionToken = Symbol("native object");
    const textEncoder = new TextEncoder();
    // Preserve a leading U+FEFF as string content, and reject malformed UTF-8.
    const textDecoder = new TextDecoder("utf-8", {fatal: true, ignoreBOM: true});

    function pointer(object, expectedType) {
        const state = states.get(object);
        if (!state || (expectedType && !(object instanceof types.get(expectedType)))) {
            throw new TypeError("Expected a compatible native object from this module");
        }
        if (!state.pointer) throw new Error("Native object has been disposed");
        return state.pointer;
    }

    class NativeObject {
        constructor(token, address) {
            if (token !== constructionToken) throw new TypeError("Use a native factory to create objects");
            states.set(this, {pointer: address});
        }
        get disposed() {
            const state = states.get(this);
            if (!state) throw new TypeError("Invalid native object");
            return state.pointer === 0;
        }
        dispose() {
            const state = states.get(this);
            if (!state) throw new TypeError("Invalid native object");
            const address = state.pointer;
            state.pointer = 0;
            if (address) wasm._binding_Release(address);
        }
        QueryInterface(type) {
            const name = typeNames.get(type);
            if (!name) throw new TypeError("Expected an interface type from this module");
            return adopt(wasm[schema.interfaces[name].query](pointer(this)), name);
        }
    }

    function adopt(address, type) {
        address >>>= 0;
        if (!address) return null;
        try {
            return new (types.get(type))(constructionToken, address);
        } catch (error) {
            wasm._binding_Release(address);
            throw error;
        }
    }

    function record(value, name, cleanup, address) {
        if (!value || typeof value !== "object" || Array.isArray(value)) {
            throw new TypeError(`${name} must be a descriptor object`);
        }
        const spec = schema.records[name];
        // Nested fields reuse their parent's initialized storage and cleanup.
        if (address === undefined) {
            address = wasm[spec.create]() >>> 0;
            if (!address) throw new Error(`Could not allocate ${name}`);
            cleanup.push(() => wasm[spec.destroy](address));
        }
        const spans = [];
        for (const key of Object.keys(value)) {
            if (!Object.hasOwn(spec.fields, key)) throw new TypeError(`Unknown ${name} field: ${key}`);
            const field = spec.fields[key];
            const fieldValue = value[key];
            // Undefined, like omission, leaves the native default untouched.
            if (fieldValue !== undefined) {
                if (field.span) spans.push({field, value: fieldValue});
                else if (field.string) wasm[field.set](address, inputString(fieldValue, field.string, cleanup));
                else if (field.record) record(fieldValue, field.record, cleanup, wasm[field.address](address) >>> 0);
                else wasm[field.set](address, inputScalar(fieldValue, field.type, `${name}.${key}`));
            }
        }
        // Counts and native defaults are resolved before pointer/count fields,
        // regardless of the caller's descriptor property order.
        for (const {field, value: elements} of spans) {
            const length = BigInt.asUintN(64, wasm[field.span.getLength](address));
            wasm[field.set](address, inputSpan(elements, length, field.span, cleanup));
        }
        return address;
    }

    function outputRecord(address, name) {
        address >>>= 0;
        if (!address) throw new Error(`Native function returned a null ${name} reference`);
        const result = {};
        for (const [key, field] of Object.entries(schema.records[name].fields)) {
            const value = field.record ? outputRecord(wasm[field.address](address), field.record) :
                field.string ? outputString(wasm[field.get](address), field.string) :
                outputScalar(wasm[field.get](address), field.type);
            // Snapshot members are copied data properties, never native accessors.
            Object.defineProperty(result, key, {value, enumerable: true, writable: true, configurable: true});
        }
        return result;
    }

    function inputSpan(value, count, spec, cleanup) {
        const length = byteLength(count);
        if (value === null && spec.nullable) return 0;
        if (spec.record) {
            if (!Array.isArray(value)) throw new TypeError(`Expected an array of ${spec.record}`);
            if (length > value.length) throw new RangeError("Element count exceeds the input array");
            if (length > (wasm[spec.maxCount]() >>> 0)) throw new RangeError("Struct array exceeds the native allocation limit");
            if (length === 0) return 0;
            const address = wasm[spec.create](length) >>> 0;
            if (!address) throw new Error("Could not allocate struct array input");
            cleanup.push(() => wasm[spec.destroy](address));
            for (let i = 0; i < length; ++i) {
                record(value[i], spec.record, cleanup, wasm[spec.element](address, i) >>> 0);
            }
            return address;
        }

        // Preserve Uint8Array inputs accepted by the original signed-byte spans.
        const Type = spec.type === "i8" && value instanceof Uint8Array ? Uint8Array : scalarArrays[spec.type];
        const values = typedArray(value, Type);
        if (length > values.length) throw new RangeError("Element count exceeds the input array");
        const size = byteLength(length * Type.BYTES_PER_ELEMENT);
        if (size === 0) return 0;
        const address = wasm._malloc(size) >>> 0;
        if (!address) throw new Error("Could not allocate array input storage");
        cleanup.push(() => wasm._free(address));
        // Copy the selected view, not its entire backing buffer. Obtain the heap
        // after allocation, which may grow Wasm memory.
        wasm.HEAPU8.set(new Uint8Array(values.buffer, values.byteOffset, size), address);
        if (spec.type === "bool") {
            // Validate the copied representation before native bool objects are read.
            for (let i = 0; i < length; ++i) {
                if (wasm.HEAPU8[address + i] > 1) throw new RangeError("Boolean arrays require zero or one");
            }
        }
        return address;
    }

    function inputString(value, spec, cleanup) {
        if (value === null && spec.nullable) return 0;
        if (typeof value !== "string") throw new TypeError("Expected a string");
        if (value.includes("\0")) throw new TypeError("Native strings cannot contain embedded null characters");
        const bytes = textEncoder.encode(value);
        const address = wasm._malloc(bytes.byteLength + 1) >>> 0;
        if (!address) throw new Error("Could not allocate string input storage");
        cleanup.push(() => wasm._free(address));
        // Empty strings still have a terminator and are distinct from null.
        wasm.HEAPU8.set(bytes, address);
        wasm.HEAPU8[address + bytes.byteLength] = 0;
        return address;
    }

    function outputString(address, spec) {
        address >>>= 0;
        if (address === 0) {
            if (!spec.nullable) throw new Error("Native function returned a null non-nullable string");
            return null;
        }
        const heap = wasm.HEAPU8;
        if (address >= heap.byteLength) throw new RangeError("Native string is outside Wasm memory");
        const end = heap.indexOf(0, address);
        if (end === -1) throw new RangeError("Native string has no null terminator in Wasm memory");
        const bytes = heap.subarray(address, end);
        // TextDecoder does not accept SharedArrayBuffer views on all runtimes.
        // Decoding finishes before the native owner or temporary input is released.
        return textDecoder.decode(typeof SharedArrayBuffer !== "undefined" && heap.buffer instanceof SharedArrayBuffer ?
            bytes.slice() : bytes);
    }

    function acquireBytes(spec, owner, self, args) {
        if (args.length !== 0) throw new TypeError("Expected 0 arguments");
        const object = pointer(self, owner);
        const output = wasm._malloc(4) >>> 0;
        if (!output) throw new Error("Could not allocate byte access output");
        let acquired = false;
        let transferred = false;
        wasm._binding_AddRef(object);
        try {
            wasm.HEAPU8.fill(0, output, output + 4);
            const status = wasm[spec.acquire](object, output);
            if (status !== spec.success) return {status, value: null};
            acquired = true;
            const length = byteLength(BigInt.asUintN(64, wasm[spec.size](object)));
            const address = new DataView(wasm.HEAPU8.buffer).getUint32(output, true);
            if (length !== 0 && (address === 0 || address > wasm.HEAPU8.byteLength ||
                                length > wasm.HEAPU8.byteLength - address)) {
                throw new RangeError("Native byte access is outside Wasm memory");
            }
            const value = createByteAccess(wasm, address, length, spec.writable, () => {
                try { return wasm[spec.release](object); }
                finally { wasm._binding_Release(object); }
            });
            transferred = true;
            return {status, value};
        } finally {
            wasm._free(output);
            if (!transferred) {
                try { if (acquired) wasm[spec.release](object); }
                finally { wasm._binding_Release(object); }
            }
        }
    }

    function readOutput(address, spec) {
        // Obtain the current heap after the call: native allocations may grow it.
        const view = new DataView(wasm.HEAPU8.buffer);
        if (spec.interface) return adopt(view.getUint32(address, true), spec.interface);
        if (spec.string) return outputString(view.getUint32(address, true), spec.string);
        const readers = {
            bool: "getUint8", i8: "getInt8", u8: "getUint8", i16: "getInt16", u16: "getUint16",
            i32: "getInt32", u32: "getUint32", i64: "getBigInt64", u64: "getBigUint64",
            float: "getFloat32", double: "getFloat64",
        };
        return outputScalar(view[readers[spec.type]](address, true), spec.type);
    }

    function invoke(spec, owner, self, args) {
        if (spec.scope) return acquireBytes(spec.scope, owner, self, args);
        const inputs = spec.parameters.filter(p => !p.output);
        const argumentCount = inputs.length;
        if (args.length !== argumentCount) throw new TypeError(`Expected ${argumentCount} arguments`);
        const nativeArgs = owner ? [pointer(self, owner)] : [];
        const cleanup = [];
        let argument = 0;
        let output;
        try {
            for (const parameter of spec.parameters) {
                if (parameter.output) {
                    // Eight bytes cover every supported scalar and a Wasm32 pointer.
                    const address = wasm._malloc(8) >>> 0;
                    if (!address) throw new Error("Could not allocate output storage");
                    cleanup.push(() => wasm._free(address));
                    wasm.HEAPU8.fill(0, address, address + 8);
                    output = {address, spec: parameter.output};
                    nativeArgs.push(address);
                } else {
                    const value = args[argument++];
                    if (parameter.record) {
                        nativeArgs.push(record(value, parameter.record, cleanup));
                    } else if (parameter.interface) {
                        nativeArgs.push(value === null && parameter.nullable ? 0 : pointer(value, parameter.interface));
                    } else if (parameter.span) {
                        const span = parameter.span;
                        const count = inputScalar(args[inputs.findIndex(p => p.name === span.length)], span.lengthType, span.length);
                        nativeArgs.push(inputSpan(value, count, span, cleanup));
                    } else if (parameter.string) {
                        nativeArgs.push(inputString(value, parameter.string, cleanup));
                    } else {
                        nativeArgs.push(inputScalar(value, parameter.type, parameter.name));
                    }
                }
            }
            const nativeResult = wasm[spec.export](...nativeArgs);
            // Finish snapshots while the native owner and temporary inputs are
            // still alive. Only copied JS values escape the call.
            const result = spec.result.record ? outputRecord(nativeResult, spec.result.record) :
                spec.result.string ? outputString(nativeResult, spec.result.string) : outputScalar(nativeResult, spec.result);
            // Statuses are preserved verbatim; a nonzero status need not be failure.
            return output ? {status: result, value: readOutput(output.address, output.spec)} : result;
        } finally {
            for (let i = cleanup.length - 1; i >= 0; --i) cleanup[i]();
        }
    }

    function interfaceType(name) {
        if (types.has(name)) return types.get(name);
        const spec = schema.interfaces[name];
        const base = spec.base === "IObject" ? NativeObject : interfaceType(spec.base);
        const type = class extends base {};
        types.set(name, type);
        typeNames.set(type, name);
        for (const [method, callable] of Object.entries(spec.methods)) {
            Object.defineProperty(type.prototype, method, {
                value: function(...args) { return invoke(callable, name, this, args); },
            });
        }
        return type;
    }

    for (const name of Object.keys(schema.interfaces)) api[name] = interfaceType(name);
    for (const [name, values] of Object.entries(schema.enums)) api[name] = Object.freeze(values);
    for (const [name, spec] of Object.entries(schema.functions)) {
        api[name] = (...args) => invoke(spec, null, null, args);
    }
    return Object.freeze(api);
}
