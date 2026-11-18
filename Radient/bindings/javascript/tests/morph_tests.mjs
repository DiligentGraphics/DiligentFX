import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE, RADIENT_STATUS_INVALID_ARGUMENT: INVALID,
        RADIENT_STATUS_NOT_FOUND: NOT_FOUND} = constants.RADIENT_STATUS;
    test("morph creation retains delta blobs and exposes editable weight snapshots", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const defaults = f.data.numbers("morph.defaultWeights");
        const floatsPerTarget = 9; // Three POSITION deltas for the triangle.
        const deltas = f.blob(new Float32Array(floatsPerTarget * defaults.length).fill(f.data.number("morph.delta")));
        const targets = defaults.map((DefaultWeight, i) => {
            const target = f.morphTarget(deltas, BigInt(i * floatsPerTarget * Float32Array.BYTES_PER_ELEMENT));
            Object.assign(target.Desc, {Name: `${f.data.string("morph.targetName")} ${i}`, DefaultWeight});
            return target;
        });
        const mesh = f.asset(f.manager.CreateMesh(f.triangle({MorphTargetData: {pMorphTargets: targets, MorphTargetCount: targets.length}})));
        deltas.dispose();
        targets[0].Desc.Name = "changed";
        await f.ready(mesh);
        const desc = mesh.GetDesc();
        assert.equal(desc.MorphTargetCount, 3);
        assert.equal(desc.pMorphTargets[0].Name, `${f.data.string("morph.targetName")} 0`);
        assert.equal(desc.pMorphTargets[0].pAttributes[0].Semantic, "POSITION");
        const weights = f.keep(f.value(mesh.CreateMorphTargetWeights()));
        const retainedMesh = f.keep(weights.GetMesh());
        assert.equal(weights.GetWeightCount(), 3);
        const snapshot = weights.GetWeights();
        assert.ok(snapshot instanceof Float32Array);
        assert.deepEqual(Array.from(snapshot), defaults);
        const version = weights.GetVersion();
        assert.equal(weights.SetWeights(1, 2, new Float32Array(f.data.numbers("morph.editedWeights"))), OK);
        assert.ok(weights.GetVersion() > version);
        assert.deepEqual(Array.from(weights.GetWeights()), f.data.numbers("morph.updatedWeights"));
        assert.deepEqual(Array.from(snapshot), defaults);
        assert.equal(weights.SetWeights(0, 0, null), NO_CHANGE);
        assert.equal(weights.SetWeights(3, 1, new Float32Array([1])), INVALID);
        assert.equal(weights.ResetToDefaults(), OK);
        assert.deepEqual(Array.from(weights.GetWeights()), defaults);
        const entity = f.value(f.writer.CreateEntity({}));
        assert.equal(f.writer.SetMesh(entity, {pMesh: mesh}), OK);
        assert.equal(f.writer.SetMorph(entity, {pWeights: weights}), OK);
        const retained = f.keep(f.value(f.scene.GetMorph(entity)).pWeights);
        weights.dispose();
        mesh.dispose();
        assert.equal(f.writer.RemoveComponent(entity, constants.RADIENT_COMPONENT_TYPE_MESH), OK);
        assert.equal(f.scene.GetMorph(entity).status, NOT_FOUND);
        assert.equal(retained.GetWeightCount(), 3);
        assert.equal(retainedMesh.GetDesc().MorphTargetCount, 3);
    });
}
