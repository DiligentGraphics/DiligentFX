import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";

const {default: loadBindings, MARSHAL_MODE, MARSHAL_STATUS} = await import(pathToFileURL(process.argv[2]));
const api = await loadBindings();
const {MARSHAL_STATUS_OK: OK, MARSHAL_STATUS_FAILED: FAILED, MARSHAL_STATUS_PENDING: PENDING} = MARSHAL_STATUS;
const wideValue = 0xfedcba9876543210n;

function object(t, mutable = true, status = OK, size = wideValue) {
    const result = api.CreateMarshalObject({Size: size}, status, mutable);
    assert.equal(result.status, status);
    assert.ok(result.value);
    t.after(() => result.value.dispose());
    return result.value;
}

test("all integer widths preserve boundary values", () => {
    for (const bits of [8, 16, 32, 64]) {
        for (const unsigned of [false, true]) {
            const method = `Echo${unsigned ? "Uint" : "Int"}${bits}`;
            const min = unsigned ? 0n : -(1n << BigInt(bits - 1));
            const max = (1n << BigInt(unsigned ? bits : bits - 1)) - 1n;
            for (const value of [min, 0n, max]) {
                const argument = bits === 64 ? value : Number(value);
                assert.equal(api[method](argument), argument, method);
            }
        }
    }
});

test("native checks independently verify values and parameter order", () => {
    assert.equal(api.CheckScalars(-101, 0xfedcba98, -0x123456789abcdefn, wideValue,
                                 1.25, -2.5, true, MARSHAL_MODE.MARSHAL_MODE_LAST), true);
    assert.equal(api.GetWideConstant(), wideValue);
});

test("floating point, booleans, and enums use their native representations", () => {
    assert.equal(api.EchoFloat(0.1), Math.fround(0.1));
    assert.equal(api.EchoDouble(0.1), 0.1);
    assert.ok(Object.is(api.EchoFloat(-0), -0));
    assert.ok(Number.isNaN(api.EchoDouble(NaN)));
    assert.equal(api.EchoFloat(Infinity), Infinity);
    assert.equal(api.InvertBool(true), false);
    assert.equal(api.InvertBool(false), true);
    assert.equal(api.EchoMode(MARSHAL_MODE.MARSHAL_MODE_LAST), 241);
    assert.deepEqual(api.MARSHAL_MODE, MARSHAL_MODE);
    assert.ok(Object.isFrozen(MARSHAL_MODE));
    for (const status of [OK, PENDING, FAILED]) assert.equal(api.EchoStatus(status), status);
});

test("input validation prevents truncation and 64-bit precision loss", () => {
    for (const bits of [8, 16, 32, 64]) {
        for (const unsigned of [false, true]) {
            const method = `Echo${unsigned ? "Uint" : "Int"}${bits}`;
            const min = unsigned ? 0n : -(1n << BigInt(bits - 1));
            const max = (1n << BigInt(unsigned ? bits : bits - 1)) - 1n;
            for (const value of [min - 1n, max + 1n]) {
                assert.throws(() => api[method](bits === 64 ? value : Number(value)), RangeError);
            }
        }
    }
    for (const value of [1.5, NaN, Infinity, "1", null, undefined, true]) {
        assert.throws(() => api.EchoInt32(value), TypeError);
    }
    assert.throws(() => api.EchoUint64(Number(wideValue)), TypeError);
    assert.throws(() => api.EchoUint32(1n), TypeError);
    assert.throws(() => api.InvertBool(1), TypeError);
    assert.throws(() => api.EchoDouble("1"), TypeError);
    assert.throws(() => api.EchoInt32(), TypeError);
    assert.throws(() => api.EchoInt32(1, 2), TypeError);
});

test("descriptor defaults are initialized by native code", () => {
    assert.equal(api.CheckDefaultDesc({}), true);
    assert.equal(api.CheckDefaultDesc({Count: undefined}), true);
    assert.equal(api.CheckDefaultDesc({Count: 19}), false);
});

test("descriptor fields reach native code unchanged", () => {
    assert.equal(api.CheckCustomDesc({
        Size: 0x123456789abcdef0n, Count: 19, Scale: -3.5,
        Weight: 0.125, Enabled: false, Mode: MARSHAL_MODE.MARSHAL_MODE_FIRST,
    }), true);
    assert.throws(() => api.CheckCustomDesc({Count: 2 ** 32}), RangeError);
    assert.throws(() => api.CheckCustomDesc({Unknown: 1}), TypeError);
    assert.throws(() => api.CheckCustomDesc(null), TypeError);
    assert.throws(() => api.CheckCustomDesc([]), TypeError);
    const error = new Error("descriptor getter failed");
    assert.throws(() => api.CheckCustomDesc({get Count() { throw error; }}), error);
    assert.equal(api.CheckDefaultDesc({}), true);
});

test("scalar outputs are initialized and statuses preserved", () => {
    assert.deepEqual(api.ReadWideOutput(false), {status: OK, value: wideValue});
    assert.deepEqual(api.ReadWideOutput(true), {status: FAILED, value: 0n});
    assert.equal(api.RecordValue(0xfedcba98), undefined);
    assert.equal(api.GetRecordedValue(), 0xfedcba98);
});

test("factories adopt one owned reference and dispose exactly once", t => {
    const destroyed = api.GetDestructionCount();
    assert.equal(api.GetLiveObjectCount(), 0);
    const value = object(t);
    assert.equal(value.GetSize(), wideValue);
    assert.equal(value.GetRefCount(), 1);
    assert.equal(api.GetLiveObjectCount(), 1);
    assert.equal(value.disposed, false);
    value.dispose();
    value.dispose();
    assert.equal(value.disposed, true);
    assert.equal(api.GetLiveObjectCount(), 0);
    assert.equal(api.GetDestructionCount(), destroyed + 1);
    assert.throws(() => value.GetSize(), /disposed/);
});

test("failure outputs are null and pending outputs remain usable", t => {
    assert.deepEqual(api.CreateMarshalObject({}, FAILED, false), {status: FAILED, value: null});
    const value = object(t, true, PENDING);
    assert.equal(value.GetSize(), wideValue);
    assert.equal(value.GetRefCount(), 1);
});

test("QueryInterface retains ownership and derived methods dispatch correctly", t => {
    const base = object(t);
    const derived = base.QueryInterface(api.IMarshalMutableObject);
    assert.ok(derived instanceof api.IMarshalObject);
    t.after(() => derived.dispose());
    assert.equal(base.GetRefCount(), 2);
    assert.equal(derived.SetSize(123n), PENDING);
    assert.equal(base.GetSize(), 123n);
    base.dispose();
    assert.equal(derived.GetRefCount(), 1);
    assert.equal(derived.GetSize(), 123n);
    const queriedBase = derived.QueryInterface(api.IMarshalObject);
    t.after(() => queriedBase.dispose());
    assert.equal(queriedBase.GetSize(), 123n);
    assert.equal(derived.GetRefCount(), 2);
});

test("unsupported QueryInterface returns null without retaining", t => {
    const value = object(t, false);
    assert.equal(value.QueryInterface(api.IMarshalMutableObject), null);
    assert.equal(value.GetRefCount(), 1);
    assert.throws(() => new api.IMarshalObject(), TypeError);
    assert.throws(() => value.QueryInterface({}), TypeError);
});

test("borrowed interface inputs accept null without changing ownership", t => {
    const value = object(t);
    assert.equal(api.GetObjectSize(value), wideValue);
    assert.equal(api.GetObjectSize(null), 0n);
    assert.equal(value.GetRefCount(), 1);
    assert.throws(() => api.GetObjectSize({}), TypeError);
    assert.throws(() => api.GetObjectSize(undefined), TypeError);
    value.dispose();
    assert.throws(() => api.GetObjectSize(value), /disposed/);
});

test("wrappers and interface types cannot cross module instances", async t => {
    const other = await loadBindings();
    const value = object(t);
    assert.throws(() => other.GetObjectSize(value), TypeError);
    assert.throws(() => value.QueryInterface(other.IMarshalObject), TypeError);
    assert.throws(() => other.IMarshalObject.prototype.GetSize.call(value), TypeError);
    assert.throws(() => api.IMarshalObject.prototype.GetSize.call({}), TypeError);
});

test("all owned objects have been released", () => {
    assert.equal(api.GetLiveObjectCount(), 0);
});
