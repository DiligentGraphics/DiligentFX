export function registerTests({test, assert, api, wasm}) {
    const allocations = new Set();
    const descriptors = new Set();
    const owners = [];
    let creations = 0;
    function track(createName, destroyName, pending) {
        const create = wasm[createName];
        const destroy = wasm[destroyName];
        wasm[createName] = (...args) => {
            const address = create(...args) >>> 0;
            if (address) {
                assert.equal(pending.has(address), false);
                pending.add(address);
                ++creations;
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

    function check(name, callback) {
        test(name, t => {
            t.after(() => {
                // Use one cleanup hook so Node and browser hook ordering does
                // not affect the native lifetime checks.
                for (const owner of owners.splice(0)) owner.dispose();
                assert.equal(allocations.size, 0, "Temporary bytes leaked");
                assert.equal(descriptors.size, 0, "Temporary descriptors leaked");
                assert.equal(api.GetLiveSnapshotCount(), 0, "Snapshot retained its native owner");
            });
            callback();
        });
    }
    function object() {
        const {status, value} = api.CreateMarshalSnapshots();
        assert.equal(status, api.MARSHAL_STATUS.MARSHAL_STATUS_OK);
        assert.ok(value);
        owners.push(value);
        return value;
    }
    const text = "Aé水😀";
    const scalars = {Size: 0xfedcba9876543210n, Count: -13, Scale: 1.25, Weight: -2.5, Enabled: true, Mode: 241};
    function expected() {
        return {Tag: 41, Name: text, Values: {...scalars},
            Child: {Sequence: 0xfedcba98, Label: "native label", Offset: {x: 1.5, y: -2.5, z: 3.25}}};
    }

    check("struct getters copy scalar, enum, string, const, and nested fields", () => {
        const source = object();
        assert.deepEqual(source.GetDesc(), expected());
        assert.deepEqual(api.GetStaticDescriptor(), scalars);
    });

    check("struct results are independent plain JavaScript objects", () => {
        const source = object();
        const first = source.GetDesc();
        const second = source.GetDesc();
        assert.equal(Object.getPrototypeOf(first), Object.prototype);
        assert.equal(Object.getPrototypeOf(first.Child.Offset), Object.prototype);
        assert.ok(first !== second && first.Child !== second.Child && first.Child.Offset !== second.Child.Offset);
        first.Values.Count = 99;
        first.Child.Offset.y = 100;
        first.Child.Label = "edited JS";
        first.Name = "edited JS";
        assert.deepEqual(second, expected());
        assert.deepEqual(source.GetDesc(), expected());
    });

    check("snapshots survive native mutation and owner destruction", () => {
        const source = object();
        const first = source.GetDesc();
        // Replace native strings with heap allocations, then release all storage.
        source.Update(42, "changed " + text.repeat(400), "new label");
        const second = source.GetDesc();
        assert.equal(second.Name, "changed " + text.repeat(400));
        assert.equal(second.Values.Count, 42);
        assert.equal(second.Child.Label, "new label");
        assert.equal(second.Child.Offset.x, 42);
        source.dispose();
        assert.equal(api.GetLiveSnapshotCount(), 0);
        assert.deepEqual(first, expected());
        assert.equal(second.Name, "changed " + text.repeat(400));
        assert.equal(second.Child.Label, "new label");
        assert.equal(second.Child.Offset.x, 42);
        assert.throws(() => source.GetDesc(), /disposed/);
    });

    check("nullable output strings distinguish null from empty text", () => {
        const source = object();
        source.Update(0, "", null);
        const nullLabel = source.GetDesc();
        source.Update(1, "", "");
        const emptyLabel = source.GetDesc();
        assert.equal(nullLabel.Name, "");
        assert.equal(nullLabel.Child.Label, null);
        assert.equal(emptyLabel.Child.Label, "");
    });

    check("returned input descriptors are copied before temporary storage is released", () => {
        assert.deepEqual(api.EchoStringDescriptor({}, false), {Name: "native default", Label: null, Enabled: true});
        const input = {Name: text, Label: "", Enabled: false};
        const result = api.EchoStringDescriptor(input, false);
        assert.deepEqual(result, input);
        assert.equal(descriptors.size, 0);
        assert.equal(allocations.size, 0);
        input.Name = "changed input";
        // More calls reuse temporary native allocations but cannot change result.
        api.EchoStringDescriptor({Name: "replacement", Label: "other"}, false);
        assert.deepEqual(result, {Name: text, Label: "", Enabled: false});
    });

    check("invalid native strings fail snapshot conversion and release temporary inputs", () => {
        assert.throws(() => api.EchoStringDescriptor({Name: text, Label: "allocated"}, true), TypeError);
        const source = object();
        source.Update(0, null, null);
        assert.throws(() => source.GetDesc(), /null non-nullable string/);
        source.Update(0, text, "recovered");
        assert.equal(source.GetDesc().Name, text);
        assert.deepEqual(api.EchoStringDescriptor({Name: text}, false), {Name: text, Label: null, Enabled: true});
    });

    check("struct snapshots use current memory after the native getter grows the heap", () => {
        const source = object();
        const size = wasm.HEAPU8.byteLength;
        const result = source.GetDescAfterGrowingMemory();
        assert.ok(wasm.HEAPU8.byteLength > size, "The native getter must actually grow memory");
        source.dispose();
        assert.deepEqual(result, expected());
    });

    check("borrowed struct getters do not allocate temporary native descriptors", () => {
        const source = object();
        const before = creations;
        assert.deepEqual(source.GetDesc(), expected());
        assert.deepEqual(api.GetStaticDescriptor(), scalars);
        assert.equal(creations, before);
        assert.equal(api.GetLiveSnapshotCount(), 1);
        source.dispose();
        assert.equal(api.GetLiveSnapshotCount(), 0);
    });
}
