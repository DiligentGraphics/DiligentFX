// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class MaterialEditTest {
    private static IRadientMaterialWriter writer(AssetFixture f, IRadientMaterialAsset material) {
        var result = material.CreateWriter();
        return f.value(result.ppWriter, result.returnValue);
    }

    public static void main(String[] args) {
        run("material writers copy value bytes and publish parameter and surface changes", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var definition = f.definition();
                var material = f.material(definition);
                var materialWriter = writer(f, material);
                var handle = f.parameter(definition, RadientStandardMaterialBaseColorFactorName);
                byte[] original = f.parameterBytes(material, handle, 16);
                equal(Fixtures.floats("material.defaultColor"), AssetFixture.floats(original));
                byte[] value = AssetFixture.floatBytes(Fixtures.floats("material.updatedColor"));
                long version = material.GetVersion();
                ok(materialWriter.SetParameter(handle, value, 16));
                Arrays.fill(value, (byte)0);
                ok(materialWriter.Commit());
                check(material.GetVersion() > version);
                equal(Fixtures.floats("material.updatedColor"), AssetFixture.floats(f.parameterBytes(material, handle, 16)));
                equal(Fixtures.floats("material.defaultColor"), AssetFixture.floats(original));
                equal(RADIENT_STATUS_NO_CHANGE, materialWriter.Commit());
                equal(RADIENT_STATUS_INVALID_ARGUMENT, material.GetParameter(handle, 4).returnValue);
                equal(RADIENT_STATUS_INVALID_ARGUMENT, materialWriter.SetParameter(handle, new byte[4], 4));
                var surfaceWriter = f.keep(materialWriter.QueryInterface(IRadientSurfaceMaterialWriter.class));
                var surface = f.keep(material.QueryInterface(IRadientSurfaceMaterialAsset.class));
                check(surfaceWriter != null && surface != null);
                long mode = RADIENT_MATERIAL_SURFACE_MODE.RADIENT_MATERIAL_SURFACE_MODE_MASKED;
                ok(surfaceWriter.SetSurfaceMode(mode));
                ok(surfaceWriter.SetAlphaCutoff(Fixtures.number("material.updatedAlphaCutoff")));
                ok(surfaceWriter.SetDoubleSided(true));
                ok(surfaceWriter.Commit());
                equal(mode, surface.GetSurfaceMode());
                equal(Fixtures.number("material.updatedAlphaCutoff"), surface.GetAlphaCutoff());
                equal(true, surface.IsDoubleSided());
            }
        });

        run("texture parameter initialization retains its asset and honors the native edit restriction", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var definition = f.definition();
                var material = f.material(definition);
                var materialWriter = writer(f, material);
                RadientTextureLoadInfo info = new RadientTextureLoadInfo();
                info.pTextureData = f.textureData(1, 1,
                    RADIENT_TEXTURE_FORMAT.RADIENT_TEXTURE_FORMAT_RGBA8_UNORM, Fixtures.bytes("material.texturePixel"));
                var created = f.manager.LoadTexture(info);
                var texture = f.ready(f.asset(created.ppTexture, created.returnValue));
                var names = RadientStandardMaterialBaseColorTextureParameterNames();
                var handle = f.parameter(definition, names.Texture);
                ok(materialWriter.SetTexture(handle, 0, texture));
                RadientStandardMaterialTextureParameters parameters = new RadientStandardMaterialTextureParameters();
                parameters.pTexture = texture;
                parameters.UVSelector = 0;
                parameters.UVScale = Fixtures.vector2("material.uvScale");
                parameters.UVRotation = Fixtures.number("material.uvRotation");
                parameters.UVBias = Fixtures.vector2("material.uvBias");
                ok(SetStandardMaterialTextureParameters(definition, materialWriter, names, parameters));
                ok(materialWriter.Commit());
                texture.close();
                var selected = material.GetTexture(handle, 0);
                var retained = f.value(selected.ppTexture, selected.returnValue);
                equal(1L, retained.GetDesc().Width);
                var scaleHandle = f.parameter(definition, names.UVScale);
                equal(Fixtures.floats("material.uvScale"), AssetFixture.floats(f.parameterBytes(material, scaleHandle, 8)));
                f.ready(material);
                ok(materialWriter.SetTexture(handle, 0, null));
                equal(RADIENT_STATUS_INVALID_OPERATION, materialWriter.Commit());
                var unchanged = material.GetTexture(handle, 0);
                f.value(unchanged.ppTexture, unchanged.returnValue);
            }
        });
    }
}
