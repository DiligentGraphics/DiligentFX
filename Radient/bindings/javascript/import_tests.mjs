import {createAssetFixture} from "./asset_test_helpers.mjs";
import {makeAnimatedTriangleGLB, sceneEntities} from "./import_test_helpers.mjs";

const encode = text => new TextEncoder().encode(text);
export const moduleOptions = {files: {
    "/assets/animated.glb": makeAnimatedTriangleGLB(),
    "/assets/obj/triangle.obj": encode(`mtllib triangle.mtl
o Triangle
v 0 0 0
v 1 0 0
v 0 1 0
usemtl Green
f 1 2 3
`),
    "/assets/obj/triangle.mtl": encode("newmtl Green\nKd 0.1 0.8 0.2\n"),
}};

export function registerTests({test, assert, api, constants}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_PENDING: PENDING, RADIENT_STATUS_UNSUPPORTED: UNSUPPORTED} = constants.RADIENT_STATUS;

    test("GLB loading exposes clips and instantiates hierarchy, meshes, and animation bindings", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        const asset = await f.ready(f.asset(f.manager.LoadScene({URI: "/assets/animated.glb"})));
        assert.equal(asset.GetType(), constants.RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_SCENE);
        const desc = asset.GetDesc();
        assert.equal(desc.AnimationClipCount, 1);
        desc.ppAnimationClips.forEach(f.keep);
        assert.equal(desc.ppAnimationClips[0].GetDesc().Name, "Move");
        const importer = f.keep(f.value(f.engine.CreateSceneImporter(f.writer)));
        const registry = f.keep(f.value(f.engine.CreateAnimationRegistry(f.scene)));
        const root = f.value(importer.InstantiateScene(asset, {
            Name: "Instance 世界", RootTransform: {Position: {x: 10}}, pAnimationRegistry: registry,
            SceneIndex: constants.InvalidRadientSceneIndex,
        }));
        assert.equal(importer.ProcessPendingImports(), OK);
        assert.equal(f.value(f.scene.GetEntityName(root)), "Instance 世界");
        const entities = sceneEntities(f, root);
        const node = entities.find(entity => f.value(f.scene.GetEntityName(entity)) === "Animated triangle 世界");
        assert.ok(node !== undefined);
        const meshEntity = entities.find(entity => f.value(f.scene.HasComponent(entity, constants.RADIENT_COMPONENT_TYPE_MESH)));
        assert.ok(meshEntity !== undefined);
        const mesh = f.keep(f.value(f.scene.GetMesh(meshEntity)).pMesh);
        await f.ready(mesh);
        const meshDesc = mesh.GetDesc();
        meshDesc.pPrimitives.forEach(primitive => f.keep(primitive.pMaterial));
        assert.equal(meshDesc.pGeometries[0].VertexCount, 3);
        assert.equal(meshDesc.pPrimitives[0].ElementCount, 3);
        const state = registry.GetState();
        assert.equal(state.EntryCount, 1);
        for (const entry of state.pEntries) {
            f.keep(entry.pClip);
            for (const binding of entry.ppBindings) {
                f.keep(binding);
                assert.equal(binding.Evaluate({Time: 0.5}), OK);
            }
        }
        assert.equal(f.value(f.scene.GetLocalTransform(node)).Position.x, 1);
        assert.equal(f.value(f.scene.GetWorldMatrix(node)).Data[12], 11);
        asset.dispose();
        assert.equal(desc.ppAnimationClips[0].GetDesc().Duration, 1);
    });

    test("OBJ ImportScene marshals both outputs and resolves a relative material library", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        const importer = f.keep(f.value(f.engine.CreateSceneImporter(f.writer)));
        const result = importer.ImportScene({URI: "/assets/obj/triangle.obj", ImporterId: "obj"}, {Name: "OBJ instance"});
        assert.ok(result.status === OK || result.status === PENDING);
        const {ppScene: asset, RootEntity: root} = result.value;
        f.keep(asset);
        assert.ok(asset !== null);
        assert.ok(root !== constants.InvalidRadientEntityID);
        await f.ready(asset);
        assert.equal(importer.ProcessPendingImports(), OK);
        assert.equal(importer.ProcessPendingImports(), OK);
        assert.equal(asset.GetDesc().AnimationClipCount, 0);
        const entity = sceneEntities(f, root).find(entity => f.value(f.scene.HasComponent(entity, constants.RADIENT_COMPONENT_TYPE_MESH)));
        assert.ok(entity !== undefined);
        const mesh = await f.ready(f.keep(f.value(f.scene.GetMesh(entity)).pMesh));
        const desc = mesh.GetDesc();
        desc.pPrimitives.forEach(primitive => f.keep(primitive.pMaterial));
        assert.equal(desc.pGeometries[0].VertexCount, 3);
        const material = desc.pPrimitives[0].pMaterial;
        assert.ok(material !== null);
        const definition = f.keep(material.GetDefinition());
        const handle = f.value(definition.FindParameter(constants.RadientStandardMaterialDiffuseFactorName));
        const value = new Float32Array(f.value(material.GetParameter(handle, 16)).buffer);
        assert.ok(Math.abs(value[1] - 0.8) < 1e-6);
    });

    test("unsupported importers return empty outputs without adding an entity", t => {
        const f = createAssetFixture(t, api, assert, constants);
        const importer = f.keep(f.value(f.engine.CreateSceneImporter(f.writer)));
        const result = importer.ImportScene({URI: "/assets/animated.glb", ImporterId: "missing"}, {});
        assert.equal(result.status, UNSUPPORTED);
        assert.equal(result.value.ppScene, null);
        assert.equal(result.value.RootEntity, constants.InvalidRadientEntityID);
        assert.equal(f.scene.GetRootEntityCount(), 0);
    });
}
