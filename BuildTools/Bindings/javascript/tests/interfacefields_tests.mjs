export function registerTests({test, assert, api}) {
    test("interface fields and arrays retain wrappers in snapshots", () => {
        const baseline = api.GetLiveObjectCount();
        const a = api.CreateMarshalObject({Size: 7n}, 0, false).value;
        const b = api.CreateMarshalObject({Size: 11n}, 0, true).value;
        try {
            assert.equal(api.SumInterfaces({pObject: a, ppObjects: [a, null, b], Count: 3}), 25n);
            assert.equal(api.SumInterfaceArray([a, b], 1), 7n);
            assert.equal(a.GetRefCount(), 1);
            const result = api.EchoInterfaces({pObject: a, ppObjects: [null, b], Count: 2});
            a.dispose(); b.dispose();
            assert.equal(result.pObject.GetSize(), 7n);
            assert.equal(result.ppObjects[1].GetSize(), 11n);
            assert.equal(result.ppObjects[0], null);
            result.pObject.dispose(); result.ppObjects[1].dispose();
            assert.equal(api.GetLiveObjectCount(), baseline);
            assert.throws(() => api.SumInterfaceArray([a], 1), /disposed/);
            assert.throws(() => api.SumInterfaceArray([{}], 1), TypeError);
        } finally { a.dispose(); b.dispose(); }
    });
    test("failed descriptor snapshots release borrowed fields and interface-array elements", () => {
        const baseline = api.GetLiveObjectCount();
        const destroyed = api.GetDestructionCount();
        const a = api.CreateMarshalObject({Size: 7n}, 0, false).value;
        const b = api.CreateMarshalObject({Size: 11n}, 0, true).value;
        try {
            for (let i = 0; i < 3; ++i) {
                assert.throws(() => api.GetInvalidInterfaceSnapshot({
                    pObject: a, ppObjects: [a, null, b], Count: 3,
                }), TypeError);
                assert.equal(a.GetRefCount(), 1);
                assert.equal(b.GetRefCount(), 1);
                assert.equal(api.GetLiveObjectCount(), baseline + 2);
                assert.equal(api.GetDestructionCount(), destroyed);
            }
        } finally {
            a.dispose(); b.dispose();
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
        assert.equal(api.GetDestructionCount(), destroyed + 2);
    });
    test("descriptor accessors cannot destroy already marshalled interface inputs", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 17n}, 0, false).value;
        assert.equal(api.SumInterfaces({pObject: object, get Count() {object.dispose(); return 0;}}), 17n);
        assert.equal(api.GetLiveObjectCount(), baseline);
        const retained = api.CreateMarshalObject({}, 0, false).value;
        try {
            assert.throws(() => api.SumInterfaces({pObject: retained, Count: 1, ppObjects: [{}]}), TypeError);
            assert.equal(retained.GetRefCount(), 1);
        } finally { retained.dispose(); }
    });
    test("borrowed interface arrays retain elements and allow empty inputs", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 17n}, 0, false).value;
        let result, copied;
        try {
            assert.equal(api.InterfaceArrayHasStorage([object], 1), true);
            result = api.EchoInterfaceArray([object, null, object], 3);
            assert.equal(result.length, 3);
            assert.equal(result[0] === result[2], false);
            assert.equal(result[1], null);
            assert.equal(object.GetRefCount(), 3);
            assert.deepEqual(api.EchoInterfaceArray([], 0), []);
            assert.equal(api.EchoInterfaces({}).ppObjects, null);
            copied = api.CopyInterfaceArray([object, null, object], 3, 2);
            assert.equal(copied.Written, 2);
            assert.equal(copied.ppOutput.length, 2);
            assert.equal(copied.ppOutput[1], null);
            assert.equal(object.GetRefCount(), 4);
            object.dispose();
            assert.equal(result[2].GetSize(), 17n);
            assert.equal(copied.ppOutput[0].GetSize(), 17n);
        } finally {
            object.dispose();
            for (const entry of result ?? []) entry?.dispose();
            for (const entry of copied?.ppOutput ?? []) entry?.dispose();
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
    test("required interface fields and array elements validate after reading the count", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 31n}, 0, false).value;
        let result;
        try {
            assert.throws(() => api.EchoRequiredInterfaces({pObject: null}), TypeError);
            const input = {pObject: object, ppObjects: [object, null], Count: 2};
            assert.throws(() => api.EchoRequiredInterfaces(input), TypeError);
            assert.throws(() => api.GetRequiredInterfaceSnapshot(input), /null interface/);
            assert.throws(() => api.GetRequiredInterfaceArray(input.ppObjects, 2), /null interface/);
            assert.equal(object.GetRefCount(), 1);
            result = api.EchoRequiredInterfaces({...input, Count: 1});
            assert.equal(result.ppObjects.length, 1);
            assert.equal(result.ppObjects[0].GetSize(), 31n);
            assert.equal(result.pObject.GetSize(), 31n);
        } finally {
            result?.pObject.dispose();
            for (const entry of result?.ppObjects ?? []) entry?.dispose();
            object.dispose();
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
    test("nested interface snapshots share rollback through fixed descriptor arrays", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 43n}, 0, false).value;
        const group = {
            Primary: {pObject: object, ppObjects: [object], Count: 1},
            Entries: [{pObject: object, ppObjects: [object, null], Count: 2},
                      {ppObjects: [object], Count: 1}],
        };
        let snapshot;
        try {
            assert.throws(() => api.EchoInterfaceGroup(group, true), TypeError);
            assert.equal(object.GetRefCount(), 1);
            snapshot = api.EchoInterfaceGroup(group, false);
            assert.equal(object.GetRefCount(), 6);
            object.dispose();
            assert.equal(snapshot.Primary.pObject.GetSize(), 43n);
            assert.equal(snapshot.Entries[1].ppObjects[0].GetSize(), 43n);
            assert.equal(snapshot.Entries[0].ppObjects[1], null);
            assert.equal(snapshot.Label, "interface group");
        } finally {
            object.dispose();
            for (const desc of snapshot ? [snapshot.Primary, ...snapshot.Entries] : []) {
                desc.pObject?.dispose();
                for (const entry of desc.ppObjects ?? []) entry?.dispose();
            }
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
    test("dynamic descriptor arrays retain nested interfaces and roll back failures", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 19n}, 0, false).value;
        const item = {pObject: object, ppObjects: [object, null], Count: 2};
        const desc = {pItems: [item, item], Count: 2};
        let snapshot, filled;
        function dispose(items) {
            for (const entry of items ?? []) {
                entry.pObject?.dispose();
                for (const value of entry.ppObjects ?? []) value?.dispose();
            }
        }
        try {
            assert.equal(api.SumInterfaceDescriptors(desc.pItems, 2), 76n);
            assert.throws(() => api.EchoInterfaceDescriptors(desc, true), TypeError);
            assert.throws(() => api.SumInterfaceDescriptors([item, {pObject: {}}], 2), TypeError);
            assert.equal(object.GetRefCount(), 1);
            assert.throws(() => api.FillInterfaceDescriptors(item, 1, true), RangeError);
            assert.equal(object.GetRefCount(), 1);
            snapshot = api.EchoInterfaceDescriptors(desc, false);
            assert.equal(object.GetRefCount(), 5);
            filled = api.FillInterfaceDescriptors(item, 5, false);
            assert.equal(filled.Written, 2);
            assert.equal(filled.pItems.length, 2);
            assert.equal(object.GetRefCount(), 9);
            object.dispose();
            assert.equal(snapshot.pItems[1].ppObjects[0].GetSize(), 19n);
            assert.equal(filled.pItems[1].pObject.GetSize(), 19n);
        } finally {
            object.dispose();
            dispose(snapshot?.pItems);
            dispose(filled?.pItems);
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
}
