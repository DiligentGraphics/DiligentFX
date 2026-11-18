import {createAssetFixture} from "./asset_test_helpers.mjs";
import {readFixtures} from "./fixtures.mjs";
import {createClip, bindClip} from "./animation_test_helpers.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE} = constants.RADIENT_STATUS;
    const types = constants.RADIENT_ANIMATION_VALUE_TYPE;
    const data = readFixtures(testData);
    test("clips copy sampler bytes and compiled bindings animate scene properties", t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const values = new Float32Array(data.numbers("animation.nodeValues"));
        const clip = createClip(f, constants, constants.RadientNodeAnimationSchemaID,
            constants.RadientNodeTranslationPropertyName, types.RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, values);
        values.fill(data.number("animation.mutatedValue"));
        const desc = clip.GetDesc();
        assert.equal(desc.Name, data.string("animation.name"));
        assert.equal(desc.Duration, data.number("animation.duration"));
        assert.deepEqual(Array.from(desc.pSamplers[0].pTimes), data.numbers("animation.times"));
        assert.deepEqual(Array.from(new Float32Array(desc.pSamplers[0].pValues.buffer)), data.numbers("animation.nodeValues"));
        const entity = f.value(f.writer.CreateEntity({}));
        const binding = bindClip(f, api, clip, f.writer, entity);
        const retained = f.keep(binding.GetClip());
        clip.dispose();
        assert.equal(binding.Evaluate({Time: data.number("animation.time")}), OK);
        assert.deepEqual(f.value(f.scene.GetLocalTransform(entity)).Position, data.vector("animation.position"));
        assert.equal(f.value(f.scene.GetWorldMatrix(entity)).Data[12], data.vector("animation.position").x);
        const empty = f.keep(f.value(retained.CreateBinding({})));
        assert.equal(empty.Evaluate({}), NO_CHANGE);
    });

    test("animation registry snapshots retain clips and bindings across every removal path", t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const clip = createClip(f, constants, constants.RadientNodeAnimationSchemaID,
            constants.RadientNodeTranslationPropertyName, types.RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, new Float32Array(6));
        const entity = f.value(f.writer.CreateEntity({}));
        const binding = bindClip(f, api, clip, f.writer, entity);
        const registry = f.keep(f.value(f.engine.CreateAnimationRegistry(f.scene)));
        const scene = f.keep(registry.GetScene());
        assert.equal(scene.IsEntityAlive(entity), OK);
        const before = registry.GetState().Revision;
        const entities = new BigUint64Array([entity]);
        assert.equal(registry.AddAnimationBinding(binding, entities, 1), OK);
        assert.equal(registry.AddAnimationBinding(binding, entities, 1), NO_CHANGE);
        const state = registry.GetState();
        assert.ok(state.Revision > before);
        assert.equal(state.EntryCount, 1);
        const entry = state.pEntries[0];
        f.keep(entry.pClip);
        entry.ppBindings.forEach(f.keep);
        assert.equal(entry.BindingCount, 1);
        assert.equal(registry.RemoveAnimationBinding(binding, entities, 1), OK);
        assert.equal(registry.GetState().EntryCount, 0);
        assert.equal(registry.AddAnimationBinding(binding, entities, 1), OK);
        assert.equal(registry.RemoveEntity(entity), OK);
        assert.equal(registry.AddAnimationBinding(binding, entities, 1), OK);
        assert.equal(registry.RemoveAnimationClip(clip), OK);
        assert.equal(registry.RemoveAnimationClip(clip), NO_CHANGE);
        binding.dispose();
        clip.dispose();
        assert.equal(entry.ppBindings[0].Evaluate({Time: data.number("animation.time")}), OK);
        assert.equal(entry.pClip.GetDesc().Name, data.string("animation.name"));
    });

    test("native quaternion helpers marshal their reference outputs", () => {
        const angle = data.number("animation.rotationAngle");
        for (const [name, component] of [["MakeXRotation", "x"], ["MakeYRotation", "y"], ["MakeZRotation", "z"]]) {
            const q = api[name](angle);
            assert.ok(Math.abs(q[component] - data.number("animation.rotationComponent")) < 1e-6);
            assert.ok(Math.abs(q.w - data.number("animation.rotationComponent")) < 1e-6);
        }
        const axis = api.MakeAxisRotation(data.vector("animation.rotationAxis"), angle);
        assert.deepEqual(axis, api.MakeYRotation(angle));
    });

    test("compiled animation reaches real material, skeleton, and morph destinations", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const definition = f.keep(f.value(f.manager.CreateStandardMaterialDefinition({})));
        const material = f.keep(f.value(f.manager.CreateMaterial(definition)));
        const colorClip = createClip(f, constants, constants.RadientMaterialAnimationSchemaID,
            constants.RadientStandardMaterialBaseColorFactorName, types.RADIENT_ANIMATION_VALUE_TYPE_FLOAT4,
            new Float32Array(data.numbers("animation.colorValues")));
        const color = bindClip(f, api, colorClip, material);
        assert.equal(color.Evaluate({Time: data.number("animation.time")}), OK);
        const handle = f.value(definition.FindParameter(constants.RadientStandardMaterialBaseColorFactorName));
        assert.deepEqual(Array.from(new Float32Array(f.value(material.GetParameter(handle, 16)).buffer)), data.numbers("animation.color"));

        const skeleton = f.keep(f.value(f.manager.CreateSkeleton({JointCount: 1, pJoints: [{}]})));
        const pose = f.keep(f.value(skeleton.CreatePose()));
        const translationClip = createClip(f, constants, constants.RadientNodeAnimationSchemaID,
            constants.RadientNodeTranslationPropertyName, types.RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, new Float32Array(data.numbers("animation.poseValues")));
        assert.equal(bindClip(f, api, translationClip, pose).Evaluate({Time: data.number("animation.time")}), OK);
        assert.deepEqual(f.value(pose.GetJointLocalTransforms(0, 1))[0].Position, data.vector("animation.position"));

        const mesh = await f.ready(f.asset(f.manager.CreateMesh(f.triangle({MorphTargetData: {
            MorphTargetCount: 1, pMorphTargets: [f.morphTarget(f.blob(new Float32Array(9)))],
        }}))));
        const weights = f.keep(f.value(mesh.CreateMorphTargetWeights()));
        const weightClip = createClip(f, constants, constants.RadientMorphWeightsAnimationSchemaID,
            constants.RadientMorphWeightsPropertyName, types.RADIENT_ANIMATION_VALUE_TYPE_FLOAT, new Float32Array(data.numbers("animation.weightValues")));
        assert.equal(bindClip(f, api, weightClip, weights).Evaluate({Time: data.number("animation.weightTime")}), OK);
        assert.equal(weights.GetWeights()[0], data.number("animation.weight"));
    });
}
