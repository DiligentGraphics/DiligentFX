// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class SkinningTest {
    public static void main(String[] args) {
        run("skeleton and skin descriptors, pose arrays and computed skinning outputs round-trip", () -> {
            try (AssetFixture f = new AssetFixture()) {
                RadientSkeletonJointDesc root = new RadientSkeletonJointDesc();
                root.Name = Fixtures.string("skinning.rootName");
                root.LocalRestTransform.Position.x = Fixtures.vector3("skinning.rootPosition").x;
                RadientSkeletonJointDesc child = new RadientSkeletonJointDesc();
                child.Name = Fixtures.string("skinning.childName");
                child.ParentJointIndex = 0;
                child.LocalRestTransform.Position.y = Fixtures.vector3("skinning.childPosition").y;
                RadientSkeletonDesc info = new RadientSkeletonDesc();
                info.Name = Fixtures.string("skinning.skeletonName");
                info.pJoints = new RadientSkeletonJointDesc[]{root, child};
                info.JointCount = 2;
                var created = f.manager.CreateSkeleton(info);
                var skeleton = f.value(created.ppSkeleton, created.returnValue);
                child.Name = "changed";
                equal(Fixtures.string("skinning.childName"), skeleton.GetDesc().pJoints[1].Name);
                equal(InvalidRadientJointIndex, skeleton.GetDesc().pJoints[0].ParentJointIndex);
                float[] inverse = Fixtures.floats("skinning.inverseBind");
                RadientSkinJointBindingDesc joint = new RadientSkinJointBindingDesc();
                joint.SkeletonJointIndex = 1;
                joint.InverseBindMatrix.Data = inverse;
                RadientSkinDesc skinInfo = new RadientSkinDesc();
                skinInfo.Name = Fixtures.string("skinning.skinName");
                skinInfo.pSkeleton = skeleton;
                skinInfo.pJoints = new RadientSkinJointBindingDesc[]{joint};
                skinInfo.JointCount = 1;
                var createdSkin = f.manager.CreateSkin(skinInfo);
                var skin = f.value(createdSkin.ppSkin, createdSkin.returnValue);
                var skinDesc = skin.GetDesc();
                f.keep(skinDesc.pSkeleton);
                equal(1L, skinDesc.JointCount);
                equal(inverse, skinDesc.pJoints[0].InverseBindMatrix.Data);
                var createdPose = skeleton.CreatePose();
                var pose = f.value(createdPose.ppPose, createdPose.returnValue);
                var retainedSkeleton = f.keep(pose.GetSkeleton());
                check(retainedSkeleton != null);
                var createdWriter = pose.CreateWriter();
                var writer = f.value(createdWriter.ppWriter, createdWriter.returnValue);
                skeleton.close();
                equal(2L, retainedSkeleton.GetDesc().JointCount);
                var local = pose.GetJointLocalTransforms(0, 2);
                ok(local.returnValue);
                equal(Fixtures.vector3("skinning.childPosition").y, local.pTransforms[1].Position.y);
                var global = pose.GetJointGlobalMatrices(0, 2);
                ok(global.returnValue);
                equal(Fixtures.vector3("skinning.rootPosition").x, global.pMatrices[1].Data[12]);
                // Java spells out the same defaults used by JS: no transpose, update globals.
                var initial = pose.ComputeSkinningMatrices(skin, false, true);
                ok(initial.returnValue);
                equal(1, initial.pMatrices.length); // Native JointCount query, not skeleton count.
                long initialVersion = pose.GetVersion();
                RadientTransform transform = new RadientTransform();
                transform.Position.x = Fixtures.vector3("skinning.updatedPosition").x;
                ok(writer.SetJointLocalTransforms(0, 1, new RadientTransform[]{transform}));
                transform.Position.x = Fixtures.number("skinning.mutatedRootX");
                ok(writer.Commit(false));
                equal(initialVersion, pose.GetVersion());
                equal(RADIENT_STATUS_PENDING, pose.GetJointGlobalMatrices(0, 2).returnValue);
                equal(RADIENT_STATUS_PENDING, pose.ComputeSkinningMatrices(skin, false, false).returnValue);
                ok(pose.UpdateGlobalTransforms());
                check(pose.GetVersion() > initialVersion);
                var matrices = pose.ComputeSkinningMatrices(skin, false, true);
                ok(matrices.returnValue);
                equal(Fixtures.vector3("skinning.skinnedPosition").x, matrices.pMatrices[0].Data[12]);
                equal(Fixtures.vector3("skinning.skinnedPosition").y, matrices.pMatrices[0].Data[13]);
                var transposed = pose.ComputeSkinningMatrices(skin, true, true);
                ok(transposed.returnValue);
                equal(Fixtures.vector3("skinning.skinnedPosition").x, transposed.pMatrices[0].Data[3]);
                equal(RADIENT_STATUS_NO_CHANGE, pose.UpdateGlobalTransforms());
                ok(writer.ResetToRestPose());
                ok(writer.Commit(true));
                var reset = pose.GetJointLocalTransforms(0, 1);
                ok(reset.returnValue);
                equal(Fixtures.vector3("skinning.rootPosition").x, reset.pTransforms[0].Position.x);
                equal(RADIENT_STATUS_NO_CHANGE, writer.Commit(true));
                equal(RADIENT_STATUS_NO_CHANGE, writer.SetJointLocalTransforms(0, 0, null));
                equal(RADIENT_STATUS_INVALID_ARGUMENT, pose.GetJointLocalTransforms(2, 1).returnValue);
                var empty = pose.GetJointGlobalMatrices(0, 0);
                ok(empty.returnValue);
                equal(new RadientMatrix4x4[0], empty.pMatrices);
            }
        });

        run("scene skin snapshots retain their skin and pose after component removal", () -> {
            try (AssetFixture f = new AssetFixture()) {
                RadientSkeletonDesc info = new RadientSkeletonDesc();
                info.pJoints = new RadientSkeletonJointDesc[]{new RadientSkeletonJointDesc()};
                info.JointCount = 1;
                var created = f.manager.CreateSkeleton(info);
                var skeleton = f.value(created.ppSkeleton, created.returnValue);
                RadientSkinJointBindingDesc joint = new RadientSkinJointBindingDesc();
                joint.SkeletonJointIndex = 0;
                RadientSkinDesc skinInfo = new RadientSkinDesc();
                skinInfo.pSkeleton = skeleton;
                skinInfo.pJoints = new RadientSkinJointBindingDesc[]{joint};
                skinInfo.JointCount = 1;
                var createdSkin = f.manager.CreateSkin(skinInfo);
                var skin = f.value(createdSkin.ppSkin, createdSkin.returnValue);
                var createdPose = skeleton.CreatePose();
                var pose = f.value(createdPose.ppPose, createdPose.returnValue);
                long entity = f.entity();
                RadientSkinComponent component = new RadientSkinComponent();
                component.pSkin = skin;
                component.pPose = pose;
                ok(f.writer.SetSkin(entity, component));
                var snapshot = f.scene.GetSkin(entity);
                f.keep(snapshot.Skin.pSkin);
                f.keep(snapshot.Skin.pPose);
                ok(snapshot.returnValue);
                skin.close();
                pose.close();
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_SKIN));
                var matrices = snapshot.Skin.pPose.ComputeSkinningMatrices(snapshot.Skin.pSkin, false, true);
                ok(matrices.returnValue);
                equal(1, matrices.pMatrices.length);
            }
        });
    }
}
