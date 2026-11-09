import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE, RADIENT_STATUS_PENDING: PENDING,
        RADIENT_STATUS_INVALID_ARGUMENT: INVALID} = constants.RADIENT_STATUS;
    test("skeleton and skin descriptors, pose arrays and computed skinning outputs round-trip", t => {
        const f = createAssetFixture(t, api, assert, constants);
        const joints = [{Name: "Root", LocalRestTransform: {Position: {x: 1}}},
            {Name: "Child", ParentJointIndex: 0, LocalRestTransform: {Position: {y: 2}}}];
        const skeleton = f.keep(f.value(f.manager.CreateSkeleton({Name: "Rig 世界", JointCount: 2, pJoints: joints})));
        joints[1].Name = "changed";
        assert.equal(skeleton.GetDesc().pJoints[1].Name, "Child");
        assert.equal(skeleton.GetDesc().pJoints[0].ParentJointIndex, constants.InvalidRadientJointIndex);
        const inverse = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1]);
        const skin = f.keep(f.value(f.manager.CreateSkin({Name: "Skin", pSkeleton: skeleton,
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
        assert.equal(f.value(pose.GetJointLocalTransforms(0, 2))[1].Position.y, 2);
        assert.equal(f.value(pose.GetJointGlobalMatrices(0, 2))[1].Data[12], 1);
        assert.equal(f.value(pose.ComputeSkinningMatrices(skin)).length, 1); // Native JointCount query.
        const initialVersion = pose.GetVersion();
        const transforms = [{Position: {x: 4}}];
        assert.equal(writer.SetJointLocalTransforms(0, 1, transforms), OK);
        transforms[0].Position.x = 100;
        assert.equal(writer.Commit(false), OK);
        assert.equal(pose.GetVersion(), initialVersion);
        assert.equal(pose.GetJointGlobalMatrices(0, 2).status, PENDING);
        assert.equal(pose.ComputeSkinningMatrices(skin, false, false).status, PENDING);
        assert.equal(pose.UpdateGlobalTransforms(), OK);
        assert.ok(pose.GetVersion() > initialVersion);
        const matrices = f.value(pose.ComputeSkinningMatrices(skin));
        assert.equal(matrices[0].Data[12], 4);
        assert.equal(matrices[0].Data[13], 0);
        assert.equal(f.value(pose.ComputeSkinningMatrices(skin, true))[0].Data[3], 4);
        assert.equal(pose.UpdateGlobalTransforms(), NO_CHANGE);
        assert.equal(writer.ResetToRestPose(), OK);
        assert.equal(writer.Commit(true), OK);
        assert.equal(f.value(pose.GetJointLocalTransforms(0, 1))[0].Position.x, 1);
        assert.equal(writer.Commit(true), NO_CHANGE);
        assert.equal(writer.SetJointLocalTransforms(0, 0, null), NO_CHANGE);
        assert.equal(pose.GetJointLocalTransforms(2, 1).status, INVALID);
        assert.deepEqual(f.value(pose.GetJointGlobalMatrices(0, 0)), []);
    });

    test("scene skin snapshots retain their skin and pose after component removal", t => {
        const f = createAssetFixture(t, api, assert, constants);
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
