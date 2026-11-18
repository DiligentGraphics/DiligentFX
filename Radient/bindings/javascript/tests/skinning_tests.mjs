import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE, RADIENT_STATUS_PENDING: PENDING,
        RADIENT_STATUS_INVALID_ARGUMENT: INVALID} = constants.RADIENT_STATUS;
    test("skeleton and skin descriptors, pose arrays and computed skinning outputs round-trip", t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const joints = [
            {Name: f.data.string("skinning.rootName"),
                LocalRestTransform: {Position: {x: f.data.vector("skinning.rootPosition").x}}},
            {Name: f.data.string("skinning.childName"), ParentJointIndex: 0,
                LocalRestTransform: {Position: {y: f.data.vector("skinning.childPosition").y}}},
        ];
        const skeleton = f.keep(f.value(f.manager.CreateSkeleton({Name: f.data.string("skinning.skeletonName"), JointCount: 2, pJoints: joints})));
        joints[1].Name = "changed";
        assert.equal(skeleton.GetDesc().pJoints[1].Name, f.data.string("skinning.childName"));
        assert.equal(skeleton.GetDesc().pJoints[0].ParentJointIndex, constants.InvalidRadientJointIndex);
        const inverse = new Float32Array(f.data.numbers("skinning.inverseBind"));
        const skin = f.keep(f.value(f.manager.CreateSkin({Name: f.data.string("skinning.skinName"), pSkeleton: skeleton,
            JointCount: 1, pJoints: [{SkeletonJointIndex: 1, InverseBindMatrix: {Data: inverse}}]})));
        const skinDesc = skin.GetDesc();
        f.keep(skinDesc.pSkeleton);
        assert.equal(skinDesc.JointCount, 1);
        assert.deepEqual(Array.from(skinDesc.pJoints[0].InverseBindMatrix.Data), Array.from(inverse));
        const pose = f.keep(f.value(skeleton.CreatePose()));
        const retainedSkeleton = f.keep(pose.GetSkeleton());
        const writer = f.keep(f.value(pose.CreateWriter()));
        skeleton.dispose();
        assert.equal(retainedSkeleton.GetDesc().JointCount, 2);
        assert.equal(f.value(pose.GetJointLocalTransforms(0, 2))[1].Position.y, f.data.vector("skinning.childPosition").y);
        assert.equal(f.value(pose.GetJointGlobalMatrices(0, 2))[1].Data[12], f.data.vector("skinning.rootPosition").x);
        assert.equal(f.value(pose.ComputeSkinningMatrices(skin)).length, 1); // Native JointCount query.
        const initialVersion = pose.GetVersion();
        const transforms = [{Position: {x: f.data.vector("skinning.updatedPosition").x}}];
        assert.equal(writer.SetJointLocalTransforms(0, 1, transforms), OK);
        transforms[0].Position.x = f.data.number("skinning.mutatedRootX");
        assert.equal(writer.Commit(false), OK);
        assert.equal(pose.GetVersion(), initialVersion);
        assert.equal(pose.GetJointGlobalMatrices(0, 2).status, PENDING);
        assert.equal(pose.ComputeSkinningMatrices(skin, false, false).status, PENDING);
        assert.equal(pose.UpdateGlobalTransforms(), OK);
        assert.ok(pose.GetVersion() > initialVersion);
        const matrices = f.value(pose.ComputeSkinningMatrices(skin));
        assert.equal(matrices[0].Data[12], f.data.vector("skinning.skinnedPosition").x);
        assert.equal(matrices[0].Data[13], f.data.vector("skinning.skinnedPosition").y);
        assert.equal(f.value(pose.ComputeSkinningMatrices(skin, true))[0].Data[3], f.data.vector("skinning.skinnedPosition").x);
        assert.equal(pose.UpdateGlobalTransforms(), NO_CHANGE);
        assert.equal(writer.ResetToRestPose(), OK);
        assert.equal(writer.Commit(true), OK);
        assert.equal(f.value(pose.GetJointLocalTransforms(0, 1))[0].Position.x, f.data.vector("skinning.rootPosition").x);
        assert.equal(writer.Commit(true), NO_CHANGE);
        assert.equal(writer.SetJointLocalTransforms(0, 0, null), NO_CHANGE);
        assert.equal(pose.GetJointLocalTransforms(2, 1).status, INVALID);
        assert.deepEqual(f.value(pose.GetJointGlobalMatrices(0, 0)), []);
    });

    test("scene skin snapshots retain their skin and pose after component removal", t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const skeleton = f.keep(f.value(f.manager.CreateSkeleton({JointCount: 1, pJoints: [{}]})));
        const skin = f.keep(f.value(f.manager.CreateSkin({pSkeleton: skeleton, JointCount: 1, pJoints: [{SkeletonJointIndex: 0}]})));
        const pose = f.keep(f.value(skeleton.CreatePose()));
        const entity = f.value(f.writer.CreateEntity({}));
        assert.equal(f.writer.SetSkin(entity, {pSkin: skin, pPose: pose}), OK);
        const component = f.value(f.scene.GetSkin(entity));
        f.keep(component.pSkin);
        f.keep(component.pPose);
        skin.dispose();
        pose.dispose();
        assert.equal(f.writer.RemoveComponent(entity, constants.RADIENT_COMPONENT_TYPE_SKIN), OK);
        assert.equal(f.value(component.pPose.ComputeSkinningMatrices(component.pSkin)).length, 1);
    });
}
