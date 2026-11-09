import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NOT_FOUND: NOT_FOUND, RADIENT_STATUS_INVALID_ARGUMENT: INVALID} = constants.RADIENT_STATUS;
    test("material definitions expose parameter names, handles, types and copied defaults", t => {
        const f = createAssetFixture(t, api, assert, constants);
        const definition = f.keep(f.value(f.manager.CreateStandardMaterialDefinition({})));
        assert.equal(definition.GetStatus(), OK);
        const desc = definition.GetDesc();
        assert.equal(desc.ParameterCount, definition.GetParameterCount());
        assert.ok(desc.ParameterCount > 0);
        const name = constants.RadientStandardMaterialBaseColorFactorName;
        const handle = f.value(definition.FindParameter(name));
        assert.deepEqual(f.value(definition.GetParameterHandle(handle.Index)), handle);
        const parameter = definition.GetParameterDesc(handle.Index);
        assert.equal(parameter.Name, name);
        assert.equal(parameter.Type, constants.RADIENT_MATERIAL_PARAMETER_TYPE.RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4);
        assert.deepEqual(Array.from(new Float32Array(parameter.pDefaultValue.buffer)), [1, 1, 1, 1]);
        assert.equal(parameter.pDefaultTexture, null);
        assert.equal(definition.FindParameter("missing parameter").status, NOT_FOUND);
        assert.equal(definition.GetParameterHandle(0xffffffff).status, INVALID);
        for (let i = 0; i < definition.GetParameterCount(); ++i) {
            const item = definition.GetParameterDesc(i);
            assert.equal(f.value(definition.FindParameter(item.Name)).Index, i);
            item.pDefaultTexture?.dispose();
        }
        for (const item of desc.pParameters) item.pDefaultTexture?.dispose();
        definition.dispose();
        assert.deepEqual(Array.from(new Float32Array(parameter.pDefaultValue.buffer)), [1, 1, 1, 1]);
    });

    test("material and scene bindings retain their referenced native assets", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        const definition = f.keep(f.value(f.manager.CreateStandardMaterialDefinition({})));
        const material = f.keep(f.value(f.manager.CreateMaterial(definition)));
        assert.equal(typeof material.GetVersion(), "bigint");
        const borrowed = f.keep(material.GetDefinition());
        definition.dispose();
        const textureHandle = f.value(borrowed.FindParameter(constants.RadientStandardMaterialBaseColorTextureParameterNames.Texture));
        assert.deepEqual(material.GetTexture(textureHandle, 0), {status: OK, value: null});
        const surface = f.keep(material.QueryInterface(api.IRadientSurfaceMaterialAsset));
        assert.equal(surface.GetSurfaceMode(), constants.RADIENT_MATERIAL_SURFACE_MODE.RADIENT_MATERIAL_SURFACE_MODE_OPAQUE);
        assert.equal(surface.GetAlphaCutoff(), 0.5);
        assert.equal(surface.IsDoubleSided(), false);
        const entity = f.value(f.writer.CreateEntity({}));
        assert.equal(f.writer.SetMaterialBindings(entity, {BindingCount: 1, pBindings: [{PrimitiveIndex: 2, pMaterial: material}]}), OK);
        const bindings = f.value(f.scene.GetMaterialBindings(entity));
        const retained = f.keep(bindings.pBindings[0].pMaterial);
        assert.equal(bindings.pBindings[0].PrimitiveIndex, 2);
        material.dispose();
        assert.equal(f.writer.RemoveComponent(entity, constants.RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS), OK);
        assert.equal(retained.GetType(), constants.RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_MATERIAL);
        assert.equal(borrowed.GetStatus(), OK);
    });
}
