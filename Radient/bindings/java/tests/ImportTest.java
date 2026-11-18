// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class ImportTest {
    private static RadientSceneLoadInfo loadInfo(String file) {
        RadientSceneLoadInfo result = new RadientSceneLoadInfo();
        result.URI = Fixtures.assetPath(file).toString();
        return result;
    }

    private static IRadientSceneImporter importer(AssetFixture f) {
        var result = f.engine.CreateSceneImporter(f.writer);
        return f.value(result.ppImporter, result.returnValue);
    }

    public static void main(String[] args) {
        run("GLB loading exposes clips and instantiates hierarchy, meshes, and animation bindings", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var loaded = f.manager.LoadScene(loadInfo("animated.glb"));
                var asset = f.ready(f.asset(loaded.ppScene, loaded.returnValue));
                equal(RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_SCENE, asset.GetType());
                var desc = asset.GetDesc();
                for (var clip : desc.ppAnimationClips) f.keep(clip);
                equal(1L, desc.AnimationClipCount);
                equal(Fixtures.string("import.clipName"), desc.ppAnimationClips[0].GetDesc().Name);
                var importer = importer(f);
                var createdRegistry = f.engine.CreateAnimationRegistry(f.scene);
                var registry = f.value(createdRegistry.ppRegistry, createdRegistry.returnValue);
                RadientSceneInstantiateInfo info = new RadientSceneInstantiateInfo();
                info.Name = Fixtures.string("import.instanceName");
                info.RootTransform.Position.x = Fixtures.number("import.rootX");
                info.pAnimationRegistry = registry;
                info.SceneIndex = InvalidRadientSceneIndex;
                var instance = importer.InstantiateScene(asset, info);
                ok(instance.returnValue);
                long root = instance.RootEntity;
                ok(importer.ProcessPendingImports());
                var name = f.scene.GetEntityName(root);
                ok(name.returnValue);
                equal(Fixtures.string("import.instanceName"), name.Name);
                var entities = f.sceneEntities(root);
                long node = entities.stream().filter(entity -> {
                    var nodeName = f.scene.GetEntityName(entity);
                    ok(nodeName.returnValue);
                    return Fixtures.string("import.nodeName").equals(nodeName.Name);
                }).findFirst().orElseThrow(() -> new AssertionError("Missing animated node"));
                long meshEntity = entities.stream().filter(entity -> f.hasComponent(entity, RADIENT_COMPONENT_TYPE_MESH))
                    .findFirst().orElseThrow(() -> new AssertionError("Missing mesh entity"));
                var mesh = f.ready(f.mesh(meshEntity));
                var meshDesc = f.meshDesc(mesh);
                equal(3L, meshDesc.pGeometries[0].VertexCount);
                equal(3L, meshDesc.pPrimitives[0].ElementCount);
                var state = registry.GetState();
                // Snapshots own references: retain every entry before asserting its contents.
                for (var entry : state.pEntries) {
                    f.keep(entry.pClip);
                    for (var binding : entry.ppBindings) f.keep(binding);
                }
                equal(1L, state.EntryCount);
                RadientAnimationEvaluateInfo evaluate = new RadientAnimationEvaluateInfo();
                evaluate.Time = Fixtures.number("import.time");
                for (var entry : state.pEntries)
                    for (var binding : entry.ppBindings) ok(binding.Evaluate(evaluate));
                var local = f.scene.GetLocalTransform(node);
                var world = f.scene.GetWorldMatrix(node);
                ok(local.returnValue);
                ok(world.returnValue);
                equal(Fixtures.number("import.localX"), local.Transform.Position.x);
                equal(Fixtures.number("import.worldX"), world.Matrix.Data[12]);
                asset.close();
                equal(Fixtures.number("import.duration"), desc.ppAnimationClips[0].GetDesc().Duration);
            }
        });

        run("OBJ ImportScene marshals both outputs and resolves a relative material library", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var importer = importer(f);
                RadientSceneLoadInfo load = loadInfo("obj/triangle.obj");
                load.ImporterId = "obj";
                RadientSceneInstantiateInfo info = new RadientSceneInstantiateInfo();
                info.Name = Fixtures.string("import.objInstanceName");
                var result = importer.ImportScene(load, info);
                var asset = f.asset(result.ppScene, result.returnValue);
                long root = result.RootEntity;
                check(root != InvalidRadientEntityID);
                f.ready(asset);
                ok(importer.ProcessPendingImports());
                ok(importer.ProcessPendingImports());
                var sceneDesc = asset.GetDesc();
                if (sceneDesc.ppAnimationClips != null)
                    for (var clip : sceneDesc.ppAnimationClips) f.keep(clip);
                equal(0L, sceneDesc.AnimationClipCount);
                long entity = f.sceneEntities(root).stream().filter(e -> f.hasComponent(e, RADIENT_COMPONENT_TYPE_MESH))
                    .findFirst().orElseThrow(() -> new AssertionError("Missing OBJ mesh"));
                var mesh = f.ready(f.mesh(entity));
                var desc = f.meshDesc(mesh);
                equal(3L, desc.pGeometries[0].VertexCount);
                var material = desc.pPrimitives[0].pMaterial;
                check(material != null);
                var definition = f.keep(material.GetDefinition());
                check(definition != null);
                var handle = f.parameter(definition, RadientStandardMaterialDiffuseFactorName);
                float[] value = AssetFixture.floats(f.parameterBytes(material, handle, 16));
                check(Math.abs(value[1] - Fixtures.number("import.objGreen")) < 1e-6);
            }
        });

        run("unsupported importers return empty outputs without adding an entity", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var importer = importer(f);
                RadientSceneLoadInfo load = loadInfo("animated.glb");
                load.ImporterId = "missing";
                var result = importer.ImportScene(load, new RadientSceneInstantiateInfo());
                f.keep(result.ppScene);
                equal(RADIENT_STATUS_UNSUPPORTED, result.returnValue);
                equal(null, result.ppScene);
                equal(InvalidRadientEntityID, result.RootEntity);
                equal(0L, f.scene.GetRootEntityCount());
            }
        });
    }
}
