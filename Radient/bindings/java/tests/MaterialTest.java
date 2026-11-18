// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class MaterialTest {
    public static void main(String[] args) {
        run("material definitions expose parameter names, handles, types and copied defaults", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var definition = f.definition();
                ok(definition.GetStatus());
                var desc = definition.GetDesc();
                for (var item : desc.pParameters) f.keep(item.pDefaultTexture);
                equal(definition.GetParameterCount(), desc.ParameterCount);
                check(desc.ParameterCount > 0);
                String name = RadientStandardMaterialBaseColorFactorName;
                var handle = f.parameter(definition, name);
                var indexed = definition.GetParameterHandle(handle.Index);
                ok(indexed.returnValue);
                descriptorEqual(handle, indexed.pHandle);
                var parameter = definition.GetParameterDesc(handle.Index);
                f.keep(parameter.pDefaultTexture);
                equal(name, parameter.Name);
                equal(RADIENT_MATERIAL_PARAMETER_TYPE.RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, parameter.Type);
                equal(Fixtures.floats("material.defaultColor"), AssetFixture.floats(parameter.pDefaultValue));
                equal(null, parameter.pDefaultTexture);
                equal(RADIENT_STATUS_NOT_FOUND, definition.FindParameter("missing parameter").returnValue);
                equal(RADIENT_STATUS_INVALID_ARGUMENT, definition.GetParameterHandle(0xffffffffL).returnValue);
                for (long i = 0; i < definition.GetParameterCount(); ++i) {
                    var item = definition.GetParameterDesc(i);
                    f.keep(item.pDefaultTexture);
                    equal(i, f.parameter(definition, item.Name).Index);
                }
                definition.close();
                equal(Fixtures.floats("material.defaultColor"), AssetFixture.floats(parameter.pDefaultValue));
            }
        });

        run("material and scene bindings retain their referenced native assets", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var definition = f.definition();
                var material = f.material(definition);
                long version = material.GetVersion(); // Native Uint64 is represented by Java long.
                var borrowed = f.keep(material.GetDefinition());
                check(borrowed != null);
                definition.close();
                var textureHandle = f.parameter(borrowed, RadientStandardMaterialBaseColorTextureParameterNames().Texture);
                var texture = material.GetTexture(textureHandle, 0);
                f.keep(texture.ppTexture);
                ok(texture.returnValue);
                equal(null, texture.ppTexture);
                var surface = f.keep(material.QueryInterface(IRadientSurfaceMaterialAsset.class));
                check(surface != null);
                equal(RADIENT_MATERIAL_SURFACE_MODE.RADIENT_MATERIAL_SURFACE_MODE_OPAQUE, surface.GetSurfaceMode());
                equal(Fixtures.number("material.defaultAlphaCutoff"), surface.GetAlphaCutoff());
                equal(false, surface.IsDoubleSided());
                long entity = f.entity();
                RadientMaterialBinding binding = new RadientMaterialBinding();
                binding.PrimitiveIndex = Fixtures.integer("material.primitiveIndex");
                binding.pMaterial = material;
                RadientMaterialBindingsComponent bindings = new RadientMaterialBindingsComponent();
                bindings.pBindings = new RadientMaterialBinding[]{binding};
                bindings.BindingCount = 1;
                ok(f.writer.SetMaterialBindings(entity, bindings));
                var snapshot = f.scene.GetMaterialBindings(entity);
                for (var item : snapshot.Bindings.pBindings) f.keep(item.pMaterial);
                ok(snapshot.returnValue);
                var retained = snapshot.Bindings.pBindings[0].pMaterial;
                check(retained != null);
                equal(binding.PrimitiveIndex, snapshot.Bindings.pBindings[0].PrimitiveIndex);
                material.close();
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS));
                equal(RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_MATERIAL, retained.GetType());
                equal(version, retained.GetVersion());
                ok(borrowed.GetStatus());
            }
        });
    }
}
