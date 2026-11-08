export function registerTests({test, assert, api, wasm}) {
    const createName = "_binding_Diligent_MarshalComputedMatrix_ArrayCreate";
    const destroyName = "_binding_Diligent_MarshalComputedMatrix_ArrayDestroy";
    const elementName = "_binding_Diligent_MarshalComputedMatrix_ArrayElement";

    function trackMatrices(t) {
        const create = wasm[createName];
        const destroy = wasm[destroyName];
        const pending = new Set();
        const counts = [];
        wasm[createName] = count => {
            counts.push(count);
            const address = create(count);
            if (address) pending.add(address >>> 0);
            return address;
        };
        wasm[destroyName] = address => {
            assert.equal(pending.delete(address >>> 0), true);
            destroy(address);
        };
        t.after(() => {
            wasm[createName] = create;
            wasm[destroyName] = destroy;
            assert.equal(pending.size, 0);
        });
        return {pending, counts};
    }

    test("matrix outputs use the source descriptor count and native allocation", t => {
        const {pending, counts} = trackMatrices(t);
        // The receiver and source differ, so querying the receiver would fail.
        const receiver = api.CreateComputedOutputs(9n).value;
        const source = api.CreateComputedOutputs(2n).value;
        t.after(() => { receiver.dispose(); source.dispose(); });
        const result = receiver.FillMatrices(source);
        assert.equal(result.status, 3);
        assert.equal(result.value.length, 2);
        assert.deepEqual(Array.from(result.value[0].Data), Array.from({length: 16}, (_, i) => i + 1));
        assert.deepEqual(Array.from(result.value[1].Data), Array.from({length: 16}, (_, i) => i + 101));
        assert.deepEqual(counts, [2]);
        assert.equal(pending.size, 0);
        const before = wasm.HEAPU8.byteLength;
        source.SetCount(3n);
        assert.equal(receiver.FillMatrices(source, false, true).value.length, 3);
        assert.ok(wasm.HEAPU8.byteLength > before);
        // The earlier result remains JS-owned after subsequent calls and growth.
        assert.equal(result.value[1].Data[15], 116);
        assert.deepEqual(counts, [2, 3]);
    });

    test("zero and oversized native counts avoid output allocation", t => {
        const {counts} = trackMatrices(t);
        const source = api.CreateComputedOutputs(0n).value;
        t.after(() => source.dispose());
        assert.deepEqual(source.FillMatrices(source).value, []);
        assert.deepEqual(Array.from(api.FillComputedValues(source, {Size: 5n})), []);
        source.SetCount(1n << 30n);
        assert.throws(() => source.FillMatrices(source), RangeError);
        source.SetCount(0xffffffffffffffffn);
        assert.throws(() => source.FillMatrices(source), RangeError);
        assert.throws(() => api.FillComputedValues(source, {}), RangeError);
        assert.deepEqual(counts, []);
    });

    test("native failures and snapshot exceptions release output arrays", t => {
        const {pending} = trackMatrices(t);
        const source = api.CreateComputedOutputs(2n).value;
        t.after(() => source.dispose());
        const failed = source.FillMatrices(source, true);
        assert.equal(failed.status, -7);
        assert.equal(failed.value.length, 2);
        assert.deepEqual(Array.from(failed.value[0].Data), new Array(16).fill(0));
        assert.equal(pending.size, 0);
        const element = wasm[elementName];
        try {
            wasm[elementName] = () => { throw new Error("snapshot failure"); };
            assert.throws(() => source.FillMatrices(source), /snapshot failure/);
            assert.equal(pending.size, 0);
        } finally { wasm[elementName] = element; }
        assert.equal(source.FillMatrices(source).value.length, 2);
    });

    test("native counts are queried after all inputs are marshalled and retained", t => {
        const baseline = api.GetLiveComputedOutputs();
        const source = api.CreateComputedOutputs(1n).value;
        t.after(() => source.dispose());
        const values = api.FillComputedValues(source, {
            get Size() {
                source.SetCount(3n);
                source.dispose();
                return 41n;
            }
        });
        assert.deepEqual(Array.from(values), [41, 42, 43]);
        assert.equal(api.GetLiveComputedOutputs(), baseline);
    });

    test("invalid inputs do not query or allocate computed outputs", t => {
        const {counts} = trackMatrices(t);
        const source = api.CreateComputedOutputs(2n).value;
        t.after(() => source.dispose());
        for (const value of [null, undefined, {}]) {
            assert.throws(() => source.FillMatrices(value), TypeError);
            assert.throws(() => api.FillComputedValues(value, {}), TypeError);
        }
        assert.throws(() => api.FillComputedValues(source, {Size: "bad"}), TypeError);
        source.dispose();
        assert.throws(() => api.FillComputedValues(source, {}), /disposed/);
        assert.deepEqual(counts, []);
    });
}
