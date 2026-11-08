// Shared runtime for generated Wasm32 bindings. Native pointers stay private.
import {byteArray, byteLength, createByteAccess} from "./byte_access.mjs";

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

    function record(value, name, cleanup) {
        if (!value || typeof value !== "object" || Array.isArray(value)) {
            throw new TypeError(`${name} must be a descriptor object`);
        }
        const spec = schema.records[name];
        const address = wasm[spec.create]() >>> 0;
        if (!address) throw new Error(`Could not allocate ${name}`);
        cleanup.push(() => wasm[spec.destroy](address));
        const buffers = [];
        for (const key of Object.keys(value)) {
            if (!Object.hasOwn(spec.fields, key)) throw new TypeError(`Unknown ${name} field: ${key}`);
            const field = spec.fields[key];
            const fieldValue = value[key];
            // Undefined, like omission, leaves the native default untouched.
            if (fieldValue !== undefined) {
                if (field.bytes) buffers.push({field, value: fieldValue});
                else wasm[field.set](address, inputScalar(fieldValue, field.type, `${name}.${key}`));
            }
        }
        // Length fields and native defaults are resolved before buffer fields,
        // regardless of the caller's descriptor property order.
        for (const {field, value: bytes} of buffers) {
            const length = BigInt.asUintN(64, wasm[field.bytes.getLength](address));
            wasm[field.set](address, inputBytes(bytes, length, field.bytes.nullable, cleanup));
        }
        return address;
    }

    function inputBytes(value, count, nullable, cleanup) {
        const length = byteLength(count);
        if (value === null && nullable) return 0;
        const bytes = byteArray(value);
        if (length > bytes.byteLength) throw new RangeError("Byte length exceeds the input buffer");
        if (length === 0) return 0;
        const address = wasm._malloc(length) >>> 0;
        if (!address) throw new Error("Could not allocate byte input storage");
        cleanup.push(() => wasm._free(address));
        wasm.HEAPU8.set(bytes.subarray(0, length), address);
        return address;
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
                    } else if (parameter.bytes) {
                        const span = parameter.bytes;
                        const count = inputScalar(args[inputs.findIndex(p => p.name === span.length)], span.lengthType, span.length);
                        nativeArgs.push(inputBytes(value, count, span.nullable, cleanup));
                    } else {
                        nativeArgs.push(inputScalar(value, parameter.type, parameter.name));
                    }
                }
            }
            const result = outputScalar(wasm[spec.export](...nativeArgs), spec.result);
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
