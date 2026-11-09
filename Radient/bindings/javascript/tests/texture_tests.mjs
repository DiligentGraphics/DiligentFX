import {createAssetFixture} from "./asset_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_INVALID_ARGUMENT: INVALID, RADIENT_STATUS_INVALID_OPERATION: LOCKED} = constants.RADIENT_STATUS;
    const {RADIENT_TEXTURE_FORMAT_RGBA8_UNORM: RGBA, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB: SRGB} = constants.RADIENT_TEXTURE_FORMAT;

    test("texture mip descriptors and blobs survive disposal of their source wrappers", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        const bytes = f.blob(new Uint8Array(48).fill(127));
        const data = {Width: 4, Height: 2, Format: RGBA, GenerateMips: false,
            pMipLevels: [{pDataBlob: bytes, ByteOffset: 4n, Stride: 20}, {pDataBlob: bytes, ByteOffset: 40n}], MipLevelCount: 2};
        const texture = f.asset(f.manager.LoadTexture({URI: "memory:mips", EncodedTextureIsSRGB: true, pTextureData: data}));
        bytes.dispose();
        data.Width = 999;
        data.pMipLevels.length = 0;
        await f.ready(texture);
        assert.equal(texture.GetType(), constants.RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_TEXTURE);
        assert.equal(texture.GetReference().URI, "memory:mips");
        assert.deepEqual(texture.GetDesc(), {Width: 4, Height: 2, Format: RGBA, MipLevels: 2});
        const snapshot = texture.GetDesc();
        texture.dispose();
        assert.equal(snapshot.Width, 4);
    });

    test("raw texture loading generates the missing mip tail with native defaults", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        const texture = await f.ready(f.asset(f.manager.LoadTexture({pTextureData: {
            Width: 4, Height: 4, Format: SRGB, MipLevelCount: 1,
            pMipLevels: [{pDataBlob: f.blob(new Uint8Array(64).fill(255))}],
        }})));
        assert.deepEqual(texture.GetDesc(), {Width: 4, Height: 4, Format: SRGB, MipLevels: 3});
    });

    test("encoded image bytes pass through the production decoder", async t => {
        const f = createAssetFixture(t, api, assert, constants);
        // Write an uncompressed 24-bit TGA: two BGR pixels, top-left origin.
        const tga = new Uint8Array(18 + 6);
        tga[2] = 2;
        tga[12] = 2;
        tga[14] = 1;
        tga[16] = 24;
        tga[17] = 0x20;
        tga.set([0, 0, 255, 0, 255, 0], 18);
        const source = f.blob(tga);
        const texture = f.asset(f.manager.LoadTexture({pDataBlob: source, EncodedTextureIsSRGB: true}));
        source.dispose();
        await f.ready(texture);
        assert.deepEqual(texture.GetDesc(), {Width: 2, Height: 1, Format: SRGB, MipLevels: 2});
    });

    test("texture inputs preserve validation failures and reject an active blob writer", t => {
        const f = createAssetFixture(t, api, assert, constants);
        assert.equal(f.manager.LoadTexture({pTextureData: {Width: 1, Height: 1, Format: RGBA}}).status, INVALID);
        const blob = f.keep(f.value(api.CreateRadientMutableDataBlob({Size: 4n})));
        const scope = f.keep(f.value(blob.BeginWrite()));
        scope.copyFrom(new Uint8Array([255, 0, 0, 255]));
        const result = f.manager.LoadTexture({pTextureData: {Width: 1, Height: 1, Format: RGBA,
            MipLevelCount: 1, pMipLevels: [{pDataBlob: blob}]}});
        f.keep(result.value);
        assert.equal(result.status, LOCKED);
        scope.dispose();
    });
}
