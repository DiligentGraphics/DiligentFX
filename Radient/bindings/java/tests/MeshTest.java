// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class MeshTest {
    public static void main(String[] args) {
        run("mesh blobs, nested vertex layout, asset status and reflection round-trip", () -> {
            try (AssetFixture f = new AssetFixture()) {
                // Match JS's own-property checks without requiring optional names/URIs.
                check(Arrays.stream(f.manager.GetDesc().getClass().getFields()).anyMatch(field -> field.getName().equals("Name")));
                RadientMeshCreateInfo info = f.triangle();
                info.IndexData.pIndexBuffer = f.blob(AssetFixture.indexBytes(Fixtures.ints("mesh.indices")));
                info.IndexData.IndexCount = 3;
                info.IndexData.IndexType = RADIENT_INDEX_TYPE.RADIENT_INDEX_TYPE_UINT16;
                var created = f.manager.CreateMesh(info);
                var mesh = f.asset(created.ppMesh, created.returnValue);
                info.VertexData.ppVertexBuffers[0].close();
                info.IndexData.pIndexBuffer.close();
                info.pPrimitives[0].Name = "changed";
                f.ready(mesh);
                equal(RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_MESH, mesh.GetType());
                check(Arrays.stream(mesh.GetReference().getClass().getFields()).anyMatch(field -> field.getName().equals("URI")));
                var desc = f.meshDesc(mesh);
                equal(1L, desc.GeometryCount);
                equal(1L, desc.PrimitiveCount);
                equal(Fixtures.string("mesh.primitiveName"), desc.pPrimitives[0].Name);
                equal(null, desc.pPrimitives[0].pMaterial);
                var geometry = desc.pGeometries[0];
                equal(3L, geometry.VertexCount);
                equal(3L, geometry.IndexCount);
                equal(RADIENT_INDEX_TYPE.RADIENT_INDEX_TYPE_UINT16, geometry.IndexType);
                equal("POSITION", geometry.VertexLayout.pAttributes[0].Semantic);
                check(geometry.VertexLayout.pBuffers[0].ByteStride > 0);
                equal(4L, GetRadientVertexComponentSize(RADIENT_VERTEX_COMPONENT_TYPE.RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32));
                equal(12L, GetRadientVertexAttributeSize(info.VertexData.VertexLayout.pAttributes[0]));
                equal(0xffffffffL, RADIENT_VERTEX_AUTO_OFFSET);
                equal(0xffffffffL, RADIENT_VERTEX_AUTO_STRIDE);
                mesh.close();
                equal(3L, desc.pGeometries[0].VertexCount);
            }
        });

        run("scene mesh getters retain assets and mesh-renderer components preserve 64-bit masks", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var created = f.manager.CreateMesh(f.triangle());
                var mesh = f.ready(f.asset(created.ppMesh, created.returnValue));
                equal(RADIENT_INDEX_TYPE.RADIENT_INDEX_TYPE_NONE, f.meshDesc(mesh).pGeometries[0].IndexType);
                long entity = f.entity();
                RadientMeshComponent component = new RadientMeshComponent();
                component.pMesh = mesh;
                ok(f.writer.SetMesh(entity, component));
                RadientMeshRendererComponent renderer = new RadientMeshRendererComponent();
                renderer.VisibilityMask = Long.parseUnsignedLong(Fixtures.string("mesh.visibilityMask"), 16);
                ok(f.writer.SetMeshRenderer(entity, renderer));
                mesh.close();
                var retained = f.mesh(entity);
                equal(3L, f.meshDesc(retained).pGeometries[0].VertexCount);
                var snapshot = f.scene.GetMeshRenderer(entity);
                ok(snapshot.returnValue);
                equal(renderer.VisibilityMask, snapshot.Renderer.VisibilityMask);
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_MESH));
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER));
                equal(1L, f.meshDesc(retained).PrimitiveCount);
            }
        });

        run("procedural mesh helpers marshal optional six-face colors and reject excessive subdivisions", () -> {
            try (AssetFixture f = new AssetFixture()) {
                int[] channels = Fixtures.ints("mesh.faceColors");
                RadientColorRGBA8[] colors = new RadientColorRGBA8[6];
                equal(24, channels.length);
                for (int i = 0; i < colors.length; ++i) {
                    colors[i] = new RadientColorRGBA8();
                    colors[i].r = (short)channels[i * 4];
                    colors[i].g = (short)channels[i * 4 + 1];
                    colors[i].b = (short)channels[i * 4 + 2];
                    colors[i].a = (short)channels[i * 4 + 3];
                }
                RadientCubeMeshCreateInfo info = new RadientCubeMeshCreateInfo();
                info.Name = "Cube";
                info.pFaceColors = colors;
                var created = CreateRadientCubeMesh(f.manager, info);
                var cube = f.ready(f.asset(created.ppMesh, created.returnValue));
                var geometry = f.meshDesc(cube).pGeometries[0];
                equal(24L, geometry.VertexCount);
                check(Arrays.stream(geometry.VertexLayout.pAttributes).anyMatch(a -> "COLOR_0".equals(a.Semantic)));
                RadientSphereMeshCreateInfo sphereInfo = new RadientSphereMeshCreateInfo();
                sphereInfo.Radius = Fixtures.number("mesh.sphereRadius");
                sphereInfo.Subdivisions = Fixtures.integer("mesh.sphereSubdivisions");
                var createdSphere = CreateRadientSphereMesh(f.manager, sphereInfo);
                var sphere = f.ready(f.asset(createdSphere.ppMesh, createdSphere.returnValue));
                check(f.meshDesc(sphere).pGeometries[0].IndexCount > 0);
                info.pFaceColors = Arrays.copyOfRange(colors, 1, colors.length);
                throwsType(IllegalArgumentException.class, () -> CreateRadientCubeMesh(f.manager, info));
                info.pFaceColors = null;
                info.Subdivisions = RADIENT_MESH_PRIMITIVE_MAX_SUBDIVISIONS + 1;
                var rejected = CreateRadientCubeMesh(f.manager, info);
                f.keep(rejected.ppMesh);
                equal(RADIENT_STATUS_INVALID_ARGUMENT, rejected.returnValue);
            }
        });
    }
}
