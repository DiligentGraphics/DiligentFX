// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class NetworkTest {
    private static RadientNetworkAssetResolverCreateInfo resolverInfo() {
        var info = new RadientNetworkAssetResolverCreateInfo();
        info.ConnectTimeoutMs = (long)Fixtures.number("network.connectTimeout");
        info.RequestTimeoutMs = (long)Fixtures.number("network.requestTimeout");
        info.MaxRedirects = (long)Fixtures.number("network.redirectLimit");
        return info;
    }

    private static RadientEngineCreateInfo engineInfo(IRadientAssetResolver resolver) {
        var info = new RadientEngineCreateInfo();
        info.Assets.pAssetResolver = resolver;
        return info;
    }

    private static RadientSceneLoadInfo loadInfo(String uri) {
        var info = new RadientSceneLoadInfo();
        info.URI = uri;
        return info;
    }

    private static void imports(String base, boolean explicit) {
        IRadientAssetResolver resolver = null;
        try {
            if (explicit) {
                var created = CreateRadientNetworkAssetResolver(resolverInfo());
                resolver = created.ppResolver;
                ok(created.returnValue);
                check(resolver != null);
            }
            try (AssetFixture f = new AssetFixture(engineInfo(resolver))) {
                if (resolver != null) resolver.close();
                var createdImporter = f.engine.CreateSceneImporter(f.writer);
                var importer = f.value(createdImporter.ppImporter, createdImporter.returnValue);
                String[] entries = Fixtures.string("network.entries").split(",");
                String[] finals = Fixtures.string("network.finals").split(",");
                for (int i = 0; i < entries.length; ++i) {
                    var loaded = f.manager.LoadScene(loadInfo(base + entries[i]));
                    var asset = f.ready(f.asset(loaded.ppScene, loaded.returnValue));
                    var instance = importer.InstantiateScene(asset, new RadientSceneInstantiateInfo());
                    ok(instance.returnValue);
                    ok(importer.ProcessPendingImports());
                    long entity = f.sceneEntities(instance.RootEntity).stream().filter(e -> f.hasComponent(e, RADIENT_COMPONENT_TYPE_MESH))
                        .findFirst().orElseThrow(() -> new AssertionError("Missing mesh entity"));
                    var mesh = f.ready(f.mesh(entity));
                    var desc = f.meshDesc(mesh);
                    equal((long)Fixtures.number("network.vertexCount"), desc.pGeometries[0].VertexCount);
                    var material = desc.pPrimitives[0].pMaterial;
                    var definition = f.keep(material.GetDefinition());
                    var parameter = f.parameter(definition, RadientStandardMaterialBaseColorTextureParameterNames().Texture);
                    var selected = material.GetTexture(parameter, 0);
                    var texture = f.ready(f.value(selected.ppTexture, selected.returnValue));
                    equal((long)Fixtures.number("network.textureWidth"), texture.GetDesc().Width);
                    equal(base + finals[i], texture.GetReference().BaseURI);
                }
            }
        } finally { if (resolver != null) resolver.close(); }
    }

    public static void main(String[] args) {
        String base = System.getenv("RADIENT_TEST_NETWORK_URL");
        check(base != null);
        run("network resolver factory validates configuration and returns an opaque owned interface", () -> {
            var defaults = new RadientNetworkAssetResolverCreateInfo();
            equal(10000L, defaults.ConnectTimeoutMs);
            equal(60000L, defaults.RequestTimeoutMs);
            equal(10L, defaults.MaxRedirects);
            var created = CreateRadientNetworkAssetResolver(defaults);
            try (var resolver = created.ppResolver) {
                ok(created.returnValue);
                check(resolver != null);
                check(java.util.Arrays.stream(resolver.getClass().getMethods()).noneMatch(method -> method.getName().equals("OpenAsset")));
            }
            for (int field = 0; field < 3; ++field) {
                var invalid = resolverInfo();
                if (field == 0) invalid.ConnectTimeoutMs = 0;
                else if (field == 1) invalid.RequestTimeoutMs = 0;
                else invalid.MaxRedirects = 0xffffffffL;
                var result = CreateRadientNetworkAssetResolver(invalid);
                try (var resolver = result.ppResolver) {
                    equal(RADIENT_STATUS_INVALID_ARGUMENT, result.returnValue);
                    equal(null, resolver);
                }
            }
        });
        run("network imports follow redirects and load buffers/textures (default routing)", () -> imports(base, false));
        run("network imports follow redirects and load buffers/textures (explicit resolver)", () -> imports(base, true));
        run("network resolver redirect configuration reaches native loading", () -> {
            var info = resolverInfo();
            info.MaxRedirects = 0;
            var created = CreateRadientNetworkAssetResolver(info);
            var resolver = created.ppResolver;
            try {
                ok(created.returnValue);
                check(resolver != null);
                try (AssetFixture f = new AssetFixture(engineInfo(resolver))) {
                    resolver.close();
                    var loaded = f.manager.LoadScene(loadInfo(base + Fixtures.string("network.entries").split(",")[0]));
                    var asset = f.keep(loaded.ppScene);
                    check(asset != null);
                    check(loaded.returnValue == RADIENT_STATUS_PENDING || loaded.returnValue == RADIENT_STATUS_OK || loaded.returnValue == RADIENT_STATUS_FAILED);
                    equal(RADIENT_STATUS_FAILED, f.loadStatus(asset));
                }
            } finally { if (resolver != null) resolver.close(); }
        });
    }
}
