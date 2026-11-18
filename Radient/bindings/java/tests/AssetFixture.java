// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

// Counterpart of asset_test_helpers.mjs: real CPU assets on an internal worker.
// Keep submitted blobs and returned snapshot interfaces until explicitly closed
// by a lifetime test or until fixture cleanup, including on assertion failures.
final class AssetFixture implements AutoCloseable {
    private final Resources resources = new Resources();
    private final SceneFixture sceneFixture = new SceneFixture();
    final IRadientEngine engine = sceneFixture.engine;
    final IRadientScene scene = sceneFixture.scene;
    final IRadientSceneWriter writer = sceneFixture.writer;
    final IRadientAssetManager manager;

    AssetFixture() {
        try {
            var result = engine.GetAssetManager();
            manager = value(result.ppAssetManager, result.returnValue);
        } catch (RuntimeException | Error error) {
            close();
            throw error;
        }
    }

    <T extends AutoCloseable> T keep(T object) { return resources.keep(object); }

    <T extends AutoCloseable> T value(T object, int status) {
        keep(object);
        ok(status);
        check(object != null);
        return object;
    }

    <T extends IRadientAsset> T asset(T object, int status) {
        keep(object);
        if (status != RADIENT_STATUS_OK && status != RADIENT_STATUS_PENDING)
            throw new AssertionError("Asset creation status: " + status);
        check(object != null);
        return object;
    }

    <T extends IRadientAsset> T ready(T object) {
        long deadline = System.nanoTime() + 15_000_000_000L;
        int status;
        while ((status = GetRadientAssetLoadStatus(object)) == RADIENT_STATUS_PENDING && System.nanoTime() < deadline) {
            try { Thread.sleep(1); }
            catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new AssertionError("Interrupted while loading an asset", error);
            }
        }
        ok(status);
        return object;
    }

    IRadientDataBlob blob(byte[] bytes) {
        RadientDataBlobCreateInfo info = new RadientDataBlobCreateInfo();
        info.Size = bytes.length;
        info.pData = bytes;
        var result = CreateRadientDataBlob(info);
        return value(result.ppBlob, result.returnValue);
    }

    static byte[] floatBytes(float[] values) {
        ByteBuffer buffer = ByteBuffer.allocate(values.length * Float.BYTES).order(ByteOrder.nativeOrder());
        buffer.asFloatBuffer().put(values);
        return buffer.array();
    }

    static float[] floats(byte[] bytes) {
        equal(0, bytes.length % Float.BYTES);
        float[] result = new float[bytes.length / Float.BYTES];
        ByteBuffer.wrap(bytes).order(ByteOrder.nativeOrder()).asFloatBuffer().get(result);
        return result;
    }

    static byte[] indexBytes(int[] indices) {
        ByteBuffer buffer = ByteBuffer.allocate(indices.length * Short.BYTES).order(ByteOrder.nativeOrder());
        for (int index : indices) buffer.putShort((short)index);
        return buffer.array();
    }

    RadientMeshCreateInfo triangle() {
        RadientMeshCreateInfo info = new RadientMeshCreateInfo();
        info.Name = Fixtures.string("mesh.name");
        info.VertexData.VertexCount = 3;
        info.VertexData.ppVertexBuffers = new IRadientDataBlob[]{blob(floatBytes(Fixtures.floats("mesh.positions")))};
        RadientVertexAttributeDesc position = new RadientVertexAttributeDesc();
        position.Semantic = "POSITION";
        position.ComponentType = RADIENT_VERTEX_COMPONENT_TYPE.RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32;
        position.ComponentCount = 3;
        info.VertexData.VertexLayout.pAttributes = new RadientVertexAttributeDesc[]{position};
        info.VertexData.VertexLayout.AttributeCount = 1;
        info.VertexData.VertexLayout.pBuffers = new RadientVertexBufferLayoutDesc[]{new RadientVertexBufferLayoutDesc()};
        info.VertexData.VertexLayout.BufferCount = 1;
        RadientMeshPrimitiveCreateInfo primitive = new RadientMeshPrimitiveCreateInfo();
        primitive.Name = Fixtures.string("mesh.primitiveName");
        primitive.ElementCount = 3;
        info.pPrimitives = new RadientMeshPrimitiveCreateInfo[]{primitive};
        info.PrimitiveCount = 1;
        return info;
    }

    RadientMorphTargetCreateInfo morphTarget(IRadientDataBlob deltas, long offset) {
        RadientMorphTargetCreateInfo target = new RadientMorphTargetCreateInfo();
        RadientMorphTargetAttributeDesc attribute = new RadientMorphTargetAttributeDesc();
        attribute.Semantic = RadientMorphTargetPositionSemantic;
        attribute.ComponentCount = 3;
        target.Desc.pAttributes = new RadientMorphTargetAttributeDesc[]{attribute};
        target.Desc.AttributeCount = 1;
        RadientMorphTargetAttributeCreateInfo data = new RadientMorphTargetAttributeCreateInfo();
        data.pDeltaBuffer = deltas;
        data.ByteOffset = offset;
        target.pAttributeData = new RadientMorphTargetAttributeCreateInfo[]{data};
        return target;
    }

    RadientTextureData textureData(long width, long height, short format, byte[] bytes) {
        RadientTextureData data = new RadientTextureData();
        data.Width = width;
        data.Height = height;
        data.Format = format;
        RadientTextureMipData mip = new RadientTextureMipData();
        mip.pDataBlob = blob(bytes);
        data.pMipLevels = new RadientTextureMipData[]{mip};
        data.MipLevelCount = 1;
        return data;
    }

    IRadientMaterialDefinitionAsset definition() {
        var result = manager.CreateStandardMaterialDefinition(new RadientStandardMaterialDefinitionCreateInfo());
        return value(result.ppDefinition, result.returnValue);
    }

    IRadientMaterialAsset material(IRadientMaterialDefinitionAsset definition) {
        var result = manager.CreateMaterial(definition);
        return value(result.ppMaterial, result.returnValue);
    }

    RadientMeshAssetDesc meshDesc(IRadientMeshAsset mesh) {
        RadientMeshAssetDesc desc = mesh.GetDesc();
        for (var primitive : desc.pPrimitives) keep(primitive.pMaterial);
        return desc;
    }

    RadientMaterialParameterHandle parameter(IRadientMaterialDefinitionAsset definition, String name) {
        var result = definition.FindParameter(name);
        ok(result.returnValue);
        return result.pHandle;
    }

    byte[] parameterBytes(IRadientMaterialAsset material, RadientMaterialParameterHandle handle, long size) {
        var result = material.GetParameter(handle, size);
        ok(result.returnValue);
        return result.pData;
    }

    long entity() { return sceneFixture.entity(new RadientEntityDesc()); }

    List<Long> sceneEntities(long root) {
        List<Long> result = new ArrayList<>();
        result.add(root);
        for (int i = 0; i < result.size(); ++i) {
            var count = scene.GetChildCount(result.get(i));
            ok(count.returnValue);
            var children = scene.GetChildren(result.get(i), 0, count.ChildCount);
            ok(children.returnValue);
            for (long child : children.pChildren) result.add(child);
        }
        return result;
    }

    boolean hasComponent(long entity, long type) {
        var result = scene.HasComponent(entity, type);
        ok(result.returnValue);
        return result.HasComponent;
    }

    IRadientMeshAsset mesh(long entity) {
        var result = scene.GetMesh(entity);
        return value(result.Mesh.pMesh, result.returnValue);
    }

    @Override public void close() {
        try { resources.close(); }
        finally { sceneFixture.close(); }
    }
}
