import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE, RADIENT_STATUS_INVALID_ARGUMENT: INVALID,
        RADIENT_STATUS_INVALID_OPERATION: LOCKED} = constants.RADIENT_STATUS;
    const bytes = values => new Uint8Array(values.buffer, values.byteOffset, values.byteLength);
    function materialFixture(t) {
        const f = createAssetFixture(t, api, assert, constants);
        const definition = f.keep(f.value(f.manager.CreateStandardMaterialDefinition({})));
        const material = f.keep(f.value(f.manager.CreateMaterial(definition)));
        const writer = f.keep(f.value(material.CreateWriter()));
        return {...f, definition, material, materialWriter: writer};
    }
    test("material writers copy value bytes and publish parameter and surface changes", t => {
        const f = materialFixture(t);
        const handle = f.value(f.definition.FindParameter(constants.RadientStandardMaterialBaseColorFactorName));
        const original = f.value(f.material.GetParameter(handle, 16));
        assert.deepEqual(Array.from(new Float32Array(original.buffer)), [1, 1, 1, 1]);
        const value = new Float32Array([0.25, 0.5, 0.75, 1]);
        const version = f.material.GetVersion();
        assert.equal(f.materialWriter.SetParameter(handle, bytes(value), 16), OK);
        value.fill(0);
        assert.equal(f.materialWriter.Commit(), OK);
        assert.ok(f.material.GetVersion() > version);
        assert.deepEqual(Array.from(new Float32Array(f.value(f.material.GetParameter(handle, 16)).buffer)), [0.25, 0.5, 0.75, 1]);
        assert.deepEqual(Array.from(new Float32Array(original.buffer)), [1, 1, 1, 1]);
        assert.equal(f.materialWriter.Commit(), NO_CHANGE);
        assert.equal(f.material.GetParameter(handle, 4).status, INVALID);
        assert.equal(f.materialWriter.SetParameter(handle, new Uint8Array(4), 4), INVALID);
        const writer = f.keep(f.materialWriter.QueryInterface(api.IRadientSurfaceMaterialWriter));
        const surface = f.keep(f.material.QueryInterface(api.IRadientSurfaceMaterialAsset));
        const mode = constants.RADIENT_MATERIAL_SURFACE_MODE.RADIENT_MATERIAL_SURFACE_MODE_MASKED;
        assert.equal(writer.SetSurfaceMode(mode), OK);
        assert.equal(writer.SetAlphaCutoff(0.25), OK);
        assert.equal(writer.SetDoubleSided(true), OK);
        assert.equal(writer.Commit(), OK);
        assert.equal(surface.GetSurfaceMode(), mode);
        assert.equal(surface.GetAlphaCutoff(), 0.25);
        assert.equal(surface.IsDoubleSided(), true);
    });

    test("texture parameter initialization retains its asset and honors the native edit restriction", async t => {
        const f = materialFixture(t);
        const texture = await f.ready(f.asset(f.manager.LoadTexture({pTextureData: {
            Width: 1, Height: 1, Format: constants.RADIENT_TEXTURE_FORMAT.RADIENT_TEXTURE_FORMAT_RGBA8_UNORM,
            pMipLevels: [{pDataBlob: f.blob(new Uint8Array([255, 127, 0, 255]))}], MipLevelCount: 1,
        }})));
        const names = constants.RadientStandardMaterialBaseColorTextureParameterNames;
        const handle = f.value(f.definition.FindParameter(names.Texture));
        assert.equal(f.materialWriter.SetTexture(handle, 0, texture), OK);
        assert.equal(api.SetStandardMaterialTextureParameters(f.definition, f.materialWriter, names,
            {pTexture: texture, UVSelector: 0, UVScale: {x: 2, y: 3}, UVRotation: 0.5, UVBias: {x: 0.25, y: 0.5}}), OK);
        assert.equal(f.materialWriter.Commit(), OK);
        texture.dispose();
        const retained = f.keep(f.value(f.material.GetTexture(handle, 0)));
        assert.equal(retained.GetDesc().Width, 1);
        const scaleHandle = f.value(f.definition.FindParameter(names.UVScale));
        assert.deepEqual(Array.from(new Float32Array(f.value(f.material.GetParameter(scaleHandle, 8)).buffer)), [2, 3]);
        await f.ready(f.material);
        assert.equal(f.materialWriter.SetTexture(handle, 0, null), OK);
        assert.equal(f.materialWriter.Commit(), LOCKED);
        f.keep(f.value(f.material.GetTexture(handle, 0)));
    });
}
