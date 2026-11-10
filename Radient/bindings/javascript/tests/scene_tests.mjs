import {createSceneFixture} from "./scene_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {
        RADIENT_STATUS_OK: OK,
        RADIENT_STATUS_NO_CHANGE: NO_CHANGE,
        RADIENT_STATUS_NOT_FOUND: NOT_FOUND,
        RADIENT_STATUS_OUT_OF_DATE: OUT_OF_DATE,
    } = constants.RADIENT_STATUS;
    const {RADIENT_ENTITY_FLAG_VISIBLE: VISIBLE, RADIENT_ENTITY_FLAG_NONE: NONE} = constants.RADIENT_ENTITY_FLAGS;
    const invalid = constants.InvalidRadientEntityID;

    test("engine resource configuration accepts partial descriptors and validates integer types", t => {
        for (const Resources of [{}, {VertexPoolSize: 1024, MaxIndexBufferSize: 0n}]) {
            const result = api.CreateRadientEngine({WorkerThreadCount: 1, Resources});
            t.after(() => result.value?.dispose());
            assert.equal(result.status, OK);
            assert.ok(result.value !== null);
            result.value.dispose();
        }
        assert.throws(() => api.CreateRadientEngine({Resources: {MaxIndexBufferSize: 4096}}), TypeError);
        assert.throws(() => api.CreateRadientEngine({Resources: {TextureAtlasSize: 64n}}), TypeError);
        assert.throws(() => api.CreateRadientEngine({Resources: {VertexPoolSize: -1}}), RangeError);
    });

    test("scene and entity names are copied, with native defaults and bigint IDs", t => {
        const {engine, scene, writer, value} = createSceneFixture(t, api, assert, OK, "Scène 世界");
        assert.deepEqual(scene.GetDesc(), {Name: "Scène 世界"});
        const desc = {Name: "Child 🟢"};
        const entity = value(writer.CreateEntity(desc));
        desc.Name = "changed JS descriptor";
        assert.equal(typeof entity, "bigint");
        assert.ok(entity !== invalid);
        assert.equal(scene.IsEntityAlive(entity), OK);
        assert.equal(value(scene.GetParent(entity)), invalid);
        assert.equal(value(scene.GetEntityFlags(entity)), VISIBLE);
        assert.equal(value(scene.GetEntityOwnVisibility(entity)), true);
        assert.deepEqual(value(scene.GetLocalTransform(entity)), {
            Position: {x: 0, y: 0, z: 0}, Rotation: {x: 0, y: 0, z: 0, w: 1}, Scale: {x: 1, y: 1, z: 1},
        });
        const name = value(scene.GetEntityName(entity));
        assert.equal(name, "Child 🟢");
        assert.equal(writer.SetEntityName(entity, "Renamed"), OK);
        assert.equal(value(scene.GetEntityName(entity)), "Renamed");
        assert.equal(name, "Child 🟢");
        assert.equal(writer.SetEntityName(entity, "Renamed"), NO_CHANGE);
        assert.equal(writer.SetEntityName(entity, null), OK);
        assert.equal(value(scene.GetEntityName(entity)), "");
    });

    test("hierarchy enumeration copies written IDs and subtree destruction invalidates handles", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const root = value(writer.CreateEntity({}));
        const other = value(writer.CreateEntity({}));
        const first = value(writer.CreateEntity({Parent: root}));
        const second = value(writer.CreateEntity({Parent: root}));
        assert.equal(scene.GetRootEntityCount(), 2);
        const empty = value(scene.GetRootEntities(0));
        assert.equal(empty.NumEntitiesWritten, 0);
        assert.equal(empty.pEntities.length, 0);
        const limited = value(scene.GetRootEntities(1));
        assert.equal(limited.NumEntitiesWritten, 1);
        assert.deepEqual(Array.from(limited.pEntities), [root]);
        const roots = value(scene.GetRootEntities(5));
        assert.equal(roots.NumEntitiesWritten, 2);
        assert.ok(roots.pEntities instanceof BigUint64Array);
        assert.deepEqual(Array.from(roots.pEntities), [root, other]);
        assert.equal(value(scene.GetParent(second)), root);
        assert.equal(value(scene.GetChildCount(root)), 2);
        const children = value(scene.GetChildren(root, 0, 5));
        assert.equal(children.NumChildrenWritten, 2);
        assert.deepEqual(Array.from(children.pChildren), [first, second]);
        const page = value(scene.GetChildren(root, 1, 1));
        assert.equal(page.NumChildrenWritten, 1);
        assert.deepEqual(Array.from(page.pChildren), [second]);
        assert.equal(writer.DestroyEntity(root), OK);
        for (const entity of [root, first, second]) {
            assert.equal(scene.IsEntityAlive(entity), NOT_FOUND);
        }
        assert.deepEqual(scene.GetEntityName(first), {status: NOT_FOUND, value: null});
        assert.equal(scene.IsEntityAlive(other), OK);
        assert.deepEqual(Array.from(children.pChildren), [first, second]);
        assert.deepEqual(Array.from(roots.pEntities), [root, other]);
        assert.deepEqual(Array.from(value(scene.GetRootEntities(5)).pEntities), [other]);
    });

    test("nested transforms, matrix snapshots, revisions, and default reparenting cross the binding", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const parent = value(writer.CreateEntity({Transform: {Position: {x: 2, y: 3, z: 4}}}));
        const child = value(writer.CreateEntity({Parent: parent}));
        const revisions = scene.GetSceneRevisions();
        const transform = {Position: {x: 5, y: 6, z: 7}, Scale: {x: 2, y: 3, z: 4}};
        assert.equal(writer.SetLocalTransform(child, transform), OK);
        transform.Position.x = 999;
        assert.equal(value(scene.GetLocalTransform(child)).Position.x, 5);
        assert.equal(typeof revisions.Transforms, "bigint");
        assert.ok(scene.GetSceneRevisions().Transforms > revisions.Transforms);
        assert.equal(scene.GetCachedWorldMatrix(child).status, OUT_OF_DATE);
        assert.equal(writer.CommitChanges(), OK);
        const expected = [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 7, 9, 11, 1];
        const snapshot = value(scene.GetWorldMatrix(child));
        assert.ok(snapshot.Data instanceof Float32Array);
        assert.deepEqual(Array.from(snapshot.Data), expected);
        assert.deepEqual(Array.from(value(scene.GetCachedWorldMatrix(child)).Data), expected);
        snapshot.Data[12] = 999;
        assert.deepEqual(Array.from(value(scene.GetWorldMatrix(child)).Data), expected);
        assert.equal(writer.SetParent(child, invalid), OK); // Native default: keep world transform.
        assert.equal(value(scene.GetParent(child)), invalid);
        assert.deepEqual(Array.from(value(scene.GetWorldMatrix(child)).Data), expected);
        assert.equal(writer.SetParent(child, parent, false), OK);
        assert.equal(value(scene.GetWorldMatrix(child)).Data[12], 9);
    });

    test("visibility edits and cached queries preserve native status and boolean semantics", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const parent = value(writer.CreateEntity({}));
        const child = value(writer.CreateEntity({Parent: parent}));
        assert.equal(writer.CommitChanges(), OK);
        assert.equal(writer.SetEntityOwnVisibility(parent, false), OK);
        assert.equal(scene.GetCachedEntityEffectiveVisibility(child).status, OUT_OF_DATE);
        assert.equal(value(scene.GetEntityOwnVisibility(child)), true);
        assert.equal(value(scene.GetEntityEffectiveVisibility(child)), false);
        assert.equal(writer.SetEntityFlags(parent, VISIBLE), OK);
        assert.equal(writer.SetEntityFlags(child, NONE), OK);
        assert.equal(writer.CommitChanges(), OK);
        assert.equal(value(scene.GetCachedEntityEffectiveVisibility(parent)), true);
        assert.equal(value(scene.GetCachedEntityEffectiveVisibility(child)), false);
        assert.equal(value(scene.GetEntityFlags(child)), NONE);
    });

    test("borrowed scene wrappers retain the native scene after other owners are disposed", t => {
        const {engine, scene, writer, value} = createSceneFixture(t, api, assert, OK, "Retained scene");
        const entity = value(writer.CreateEntity({Name: "Retained entity"}));
        const retained = writer.GetScene();
        t.after(() => retained.dispose());
        writer.dispose();
        scene.dispose();
        engine.dispose();
        assert.equal(retained.IsEntityAlive(entity), OK);
        assert.deepEqual(retained.GetDesc(), {Name: "Retained scene"});
        assert.equal(value(retained.GetEntityName(entity)), "Retained entity");
        retained.dispose();
        assert.throws(() => retained.GetDesc(), /disposed/);
    });
}
