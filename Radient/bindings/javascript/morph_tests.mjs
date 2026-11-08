import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE, RADIENT_STATUS_INVALID_ARGUMENT: INVALID,
        RADIENT_STATUS_NOT_FOUND: NOT_FOUND} = constants.RADIENT_STATUS;
    test("morph creation retains delta blobs and exposes editable weight snapshots", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        const deltas = f.blob(new Float32Array(27).fill(0.125));
        const targets = [0.25, -1, 0.75].map((DefaultWeight, i) => ({
            Desc: {Name: `Target ${i}`, DefaultWeight, AttributeCount: 1,
                pAttributes: [{Semantic: constants.RadientMorphTargetPositionSemantic, ComponentCount: 3}]},
            pAttributeData: [{pDeltaBuffer: deltas, ByteOffset: BigInt(i * 36)}],
        }));
        const mesh = f.asset(f.manager.CreateMesh(f.triangle({MorphTargetData: {pMorphTargets: targets, MorphTargetCount: 3}})));
        deltas.dispose();
        targets[0].Desc.Name = "changed";
        await f.ready(mesh);
        const desc = mesh.GetDesc();
        assert.equal(desc.MorphTargetCount, 3);
        assert.equal(desc.pMorphTargets[0].Name, "Target 0");
        assert.equal(desc.pMorphTargets[0].pAttributes[0].Semantic, "POSITION");
        const weights = f.keep(f.value(mesh.CreateMorphTargetWeights()));
        const retainedMesh = f.keep(weights.GetMesh());
        assert.equal(weights.GetWeightCount(), 3);
        const snapshot = weights.GetWeights();
        assert.ok(snapshot instanceof Float32Array);
        assert.deepEqual(Array.from(snapshot), [0.25, -1, 0.75]);
        const version = weights.GetVersion();
        assert.equal(weights.SetWeights(1, 2, new Float32Array([0.5, 1])), OK);
        assert.ok(weights.GetVersion() > version);
        assert.deepEqual(Array.from(weights.GetWeights()), [0.25, 0.5, 1]);
        assert.deepEqual(Array.from(snapshot), [0.25, -1, 0.75]);
        assert.equal(weights.SetWeights(0, 0, null), NO_CHANGE);
        assert.equal(weights.SetWeights(3, 1, new Float32Array([1])), INVALID);
        assert.equal(weights.ResetToDefaults(), OK);
        assert.deepEqual(Array.from(weights.GetWeights()), [0.25, -1, 0.75]);
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
