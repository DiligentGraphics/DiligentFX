// The small assertion subset used by the shared binding tests. Node uses its
// standard assert module; the browser adapter compares only plain test data.
function describe(value) {
    return JSON.stringify(value, (_key, item) => typeof item === "bigint" ? `${item}n` : item) ?? String(value);
}

function sameData(actual, expected) {
    if (Object.is(actual, expected)) return true;
    if (actual === null || expected === null || typeof actual !== "object" || typeof expected !== "object") return false;
    if (Object.getPrototypeOf(actual) !== Object.getPrototypeOf(expected)) return false;
    if (!Array.isArray(actual) && !(actual instanceof Uint8Array) && Object.getPrototypeOf(actual) !== Object.prototype) {
        throw new TypeError("deepEqual only supports plain objects, arrays, and Uint8Array");
    }
    if (Array.isArray(actual) && actual.length !== expected.length) return false;
    const keys = Object.keys(actual);
    return keys.length === Object.keys(expected).length &&
        keys.every(key => Object.hasOwn(expected, key) && sameData(actual[key], expected[key]));
}

export const assert = {
    fail(message = "Assertion failed") { throw new Error(message); },
    ok(value, message = "Expected a truthy value") {
        if (!value) this.fail(message);
    },
    equal(actual, expected, message = "Values differ") {
        if (!Object.is(actual, expected)) this.fail(`${message}: ${describe(actual)} !== ${describe(expected)}`);
    },
    deepEqual(actual, expected, message = "Values differ") {
        if (!sameData(actual, expected)) this.fail(`${message}: ${describe(actual)} !== ${describe(expected)}`);
    },
    throws(callback, expected) {
        let thrown = false;
        let error;
        try { callback(); } catch (caught) { thrown = true; error = caught; }
        if (!thrown) this.fail("Expected an exception");
        if (expected instanceof RegExp) {
            this.ok(expected.test(String(error?.message ?? error)), `Unexpected exception: ${error}`);
        } else if (typeof expected === "function") {
            this.ok(error instanceof expected, `Expected ${expected.name}, received ${error}`);
        } else if (expected instanceof Error) {
            this.equal(error, expected, "Unexpected exception object");
        } else {
            throw new TypeError("Unsupported exception expectation");
        }
    },
};
