// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.Radient.RADIENT_ANIMATION_VALUE_TYPE.*;
import static diligent.radient.tests.TestSupport.*;
import static diligent.radient.tests.AnimationTestHelpers.*;

public final class AnimationTest {
    public static void main(String[] args) {
        run("clips copy sampler bytes and compiled bindings animate scene properties", () -> {
            try (AssetFixture f = new AssetFixture()) {
                float[] samples = Fixtures.floats("animation.nodeValues");
                byte[] values = AssetFixture.floatBytes(samples);
                var clip = createClip(f, RadientNodeAnimationSchemaID(), RadientNodeTranslationPropertyName,
                    RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, values);
                // Mutate the actual byte array submitted to JNI, not just its float source.
                Arrays.fill(samples, Fixtures.number("animation.mutatedValue"));
                System.arraycopy(AssetFixture.floatBytes(samples), 0, values, 0, values.length);
                var desc = clip.GetDesc();
                equal(Fixtures.string("animation.name"), desc.Name);
                equal(Fixtures.number("animation.duration"), desc.Duration);
                equal(Fixtures.floats("animation.times"), desc.pSamplers[0].pTimes);
                equal(Fixtures.floats("animation.nodeValues"), AssetFixture.floats(desc.pSamplers[0].pValues));
                long entity = f.entity();
                var binding = bindClip(f, clip, f.writer, entity);
                var retained = f.keep(binding.GetClip());
                check(retained != null);
                clip.close();
                ok(binding.Evaluate(evaluateAt("animation.time")));
                var local = f.scene.GetLocalTransform(entity);
                ok(local.returnValue);
                descriptorEqual(Fixtures.vector3("animation.position"), local.Transform.Position);
                var world = f.scene.GetWorldMatrix(entity);
                ok(world.returnValue);
                equal(Fixtures.vector3("animation.position").x, world.Matrix.Data[12]);
                var emptyResult = retained.CreateBinding(new RadientAnimationBindingDesc());
                var empty = f.value(emptyResult.ppBinding, emptyResult.returnValue);
                equal(RADIENT_STATUS_NO_CHANGE, empty.Evaluate(new RadientAnimationEvaluateInfo()));
            }
        });

        run("animation registry snapshots retain clips and bindings across every removal path", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var clip = createClip(f, RadientNodeAnimationSchemaID(), RadientNodeTranslationPropertyName,
                    RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, AssetFixture.floatBytes(new float[6]));
                long entity = f.entity();
                var binding = bindClip(f, clip, f.writer, entity);
                var created = f.engine.CreateAnimationRegistry(f.scene);
                var registry = f.value(created.ppRegistry, created.returnValue);
                var scene = f.keep(registry.GetScene());
                check(scene != null);
                ok(scene.IsEntityAlive(entity));
                long before = state(f, registry).Revision;
                long[] entities = {entity};
                ok(registry.AddAnimationBinding(binding, entities, 1));
                equal(RADIENT_STATUS_NO_CHANGE, registry.AddAnimationBinding(binding, entities, 1));
                var snapshot = state(f, registry);
                check(snapshot.Revision > before);
                equal(1L, snapshot.EntryCount);
                var entry = snapshot.pEntries[0];
                equal(1L, entry.BindingCount);
                ok(registry.RemoveAnimationBinding(binding, entities, 1));
                equal(0L, state(f, registry).EntryCount);
                ok(registry.AddAnimationBinding(binding, entities, 1));
                ok(registry.RemoveEntity(entity));
                ok(registry.AddAnimationBinding(binding, entities, 1));
                ok(registry.RemoveAnimationClip(clip));
                equal(RADIENT_STATUS_NO_CHANGE, registry.RemoveAnimationClip(clip));
                binding.close();
                clip.close();
                ok(entry.ppBindings[0].Evaluate(evaluateAt("animation.time")));
                equal(Fixtures.string("animation.name"), entry.pClip.GetDesc().Name);
            }
        });

        run("native quaternion helpers marshal their reference outputs", () -> {
            float angle = Fixtures.number("animation.rotationAngle");
            RadientQuaternion[] rotations = {MakeXRotation(angle).Rotation,
                MakeYRotation(angle).Rotation, MakeZRotation(angle).Rotation};
            float[] components = {rotations[0].x, rotations[1].y, rotations[2].z};
            float expected = Fixtures.number("animation.rotationComponent");
            for (int i = 0; i < rotations.length; ++i) {
                check(Math.abs(components[i] - expected) < 1e-6);
                check(Math.abs(rotations[i].w - expected) < 1e-6);
            }
            var axis = MakeAxisRotation(Fixtures.vector3("animation.rotationAxis"), angle).Rotation;
            descriptorEqual(MakeYRotation(angle).Rotation, axis);
        });

        run("compiled animation reaches real material, skeleton, and morph destinations", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var definition = f.definition();
                var material = f.material(definition);
                var colorClip = createClip(f, RadientMaterialAnimationSchemaID(), RadientStandardMaterialBaseColorFactorName,
                    RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, AssetFixture.floatBytes(Fixtures.floats("animation.colorValues")));
                var color = bindClip(f, colorClip, material, 0);
                ok(color.Evaluate(evaluateAt("animation.time")));
                var handle = f.parameter(definition, RadientStandardMaterialBaseColorFactorName);
                equal(Fixtures.floats("animation.color"), AssetFixture.floats(f.parameterBytes(material, handle, 16)));

                RadientSkeletonDesc skeletonDesc = new RadientSkeletonDesc();
                skeletonDesc.pJoints = new RadientSkeletonJointDesc[]{new RadientSkeletonJointDesc()};
                skeletonDesc.JointCount = 1;
                var createdSkeleton = f.manager.CreateSkeleton(skeletonDesc);
                var skeleton = f.value(createdSkeleton.ppSkeleton, createdSkeleton.returnValue);
                var createdPose = skeleton.CreatePose();
                var pose = f.value(createdPose.ppPose, createdPose.returnValue);
                var translationClip = createClip(f, RadientNodeAnimationSchemaID(), RadientNodeTranslationPropertyName,
                    RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, AssetFixture.floatBytes(Fixtures.floats("animation.poseValues")));
                ok(bindClip(f, translationClip, pose, 0).Evaluate(evaluateAt("animation.time")));
                var transforms = pose.GetJointLocalTransforms(0, 1);
                ok(transforms.returnValue);
                descriptorEqual(Fixtures.vector3("animation.position"), transforms.pTransforms[0].Position);

                RadientMeshCreateInfo meshInfo = f.triangle();
                meshInfo.MorphTargetData.pMorphTargets = new RadientMorphTargetCreateInfo[]{
                    f.morphTarget(f.blob(AssetFixture.floatBytes(new float[9])), 0)};
                meshInfo.MorphTargetData.MorphTargetCount = 1;
                var createdMesh = f.manager.CreateMesh(meshInfo);
                var mesh = f.ready(f.asset(createdMesh.ppMesh, createdMesh.returnValue));
                var createdWeights = mesh.CreateMorphTargetWeights();
                var weights = f.value(createdWeights.ppWeights, createdWeights.returnValue);
                var weightClip = createClip(f, RadientMorphWeightsAnimationSchemaID(), RadientMorphWeightsPropertyName,
                    RADIENT_ANIMATION_VALUE_TYPE_FLOAT, AssetFixture.floatBytes(Fixtures.floats("animation.weightValues")));
                ok(bindClip(f, weightClip, weights, 0).Evaluate(evaluateAt("animation.weightTime")));
                equal(Fixtures.number("animation.weight"), weights.GetWeights()[0]);
            }
        });
    }
}
