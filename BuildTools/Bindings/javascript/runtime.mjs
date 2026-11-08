// Shared runtime for generated Wasm32 bindings. Native pointers stay private.
import {typedArray, byteLength, createByteAccess} from "./byte_access.mjs";

// Snapshot file inputs before asynchronous module initialization. Workers use
// the normal native filesystem, so asset loading needs no JavaScript callbacks.
export function prepareModuleOptions(options) {
    const {files, ...result} = options;
    if (files === undefined) return result;
    if (files === null || typeof files !== "object" || Array.isArray(files)) {
        throw new TypeError("files must map absolute paths to Uint8Array data");
    }
    const entries = Object.entries(files).map(([path, data]) => {
        if (!path.startsWith("/") || path.includes("\\") || path.includes("\0") ||
            path.slice(1).split("/").some(part => part === "" || part === "." || part === "..")) {
            throw new TypeError("Virtual file paths must be absolute POSIX file paths");
        }
        return [path, typedArray(data, Uint8Array).slice()];
    });
    const hooks = result.preRun === undefined ? [] : Array.isArray(result.preRun) ? result.preRun : [result.preRun];
    result.preRun = [module => {
        for (const [path, data] of entries) {
            module.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")) || "/");
            module.FS.writeFile(path, data);
        }
    }, ...hooks];
    return result;
}

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

export function createBindings(wasm, schema, constants = {}) {
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

    function inputInterface(value, spec, cleanup) {
        if (value === null && spec.nullable) return 0;
        const address = pointer(value, spec.interface);
        // Pin inputs until native dispatch and result snapshots finish, including
        // when later JS descriptor accessors dispose an earlier wrapper.
        wasm._binding_AddRef(address);
        cleanup.push(() => wasm._binding_Release(address));
        return address;
    }

    function outputInterface(address, spec, rollback) {
        address >>>= 0;
        if (!address && !spec.nullable) throw new Error("Native function returned a null interface");
        if (address && spec.borrowed) wasm._binding_AddRef(address);
        const value = adopt(address, spec.interface);
        if (value) rollback.push(() => value.dispose());
        return value;
    }

    function inputRecordPointer(value, spec, cleanup) {
        return value === null && spec.nullable ? 0 : record(value, spec.recordPointer, cleanup);
    }

    function outputRecordPointer(address, spec, rollback) {
        if ((address >>> 0) === 0 && spec.nullable) return null;
        return outputRecord(address, spec.recordPointer, rollback);
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
                if (field.array) inputFixedArray(fieldValue, wasm[field.address](address) >>> 0, field.array, cleanup);
                else if (field.span) spans.push({field, value: fieldValue});
                else if (field.recordPointer) wasm[field.set](address, inputRecordPointer(fieldValue, field, cleanup));
                else if (field.interface) wasm[field.set](address, inputInterface(fieldValue, field, cleanup));
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

    function outputRecord(address, name, rollback) {
        address >>>= 0;
        if (!address) throw new Error(`Native function returned a null ${name} reference`);
        const result = {};
        for (const [key, field] of Object.entries(schema.records[name].fields)) {
            const value = field.array ? outputFixedArray(wasm[field.address](address) >>> 0, field.array, rollback) :
                field.span ? outputSpan(wasm[field.get](address),
                BigInt.asUintN(64, wasm[field.span.getLength](address)), field.span, rollback) :
                field.record ? outputRecord(wasm[field.address](address), field.record, rollback) :
                field.recordPointer ? outputRecordPointer(wasm[field.get](address), field, rollback) :
                field.interface ? outputInterface(wasm[field.get](address), field, rollback) :
                field.string ? outputString(wasm[field.get](address), field.string) :
                outputScalar(wasm[field.get](address), field.type);
            // Snapshot members are copied data properties, never native accessors.
            Object.defineProperty(result, key, {value, enumerable: true, writable: true, configurable: true});
        }
        return result;
    }

    function inputFixedArray(value, address, spec, cleanup) {
        if (!spec.array) {
            inputSpan(value, spec.count, spec, cleanup, address);
            return;
        }
        if (!Array.isArray(value)) throw new TypeError("Expected an array of fixed-size arrays");
        if (value.length !== spec.count) throw new RangeError(`Fixed array requires exactly ${spec.count} elements`);
        for (let i = 0; i < spec.count; ++i) {
            inputFixedArray(value[i], wasm[spec.element](address, i) >>> 0, spec.array, cleanup);
        }
    }

    function outputFixedArray(address, spec, rollback) {
        if (!spec.array) return outputSpan(address, spec.count, spec, rollback);
        const result = new Array(spec.count);
        for (let i = 0; i < spec.count; ++i) result[i] = outputFixedArray(wasm[spec.element](address, i) >>> 0, spec.array, rollback);
        return result;
    }

    function outputSpan(pointer, count, spec, rollback) {
        const address = pointer >>> 0;
        const length = byteLength(count);
        const Type = spec.interface ? Uint32Array : spec.record ? null : scalarArrays[spec.type];
        // A null empty span preserves explicit nullability; a non-null empty
        // span is an empty array. No storage is read for a zero count.
        if (length === 0) return address === 0 && spec.nullable ? null : (spec.record || spec.interface) ? [] : new Type();
        const size = spec.record ? wasm[spec.size]() >>> 0 : Type.BYTES_PER_ELEMENT;
        const alignment = spec.record ? wasm[spec.alignment]() >>> 0 : Type.BYTES_PER_ELEMENT;
        // Check the complete range before allocating JS storage or dereferencing
        // any native elements. Division avoids wrapping count * element size.
        if (address === 0 || address % alignment !== 0 || address > wasm.HEAPU8.byteLength ||
            length > Math.floor((wasm.HEAPU8.byteLength - address) / size)) {
            throw new RangeError("Native array is outside Wasm memory or misaligned");
        }
        if (spec.record) {
            const result = new Array(length);
            for (let i = 0; i < length; ++i) result[i] = outputRecord(wasm[spec.element](address, i), spec.record, rollback);
            return result;
        }
        if (spec.interface) {
            const result = new Array(length);
            for (let i = 0; i < length; ++i) {
                const object = new DataView(wasm.HEAPU8.buffer).getUint32(address + i * 4, true);
                result[i] = outputInterface(object, spec.interface, rollback);
            }
            return result;
        }
        // Copy bytes into JS-owned storage, never return a view of native memory.
        // This also works when the Wasm heap uses SharedArrayBuffer.
        const result = new Type(length);
        new Uint8Array(result.buffer).set(wasm.HEAPU8.subarray(address, address + length * size));
        return result;
    }

    function inputSpan(value, count, spec, cleanup, address) {
        const length = byteLength(count);
        const embedded = address !== undefined;
        if (value === null && spec.nullable) return 0;
        if (spec.interface) {
            if (!Array.isArray(value)) throw new TypeError("Expected an interface array");
            if (length > value.length) throw new RangeError("Element count exceeds the input array");
            if (length === 0) return 0;
            const size = byteLength(length * 4);
            address = wasm._malloc(size) >>> 0;
            if (!address) throw new Error("Could not allocate interface array");
            cleanup.push(() => wasm._free(address));
            for (let i = 0; i < length; ++i) {
                const object = inputInterface(value[i], spec.interface, cleanup);
                new DataView(wasm.HEAPU8.buffer).setUint32(address + i * 4, object, true);
            }
            return address;
        }
        if (spec.record) {
            if (!Array.isArray(value)) throw new TypeError(`Expected an array of ${spec.record}`);
            if (embedded && length !== value.length) throw new RangeError(`Fixed array requires exactly ${length} elements`);
            if (length > value.length) throw new RangeError("Element count exceeds the input array");
            if (length === 0) return 0;
            if (!embedded) {
                if (length > (wasm[spec.maxCount]() >>> 0)) throw new RangeError("Struct array exceeds the native allocation limit");
                address = wasm[spec.create](length) >>> 0;
                if (!address) throw new Error("Could not allocate struct array input");
                cleanup.push(() => wasm[spec.destroy](address));
            }
            for (let i = 0; i < length; ++i) {
                record(value[i], spec.record, cleanup, wasm[spec.element](address, i) >>> 0);
            }
            return address;
        }

        // Preserve Uint8Array inputs accepted by the original signed-byte spans.
        const Type = spec.type === "i8" && value instanceof Uint8Array ? Uint8Array : scalarArrays[spec.type];
        const values = typedArray(value, Type);
        if (embedded && length !== values.length) throw new RangeError(`Fixed array requires exactly ${length} elements`);
        if (length > values.length) throw new RangeError("Element count exceeds the input array");
        const size = byteLength(length * Type.BYTES_PER_ELEMENT);
        if (size === 0) return 0;
        if (!embedded) {
            address = wasm._malloc(size) >>> 0;
            if (!address) throw new Error("Could not allocate array input storage");
            cleanup.push(() => wasm._free(address));
        }
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

    function readOutput(address, spec, rollback = []) {
        // Obtain the current heap after the call: native allocations may grow it.
        if (spec.record) return outputRecord(address, spec.record, rollback);
        const view = new DataView(wasm.HEAPU8.buffer);
        if (spec.interface) return outputInterface(view.getUint32(address, true), spec, rollback);
        if (spec.string) return outputString(view.getUint32(address, true), spec.string);
        const readers = {
            bool: "getUint8", i8: "getInt8", u8: "getUint8", i16: "getInt16", u16: "getUint16",
            i32: "getInt32", u32: "getUint32", i64: "getBigInt64", u64: "getBigUint64",
            float: "getFloat32", double: "getFloat64",
        };
        return outputScalar(view[readers[spec.type]](address, true), spec.type);
    }

    function allocateOutput(spec, capacity, cleanup) {
        if (spec.buffer) {
            const buffer = spec.buffer;
            if (capacity === 0) return 0;
            if (buffer.record) {
                if (capacity > (wasm[buffer.maxCount]() >>> 0)) throw new RangeError("Output array exceeds allocation limit");
                const address = wasm[buffer.create](capacity) >>> 0;
                if (!address) throw new Error("Could not allocate output array");
                cleanup.push(() => wasm[buffer.destroy](address));
                return address;
            }
            const size = byteLength(capacity * (buffer.interface ? 4 : scalarArrays[buffer.type].BYTES_PER_ELEMENT));
            const address = wasm._malloc(size) >>> 0;
            if (!address) throw new Error("Could not allocate output array");
            cleanup.push(() => wasm._free(address));
            wasm.HEAPU8.fill(0, address, address + size);
            return address;
        }
        const recordSpec = spec.record && schema.records[spec.record];
        const address = (recordSpec ? wasm[recordSpec.create]() : wasm._malloc(8)) >>> 0;
        if (!address) throw new Error("Could not allocate output storage");
        cleanup.push(() => recordSpec ? wasm[recordSpec.destroy](address) : wasm._free(address));
        if (!recordSpec) wasm.HEAPU8.fill(0, address, address + 8);
        return address;
    }

    function invoke(spec, owner, self, args) {
        if (spec.scope) return acquireBytes(spec.scope, owner, self, args);
        const inputs = spec.parameters.filter(p => !p.output);
        const argumentCount = inputs.length;
        if (spec.defaults && Object.keys(spec.defaults).length) {
            while (args.length && args[args.length - 1] === undefined) args = args.slice(0, -1);
        }
        const dispatch = args.length === argumentCount ? spec.export : spec.defaults?.[args.length];
        if (!dispatch) throw new TypeError(`Expected ${argumentCount} arguments or a supported default-argument form`);
        const nativeArgs = owner ? [pointer(self, owner)] : [];
        const cleanup = [];
        if (owner && args.length) inputInterface(self, {interface: owner, nullable: false}, cleanup);
        let argument = 0;
        const outputs = [];
        const rollback = [];
        let completed = false;
        try {
            for (const parameter of spec.parameters) {
                if (parameter.output) {
                    outputs.push({name: parameter.name, spec: parameter.output,
                                  index: nativeArgs.length, consumed: false});
                    nativeArgs.push(0);
                } else {
                    if (parameter.default && argument >= args.length) {
                        ++argument;
                        continue;
                    }
                    const value = args[argument++];
                    if (parameter.recordPointer) {
                        nativeArgs.push(inputRecordPointer(value, parameter, cleanup));
                    } else if (parameter.record) {
                        nativeArgs.push(record(value, parameter.record, cleanup));
                    } else if (parameter.interface) {
                        nativeArgs.push(inputInterface(value, parameter, cleanup));
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
            // Resolve native counts after every input has been marshalled and
            // retained. Later descriptor getters may modify or dispose earlier
            // interface wrappers; no JS getters run between sizing and dispatch.
            for (const output of outputs) {
                const buffer = output.spec.buffer;
                let capacity = 0;
                if (buffer?.lengthQuery) {
                    const query = buffer.lengthQuery;
                    const sourceIndex = spec.parameters.findIndex(p => p.name === query.parameter) + (owner ? 1 : 0);
                    capacity = byteLength(BigInt.asUintN(64, wasm[query.export](nativeArgs[sourceIndex])));
                } else if (buffer) {
                    capacity = byteLength(inputScalar(args[inputs.findIndex(p => p.name === buffer.length)], buffer.lengthType, buffer.length));
                }
                const address = allocateOutput(output.spec, capacity, cleanup);
                output.address = address;
                output.capacity = capacity;
                nativeArgs[output.index] = address;
                if (output.spec.interface && !output.spec.borrowed) cleanup.push(() => {
                    if (!output.consumed) {
                        const object = new DataView(wasm.HEAPU8.buffer).getUint32(address, true);
                        if (object) wasm._binding_Release(object);
                    }
                });
            }
            const nativeResult = wasm[dispatch](...nativeArgs);
            // Finish snapshots while the native owner and temporary inputs are
            // still alive. Only copied JS values escape the call.
            const result = spec.result.recordPointer ? outputRecordPointer(nativeResult, spec.result, rollback) : spec.result.interface ? outputInterface(nativeResult, spec.result, rollback) : spec.result.span ? outputSpan(nativeResult,
                spec.result.span.lengthExport ? BigInt.asUintN(64, wasm[spec.result.span.lengthExport](nativeArgs[0])) :
                args[inputs.findIndex(p => p.name === spec.result.span.length)], spec.result.span, rollback) :
                spec.result.record ? outputRecord(nativeResult, spec.result.record, rollback) :
                spec.result.string ? outputString(nativeResult, spec.result.string) : outputScalar(nativeResult, spec.result);
            // Statuses are preserved verbatim; a nonzero status need not be failure.
            const values = {};
            for (const output of outputs) {
                let value;
                if (output.spec.buffer) {
                    const buffer = output.spec.buffer;
                    const countOutput = buffer.written && outputs.find(o => o.name === buffer.written);
                    const count = countOutput ? byteLength(readOutput(countOutput.address, countOutput.spec)) : output.capacity;
                    if (count > output.capacity) throw new RangeError("Written count exceeds output capacity");
                    value = outputSpan(output.address, count, buffer, rollback);
                } else {
                    output.consumed = true;
                    value = readOutput(output.address, output.spec, rollback);
                }
                Object.defineProperty(values, output.name, {value, enumerable: true});
            }
            completed = true;
            if (!outputs.length) return result;
            const value = outputs.length === 1 ? values[outputs[0].name] : values;
            return spec.result === "void" ? value : {status: result, value};
        } finally {
            if (!completed) for (let i = rollback.length - 1; i >= 0; --i) rollback[i]();
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

    for (const [name, value] of Object.entries(constants)) api[name] = value;
    for (const name of Object.keys(schema.interfaces)) api[name] = interfaceType(name);
    for (const [name, values] of Object.entries(schema.enums)) api[name] = Object.freeze(values);
    for (const [name, spec] of Object.entries(schema.functions)) {
        api[name] = (...args) => invoke(spec, null, null, args);
    }
    return Object.freeze(api);
}
