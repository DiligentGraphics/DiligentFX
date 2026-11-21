// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class TextureTest {
    private static void desc(IRadientTextureAsset texture, long width, long height, short format, long mips) {
        RadientTextureAssetDesc expected = new RadientTextureAssetDesc();
        expected.Width = width;
        expected.Height = height;
        expected.Format = format;
        expected.MipLevels = mips;
        descriptorEqual(expected, texture.GetDesc());
    }

    public static void main(String[] args) {
        final short rgba = RADIENT_TEXTURE_FORMAT.RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
        final short srgb = RADIENT_TEXTURE_FORMAT.RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB;
        run("texture mip descriptors and blobs survive disposal of their source wrappers", () -> {
            try (AssetFixture f = new AssetFixture()) {
                byte[] bytes = new byte[(int)Fixtures.integer("texture.mipBytes")];
                Arrays.fill(bytes, (byte)Fixtures.integer("texture.mipFill"));
                var source = f.blob(bytes);
                RadientTextureData data = new RadientTextureData();
                data.Width = Fixtures.integer("texture.width");
                data.Height = Fixtures.integer("texture.height");
                data.Format = rgba;
                data.GenerateMips = false;
                RadientTextureMipData first = new RadientTextureMipData();
                first.pDataBlob = source;
                first.ByteOffset = Fixtures.integer("texture.firstOffset");
                first.Stride = Fixtures.integer("texture.firstStride");
                RadientTextureMipData second = new RadientTextureMipData();
                second.pDataBlob = source;
                second.ByteOffset = Fixtures.integer("texture.secondOffset");
                data.pMipLevels = new RadientTextureMipData[]{first, second};
                data.MipLevelCount = 2;
                RadientTextureLoadInfo info = new RadientTextureLoadInfo();
                info.URI = Fixtures.string("texture.uri");
                info.EncodedTextureIsSRGB = true;
                info.pTextureData = data;
                var created = f.manager.LoadTexture(info);
                var texture = f.asset(created.ppTexture, created.returnValue);
                source.close();
                data.Width = 999;
                data.pMipLevels = new RadientTextureMipData[0];
                f.ready(texture);
                equal(RADIENT_ASSET_TYPE.RADIENT_ASSET_TYPE_TEXTURE, texture.GetType());
                equal(Fixtures.string("texture.uri"), texture.GetReference().URI);
                desc(texture, Fixtures.integer("texture.width"), Fixtures.integer("texture.height"), rgba, 2);
                var snapshot = texture.GetDesc();
                texture.close();
                equal(Fixtures.integer("texture.width"), snapshot.Width);
            }
        });

        run("raw texture loading generates the missing mip tail with native defaults", () -> {
            try (AssetFixture f = new AssetFixture()) {
                int dimension = (int)Fixtures.integer("texture.generatedDimension");
                byte[] bytes = new byte[dimension * dimension * 4];
                Arrays.fill(bytes, (byte)255);
                RadientTextureLoadInfo info = new RadientTextureLoadInfo();
                info.pTextureData = f.textureData(dimension, dimension, srgb, bytes);
                var created = f.manager.LoadTexture(info);
                var texture = f.ready(f.asset(created.ppTexture, created.returnValue));
                desc(texture, dimension, dimension, srgb, Fixtures.integer("texture.generatedMips"));
            }
        });

        run("encoded image bytes pass through the production decoder", () -> {
            try (AssetFixture f = new AssetFixture()) {
                var source = f.blob(Fixtures.assetBytes("colors.tga"));
                RadientTextureLoadInfo info = new RadientTextureLoadInfo();
                info.pDataBlob = source;
                info.EncodedTextureIsSRGB = true;
                var created = f.manager.LoadTexture(info);
                var texture = f.asset(created.ppTexture, created.returnValue);
                source.close();
                f.ready(texture);
                desc(texture, 2, 1, srgb, 2);
            }
        });

        run("texture references preserve their base in independent snapshots", () -> {
            try (AssetFixture f = new AssetFixture()) {
                RadientTextureLoadInfo info = new RadientTextureLoadInfo();
                info.URI = "image%2Etga";
                info.BaseURI = "opaque:document/model.gltf";
                info.pDataBlob = f.blob(Fixtures.assetBytes("colors.tga"));
                var created = f.manager.LoadTexture(info);
                var texture = f.asset(created.ppTexture, created.returnValue);
                var before = texture.GetReference();
                f.ready(texture);
                var after = texture.GetReference();
                texture.close();
                equal("image%2Etga", after.URI);
                equal("opaque:document/model.gltf", after.BaseURI);
                equal(before.URI, after.URI);
                equal(before.BaseURI, after.BaseURI);
            }
        });

        run("texture inputs preserve validation failures and reject an active blob writer", () -> {
            try (AssetFixture f = new AssetFixture()) {
                RadientTextureLoadInfo info = new RadientTextureLoadInfo();
                info.pTextureData = new RadientTextureData();
                info.pTextureData.Width = 1;
                info.pTextureData.Height = 1;
                info.pTextureData.Format = rgba;
                var invalid = f.manager.LoadTexture(info);
                f.keep(invalid.ppTexture);
                equal(RADIENT_STATUS_INVALID_ARGUMENT, invalid.returnValue);
                RadientDataBlobCreateInfo blobInfo = new RadientDataBlobCreateInfo();
                blobInfo.Size = 4;
                var createdBlob = CreateRadientMutableDataBlob(blobInfo);
                var blob = f.value(createdBlob.ppBlob, createdBlob.returnValue);
                var acquired = blob.BeginWrite();
                var scope = f.value(acquired.ppData, acquired.returnValue);
                scope.copyFrom(Fixtures.bytes("texture.lockedPixel"));
                RadientTextureMipData mip = new RadientTextureMipData();
                mip.pDataBlob = blob;
                info.pTextureData.pMipLevels = new RadientTextureMipData[]{mip};
                info.pTextureData.MipLevelCount = 1;
                var locked = f.manager.LoadTexture(info);
                f.keep(locked.ppTexture);
                equal(RADIENT_STATUS_INVALID_OPERATION, locked.returnValue);
                scope.close();
            }
        });
    }
}
