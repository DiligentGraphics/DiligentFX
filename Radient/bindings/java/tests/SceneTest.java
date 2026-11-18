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
        run("engine resource configuration accepts partial descriptors and validates integer types", () -> {
            try (Resources r = new Resources()) {
                for (boolean configured : new boolean[]{false, true}) {
                    RadientEngineCreateInfo info = new RadientEngineCreateInfo();
                    info.WorkerThreadCount = 1;
                    if (configured) {
                        info.Resources.VertexPoolSize = Fixtures.integer("resources.VertexPoolSize");
                        info.Resources.MaxIndexBufferSize = Fixtures.integer("resources.MaxIndexBufferSize");
                    }
                    var result = CreateRadientEngine(info);
                    r.keep(result.ppEngine);
                    ok(result.returnValue);
                    check(result.ppEngine != null);
                    result.ppEngine.close();
                }
                // Java rejects wrong scalar types at compile time. Uint32 uses
                // long, so its negative range check remains a runtime case.
                RadientEngineCreateInfo invalid = new RadientEngineCreateInfo();
                invalid.Resources.VertexPoolSize = -1;
                throwsType(IllegalArgumentException.class, () -> CreateRadientEngine(invalid));
            }
        });

        run("scene and entity names are copied, with native defaults and 64-bit IDs", () -> {
            try (SceneFixture f = new SceneFixture(Fixtures.string("scene.name"))) {
                equal(Fixtures.string("scene.name"), f.scene.GetDesc().Name);
                RadientEntityDesc desc = new RadientEntityDesc();
                equal(InvalidRadientEntityID, desc.Parent);
                equal(1.f, desc.Transform.Scale.x);
                equal(1.f, desc.Transform.Rotation.w);
                desc.Name = Fixtures.string("scene.entityName");
                long entity = f.entity(desc); // Java long preserves the native Uint64 bits.
                desc.Name = "changed client descriptor";
                ok(f.scene.IsEntityAlive(entity));
                var parent = f.scene.GetParent(entity);
                ok(parent.returnValue);
                equal(InvalidRadientEntityID, parent.Parent);
                var flags = f.scene.GetEntityFlags(entity);
                ok(flags.returnValue);
                equal(RADIENT_ENTITY_FLAGS.RADIENT_ENTITY_FLAG_VISIBLE, flags.Flags);
                var visible = f.scene.GetEntityOwnVisibility(entity);
                ok(visible.returnValue);
                equal(true, visible.Visible);
                var local = f.scene.GetLocalTransform(entity);
                ok(local.returnValue);
                RadientTransform identity = new RadientTransform();
                equal(0.f, identity.Position.x); equal(0.f, identity.Position.y); equal(0.f, identity.Position.z);
                equal(0.f, identity.Rotation.x); equal(0.f, identity.Rotation.y); equal(0.f, identity.Rotation.z); equal(1.f, identity.Rotation.w);
                equal(1.f, identity.Scale.x); equal(1.f, identity.Scale.y); equal(1.f, identity.Scale.z);
                descriptorEqual(identity, local.Transform);
                var original = f.scene.GetEntityName(entity);
                ok(original.returnValue);
                equal(Fixtures.string("scene.entityName"), original.Name);
                ok(f.writer.SetEntityName(entity, Fixtures.string("scene.renamed")));
                var renamed = f.scene.GetEntityName(entity);
                ok(renamed.returnValue);
                equal(Fixtures.string("scene.renamed"), renamed.Name);
                equal(Fixtures.string("scene.entityName"), original.Name);
                equal(RADIENT_STATUS_NO_CHANGE, f.writer.SetEntityName(entity, Fixtures.string("scene.renamed")));
                ok(f.writer.SetEntityName(entity, null));
                var cleared = f.scene.GetEntityName(entity);
                ok(cleared.returnValue);
                equal("", cleared.Name);
            }
        });

        run("hierarchy enumeration copies written IDs and subtree destruction invalidates handles", () -> {
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
                equal(1L, page.NumChildrenWritten);
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
                var remaining = f.scene.GetRootEntities(5);
                ok(remaining.returnValue);
                equal(new long[]{other}, remaining.pEntities);
            }
        });

        run("nested transforms, matrix snapshots, revisions, and default reparenting cross the binding", () -> {
            try (SceneFixture f = new SceneFixture()) {
                RadientEntityDesc desc = new RadientEntityDesc();
                desc.Transform.Position = Fixtures.vector3("scene.parentPosition");
                long parent = f.entity(desc);
                long child = f.entity(childOf(parent));
                var before = f.scene.GetSceneRevisions();
                RadientTransform transform = new RadientTransform();
                transform.Position = Fixtures.vector3("scene.childPosition");
                transform.Scale = Fixtures.vector3("scene.childScale");
                ok(f.writer.SetLocalTransform(child, transform));
                transform.Position.x = 999;
                var local = f.scene.GetLocalTransform(child);
                ok(local.returnValue);
                equal(Fixtures.vector3("scene.childPosition").x, local.Transform.Position.x);
                check(f.scene.GetSceneRevisions().Transforms > before.Transforms);
                equal(RADIENT_STATUS_OUT_OF_DATE, f.scene.GetCachedWorldMatrix(child).returnValue);
                ok(f.writer.CommitChanges());
                float[] expected = Fixtures.floats("scene.worldMatrix");
                RadientMatrix4x4 snapshot = world(f.scene, child);
                equal(expected, snapshot.Data);
                snapshot.Data[12] = 999;
                equal(expected, world(f.scene, child).Data);
                var cached = f.scene.GetCachedWorldMatrix(child);
                ok(cached.returnValue);
                equal(expected, cached.Matrix.Data);
                // Java requires the explicit equivalent of JS's native default.
                ok(f.writer.SetParent(child, InvalidRadientEntityID, true));
                var parentResult = f.scene.GetParent(child);
                ok(parentResult.returnValue);
                equal(InvalidRadientEntityID, parentResult.Parent);
                equal(expected, world(f.scene, child).Data);
                ok(f.writer.SetParent(child, parent, false));
                equal(Fixtures.number("scene.reparentedWorldX"), world(f.scene, child).Data[12]);
            }
        });

        run("visibility edits and cached queries preserve native status and boolean semantics", () -> {
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
                ok(f.writer.SetEntityOwnVisibility(parent, false));
                ok(f.writer.SetEntityFlags(parent, RADIENT_ENTITY_FLAGS.RADIENT_ENTITY_FLAG_VISIBLE));
                ok(f.writer.SetEntityFlags(child, RADIENT_ENTITY_FLAGS.RADIENT_ENTITY_FLAG_NONE));
                ok(f.writer.CommitChanges());
                var cachedParent = f.scene.GetCachedEntityEffectiveVisibility(parent);
                var cachedChild = f.scene.GetCachedEntityEffectiveVisibility(child);
                ok(cachedParent.returnValue);
                ok(cachedChild.returnValue);
                equal(true, cachedParent.Visible);
                equal(false, cachedChild.Visible);
                var flags = f.scene.GetEntityFlags(child);
                ok(flags.returnValue);
                equal(RADIENT_ENTITY_FLAGS.RADIENT_ENTITY_FLAG_NONE, flags.Flags);
            }
        });

        run("borrowed scene wrappers retain the native scene after other owners are disposed", () -> {
            try (SceneFixture f = new SceneFixture(Fixtures.string("scene.retainedName"))) {
                RadientEntityDesc desc = new RadientEntityDesc();
                desc.Name = Fixtures.string("scene.retainedEntityName");
                long entity = f.entity(desc);
                try (IRadientScene retained = f.writer.GetScene()) {
                    check(retained != null);
                    f.close();
                    ok(retained.IsEntityAlive(entity));
                    equal(Fixtures.string("scene.retainedName"), retained.GetDesc().Name);
                    var name = retained.GetEntityName(entity);
                    ok(name.returnValue);
                    equal(Fixtures.string("scene.retainedEntityName"), name.Name);
                    throwsType(IllegalStateException.class, f.scene::GetDesc);
                    retained.close();
                    throwsType(IllegalStateException.class, retained::GetDesc);
                }
            }
        });
    }
}
