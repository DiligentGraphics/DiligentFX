// The same UTF-8 properties file is read by Java's Properties.load(Reader).
// Keep the fixture format deliberately simple: key=value, comments, numeric lists.
export function readFixtures(text) {
    if (typeof text !== "string") throw new Error("Shared integration fixtures were not supplied");
    const values = new Map();
    for (const line of text.split(/\r?\n/)) {
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith("#")) continue;
        const separator = trimmed.indexOf("=");
        if (separator <= 0 || trimmed.includes("\\")) throw new Error(`Invalid fixture: ${line}`);
        const key = trimmed.slice(0, separator).trim();
        if (values.has(key)) throw new Error(`Duplicate fixture: ${key}`);
        values.set(key, trimmed.slice(separator + 1).trim());
    }
    function string(key) {
        if (!values.has(key)) throw new Error(`Missing fixture: ${key}`);
        return values.get(key);
    }
    function numbers(key) {
        const result = string(key).split(",").map(Number);
        if (result.some(value => !Number.isFinite(value))) throw new Error(`Invalid numbers: ${key}`);
        return result;
    }
    function number(key) {
        const result = numbers(key);
        if (result.length !== 1) throw new Error(`Expected one number: ${key}`);
        return result[0];
    }
    function boolean(key) {
        const value = string(key);
        if (value !== "true" && value !== "false") throw new Error(`Invalid boolean: ${key}`);
        return value === "true";
    }
    function vector(key) {
        const [x, y, z] = numbers(key);
        return z === undefined ? {x, y} : {x, y, z};
    }
    return {string, numbers, number, boolean, vector, bytes: key => Uint8Array.from(numbers(key))};
}
