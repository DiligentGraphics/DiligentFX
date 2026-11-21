import {createAssetFixture} from "./asset_test_helpers.mjs";

export const assetFiles = ["colors.tga"];

export function registerTests({test, assert, api, constants, testData, assets}) {
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_INVALID_ARGUMENT: INVALID, RADIENT_STATUS_INVALID_OPERATION: LOCKED} = constants.RADIENT_STATUS;
    const {RADIENT_TEXTURE_FORMAT_RGBA8_UNORM: RGBA, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB: SRGB} = constants.RADIENT_TEXTURE_FORMAT;

    test("texture mip descriptors and blobs survive disposal of their source wrappers", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const bytes = f.blob(new Uint8Array(f.data.number("texture.mipBytes")).fill(f.data.number("texture.mipFill")));
        const data = {Width: f.data.number("texture.width"), Height: f.data.number("texture.height"), Format: RGBA, GenerateMips: false,
            pMipLevels: [{pDataBlob: bytes, ByteOffset: BigInt(f.data.number("texture.firstOffset")), Stride: f.data.number("texture.firstStride")}, {pDataBlob: bytes, ByteOffset: BigInt(f.data.number("texture.secondOffset"))}], MipLevelCount: 2};
        const texture = f.asset(f.manager.LoadTexture({URI: f.data.string("texture.uri"), EncodedTextureIsSRGB: true, pTextureData: data}));
        bytes.dispose();
        data.Width = 999;
        data.pMipLevels.length = 0;
        await f.ready(texture);
        assert.equal(texture.GetType(), constants.RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_TEXTURE);
        assert.equal(texture.GetReference().URI, f.data.string("texture.uri"));
        assert.deepEqual(texture.GetDesc(), {Width: f.data.number("texture.width"), Height: f.data.number("texture.height"), Format: RGBA, MipLevels: 2});
        const snapshot = texture.GetDesc();
        texture.dispose();
        assert.equal(snapshot.Width, f.data.number("texture.width"));
    });

    test("raw texture loading generates the missing mip tail with native defaults", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const dimension = f.data.number("texture.generatedDimension");
        const texture = await f.ready(f.asset(f.manager.LoadTexture({pTextureData: {
            Width: dimension, Height: dimension, Format: SRGB, MipLevelCount: 1,
            pMipLevels: [{pDataBlob: f.blob(new Uint8Array(dimension * dimension * 4).fill(255))}],
        }})));
        assert.deepEqual(texture.GetDesc(), {Width: dimension, Height: dimension, Format: SRGB, MipLevels: f.data.number("texture.generatedMips")});
    });

    test("encoded image bytes pass through the production decoder", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const source = f.blob(assets["colors.tga"]);
        const texture = f.asset(f.manager.LoadTexture({pDataBlob: source, EncodedTextureIsSRGB: true}));
        source.dispose();
        await f.ready(texture);
        assert.deepEqual(texture.GetDesc(), {Width: 2, Height: 1, Format: SRGB, MipLevels: 2});
    });

    test("texture references preserve their base in independent snapshots", async t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        const texture = f.asset(f.manager.LoadTexture({URI: "image%2Etga", BaseURI: "opaque:document/model.gltf",
            pDataBlob: f.blob(assets["colors.tga"])}));
        const before = texture.GetReference();
        await f.ready(texture);
        const after = texture.GetReference();
        assert.deepEqual(before, after);
        texture.dispose();
        assert.equal(after.URI, "image%2Etga");
        assert.equal(after.BaseURI, "opaque:document/model.gltf");
    });

    test("texture inputs preserve validation failures and reject an active blob writer", t => {
        const f = createAssetFixture(t, api, assert, constants, testData);
        assert.equal(f.manager.LoadTexture({pTextureData: {Width: 1, Height: 1, Format: RGBA}}).status, INVALID);
        const blob = f.keep(f.value(api.CreateRadientMutableDataBlob({Size: 4n})));
        const scope = f.keep(f.value(blob.BeginWrite()));
        scope.copyFrom(f.data.bytes("texture.lockedPixel"));
        const result = f.manager.LoadTexture({pTextureData: {Width: 1, Height: 1, Format: RGBA,
            MipLevelCount: 1, pMipLevels: [{pDataBlob: blob}]}});
        f.keep(result.value);
        assert.equal(result.status, LOCKED);
        scope.dispose();
    });
}
