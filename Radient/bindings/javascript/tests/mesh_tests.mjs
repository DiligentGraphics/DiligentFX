import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_INVALID_ARGUMENT: INVALID} = constants.RADIENT_STATUS;
    const {RADIENT_INDEX_TYPE_NONE: NONE, RADIENT_INDEX_TYPE_UINT16: UINT16} = constants.RADIENT_INDEX_TYPE;

    test("mesh blobs, nested vertex layout, asset status and reflection round-trip", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        assert.ok(Object.hasOwn(f.manager.GetDesc(), "Name"));
        const ci = f.triangle({IndexData: {pIndexBuffer: f.blob(new Uint16Array(f.data.numbers("mesh.indices"))), IndexCount: 3, IndexType: UINT16}});
        const mesh = f.asset(f.manager.CreateMesh(ci));
        ci.VertexData.ppVertexBuffers[0].dispose();
        ci.IndexData.pIndexBuffer.dispose();
        ci.pPrimitives[0].Name = "changed";
        await f.ready(mesh);
        assert.equal(mesh.GetType(), constants.RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_MESH);
        assert.ok(Object.hasOwn(mesh.GetReference(), "URI"));
        const desc = mesh.GetDesc();
        assert.equal(desc.GeometryCount, 1);
        assert.equal(desc.PrimitiveCount, 1);
        assert.equal(desc.pPrimitives[0].Name, f.data.string("mesh.primitiveName"));
        assert.equal(desc.pPrimitives[0].pMaterial, null);
        const geometry = desc.pGeometries[0];
        assert.equal(geometry.VertexCount, 3);
        assert.equal(geometry.IndexCount, 3);
        assert.equal(geometry.IndexType, UINT16);
        assert.equal(geometry.VertexLayout.pAttributes[0].Semantic, "POSITION");
        assert.ok(geometry.VertexLayout.pBuffers[0].ByteStride > 0);
        assert.equal(api.GetRadientVertexComponentSize(constants.RADIENT_VERTEX_COMPONENT_TYPE.RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32), 4);
        assert.equal(api.GetRadientVertexAttributeSize(ci.VertexData.VertexLayout.pAttributes[0]), 12);
        assert.equal(constants.RADIENT_VERTEX_AUTO_OFFSET, 0xffffffff);
        assert.equal(constants.RADIENT_VERTEX_AUTO_STRIDE, 0xffffffff);
        mesh.dispose();
        assert.equal(desc.pGeometries[0].VertexCount, 3); // Copied metadata survives the owner.
    });

    test("scene mesh getters retain assets and mesh-renderer components preserve 64-bit masks", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const mesh = await f.ready(f.asset(f.manager.CreateMesh(f.triangle())));
        assert.equal(mesh.GetDesc().pGeometries[0].IndexType, NONE);
        const entity = f.value(f.writer.CreateEntity({}));
        assert.equal(f.writer.SetMesh(entity, {pMesh: mesh}), OK);
        assert.equal(f.writer.SetMeshRenderer(entity, {VisibilityMask: BigInt("0x" + f.data.string("mesh.visibilityMask"))}), OK);
        mesh.dispose();
        const retained = f.keep(f.value(f.scene.GetMesh(entity)).pMesh);
        assert.equal(retained.GetDesc().pGeometries[0].VertexCount, 3);
        assert.equal(f.value(f.scene.GetMeshRenderer(entity)).VisibilityMask, BigInt("0x" + f.data.string("mesh.visibilityMask")));
        assert.equal(f.writer.RemoveComponent(entity, constants.RADIENT_COMPONENT_TYPE_MESH), OK);
        assert.equal(f.writer.RemoveComponent(entity, constants.RADIENT_COMPONENT_TYPE_MESH_RENDERER), OK);
        assert.equal(retained.GetDesc().PrimitiveCount, 1);
    });

    test("procedural mesh helpers marshal optional six-face colors and reject excessive subdivisions", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const channels = f.data.numbers("mesh.faceColors");
        const colors = Array.from({length: 6}, (_, i) => ({r: channels[i * 4], g: channels[i * 4 + 1], b: channels[i * 4 + 2], a: channels[i * 4 + 3]}));
        const cube = await f.ready(f.asset(api.CreateRadientCubeMesh(f.manager, {Name: "Cube", pFaceColors: colors})));
        assert.equal(cube.GetDesc().pGeometries[0].VertexCount, 24);
        assert.ok(cube.GetDesc().pGeometries[0].VertexLayout.pAttributes.some(a => a.Semantic === "COLOR_0"));
        const sphere = await f.ready(f.asset(api.CreateRadientSphereMesh(f.manager, {Radius: f.data.number("mesh.sphereRadius"), Subdivisions: f.data.number("mesh.sphereSubdivisions")})));
        assert.ok(sphere.GetDesc().pGeometries[0].IndexCount > 0);
        assert.throws(() => api.CreateRadientCubeMesh(f.manager, {pFaceColors: colors.slice(1)}), /count exceeds/);
        assert.equal(api.CreateRadientCubeMesh(f.manager, {Subdivisions: constants.RADIENT_MESH_PRIMITIVE_MAX_SUBDIVISIONS + 1}).status, INVALID);
    });
}
