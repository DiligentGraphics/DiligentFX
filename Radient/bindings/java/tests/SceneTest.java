// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class SceneTest {
    private static RadientEntityDesc childOf(long parent) {
        RadientEntityDesc desc = new RadientEntityDesc();
        desc.Parent = parent;
        return desc;
    }

    private static RadientMatrix4x4 world(IRadientScene scene, long entity) {
        var result = scene.GetWorldMatrix(entity);
        ok(result.returnValue);
        return result.Matrix;
    }

    @SuppressWarnings("try")
    public static void main(String[] args) {
        run("Scene.NativeDefaultsNamesAndRetainedScene", () -> {
            try (SceneFixture f = new SceneFixture()) {
                equal("Scène 世界", f.scene.GetDesc().Name);
                RadientEntityDesc desc = new RadientEntityDesc();
                equal(InvalidRadientEntityID, desc.Parent);
                equal(1.f, desc.Transform.Scale.x);
                equal(1.f, desc.Transform.Rotation.w);
                desc.Name = "Child 🟢";
                long entity = f.entity(desc);
                desc.Name = "changed Java value";
                var original = f.scene.GetEntityName(entity);
                ok(original.returnValue);
                equal("Child 🟢", original.Name);
                ok(f.writer.SetEntityName(entity, "Renamed"));
                equal(RADIENT_STATUS_NO_CHANGE, f.writer.SetEntityName(entity, "Renamed"));
                equal("Child 🟢", original.Name);
                var renamed = f.scene.GetEntityName(entity);
                ok(renamed.returnValue);
                equal("Renamed", renamed.Name);

                try (IRadientScene retained = f.writer.GetScene()) {
                    check(retained != null);
                    f.close();
                    ok(retained.IsEntityAlive(entity));
                    equal("Scène 世界", retained.GetDesc().Name);
                    throwsType(IllegalStateException.class, f.scene::GetDesc);
                }
            }
        });

        run("Scene.HierarchyOutputArraysAndSubtreeDestruction", () -> {
            try (SceneFixture f = new SceneFixture()) {
                long root = f.entity(new RadientEntityDesc());
                long other = f.entity(new RadientEntityDesc());
                long first = f.entity(childOf(root));
                long second = f.entity(childOf(root));
                equal(2L, f.scene.GetRootEntityCount());
                var empty = f.scene.GetRootEntities(0);
                ok(empty.returnValue);
                equal(0L, empty.NumEntitiesWritten);
                equal(new long[0], empty.pEntities);
                var roots = f.scene.GetRootEntities(5);
                ok(roots.returnValue);
                equal(2L, roots.NumEntitiesWritten);
                equal(new long[]{root, other}, roots.pEntities);
                var limited = f.scene.GetRootEntities(1);
                ok(limited.returnValue);
                equal(1L, limited.NumEntitiesWritten);
                equal(new long[]{root}, limited.pEntities);
                var parent = f.scene.GetParent(second);
                ok(parent.returnValue);
                equal(root, parent.Parent);
                var count = f.scene.GetChildCount(root);
                ok(count.returnValue);
                equal(2L, count.ChildCount);
                var children = f.scene.GetChildren(root, 0, 5);
                ok(children.returnValue);
                equal(2L, children.NumChildrenWritten);
                equal(new long[]{first, second}, children.pChildren);
                var page = f.scene.GetChildren(root, 1, 1);
                ok(page.returnValue);
                equal(new long[]{second}, page.pChildren);
                ok(f.writer.DestroyEntity(root));
                for (long entity : new long[]{root, first, second})
                    equal(RADIENT_STATUS_NOT_FOUND, f.scene.IsEntityAlive(entity));
                ok(f.scene.IsEntityAlive(other));
                var removed = f.scene.GetEntityName(first);
                equal(RADIENT_STATUS_NOT_FOUND, removed.returnValue);
                equal(null, removed.Name);
                equal(new long[]{first, second}, children.pChildren);
                equal(new long[]{root, other}, roots.pEntities);
            }
        });

        run("Scene.TransformSnapshotsRevisionsAndReparenting", () -> {
            try (SceneFixture f = new SceneFixture()) {
                RadientEntityDesc desc = new RadientEntityDesc();
                desc.Transform.Position.x = 2;
                desc.Transform.Position.y = 3;
                desc.Transform.Position.z = 4;
                long parent = f.entity(desc);
                long child = f.entity(childOf(parent));
                var before = f.scene.GetSceneRevisions();
                RadientTransform transform = new RadientTransform();
                transform.Position.x = 5;
                transform.Position.y = 6;
                transform.Position.z = 7;
                transform.Scale.x = 2;
                transform.Scale.y = 3;
                transform.Scale.z = 4;
                ok(f.writer.SetLocalTransform(child, transform));
                transform.Position.x = 999;
                var local = f.scene.GetLocalTransform(child);
                ok(local.returnValue);
                equal(5.f, local.Transform.Position.x);
                check(f.scene.GetSceneRevisions().Transforms > before.Transforms);
                equal(RADIENT_STATUS_OUT_OF_DATE, f.scene.GetCachedWorldMatrix(child).returnValue);
                ok(f.writer.CommitChanges());
                float[] expected = {2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 7, 9, 11, 1};
                RadientMatrix4x4 snapshot = world(f.scene, child);
                equal(expected, snapshot.Data);
                snapshot.Data[12] = 999;
                equal(expected, world(f.scene, child).Data);
                var cached = f.scene.GetCachedWorldMatrix(child);
                ok(cached.returnValue);
                equal(expected, cached.Matrix.Data);
                ok(f.writer.SetParent(child, InvalidRadientEntityID, true));
                equal(expected, world(f.scene, child).Data);
                ok(f.writer.SetParent(child, parent, false));
                equal(9.f, world(f.scene, child).Data[12]);
            }
        });

        run("Scene.VisibilityStatusAndBooleanOutputs", () -> {
            try (SceneFixture f = new SceneFixture()) {
                long parent = f.entity(new RadientEntityDesc());
                long child = f.entity(childOf(parent));
                ok(f.writer.CommitChanges());
                ok(f.writer.SetEntityOwnVisibility(parent, false));
                equal(RADIENT_STATUS_OUT_OF_DATE, f.scene.GetCachedEntityEffectiveVisibility(child).returnValue);
                var own = f.scene.GetEntityOwnVisibility(child);
                ok(own.returnValue);
                equal(true, own.Visible);
                var effective = f.scene.GetEntityEffectiveVisibility(child);
                ok(effective.returnValue);
                equal(false, effective.Visible);
                ok(f.writer.SetEntityOwnVisibility(parent, true));
                ok(f.writer.CommitChanges());
                var visible = f.scene.GetCachedEntityEffectiveVisibility(child);
                ok(visible.returnValue);
                equal(true, visible.Visible);
            }
        });
    }
}
