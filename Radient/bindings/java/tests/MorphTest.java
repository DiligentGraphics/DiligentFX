// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class MorphTest {
    public static void main(String[] args) {
        run("morph creation retains delta blobs and exposes editable weight snapshots", () -> {
            try (AssetFixture f = new AssetFixture()) {
                float[] defaults = Fixtures.floats("morph.defaultWeights");
                final int floatsPerTarget = 9; // Three POSITION deltas for the triangle.
                float[] values = new float[floatsPerTarget * defaults.length];
                Arrays.fill(values, Fixtures.number("morph.delta"));
                var deltas = f.blob(AssetFixture.floatBytes(values));
                RadientMorphTargetCreateInfo[] targets = new RadientMorphTargetCreateInfo[defaults.length];
                for (int i = 0; i < targets.length; ++i) {
                    targets[i] = f.morphTarget(deltas, (long)i * floatsPerTarget * Float.BYTES);
                    targets[i].Desc.Name = Fixtures.string("morph.targetName") + " " + i;
                    targets[i].Desc.DefaultWeight = defaults[i];
                }
                RadientMeshCreateInfo info = f.triangle();
                info.MorphTargetData.pMorphTargets = targets;
                info.MorphTargetData.MorphTargetCount = targets.length;
                var created = f.manager.CreateMesh(info);
                var mesh = f.asset(created.ppMesh, created.returnValue);
                deltas.close();
                targets[0].Desc.Name = "changed";
                f.ready(mesh);
                var desc = f.meshDesc(mesh);
                equal(3L, desc.MorphTargetCount);
                equal(Fixtures.string("morph.targetName") + " 0", desc.pMorphTargets[0].Name);
                equal("POSITION", desc.pMorphTargets[0].pAttributes[0].Semantic);
                var createdWeights = mesh.CreateMorphTargetWeights();
                var weights = f.value(createdWeights.ppWeights, createdWeights.returnValue);
                var retainedMesh = f.keep(weights.GetMesh());
                check(retainedMesh != null);
                equal(3L, weights.GetWeightCount());
                float[] snapshot = weights.GetWeights(); // Counterpart of JS's Float32Array.
                equal(defaults, snapshot);
                long version = weights.GetVersion();
                ok(weights.SetWeights(1, 2, Fixtures.floats("morph.editedWeights")));
                check(weights.GetVersion() > version);
                equal(Fixtures.floats("morph.updatedWeights"), weights.GetWeights());
                equal(defaults, snapshot);
                equal(RADIENT_STATUS_NO_CHANGE, weights.SetWeights(0, 0, null));
                equal(RADIENT_STATUS_INVALID_ARGUMENT, weights.SetWeights(3, 1, new float[]{1}));
                ok(weights.ResetToDefaults());
                equal(defaults, weights.GetWeights());
                long entity = f.entity();
                RadientMeshComponent meshComponent = new RadientMeshComponent();
                meshComponent.pMesh = mesh;
                ok(f.writer.SetMesh(entity, meshComponent));
                RadientMorphComponent morphComponent = new RadientMorphComponent();
                morphComponent.pWeights = weights;
                ok(f.writer.SetMorph(entity, morphComponent));
                var gotMorph = f.scene.GetMorph(entity);
                var retained = f.value(gotMorph.Morph.pWeights, gotMorph.returnValue);
                weights.close();
                mesh.close();
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_MESH));
                var removed = f.scene.GetMorph(entity);
                f.keep(removed.Morph.pWeights);
                equal(RADIENT_STATUS_NOT_FOUND, removed.returnValue);
                equal(3L, retained.GetWeightCount());
                equal(3L, f.meshDesc(retainedMesh).MorphTargetCount);
            }
        });
    }
}
