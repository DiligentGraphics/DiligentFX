import {createAssetFixture} from "./asset_test_helpers.mjs";
import {sceneEntities} from "./import_test_helpers.mjs";
import {readFixtures} from "./fixtures.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const data = readFixtures(testData);
    const base = globalThis.process?.env?.RADIENT_TEST_NETWORK_URL ?? data.string("network.baseURL");
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_INVALID_ARGUMENT: INVALID, RADIENT_STATUS_FAILED: FAILED} = constants.RADIENT_STATUS;
    const resolverInfo = () => ({ConnectTimeoutMs: data.number("network.connectTimeout"),
        RequestTimeoutMs: data.number("network.requestTimeout"), MaxRedirects: data.number("network.redirectLimit")});

    test("network resolver factory validates configuration and returns an opaque owned interface", t => {
        const result = api.CreateRadientNetworkAssetResolver({});
        t.after(() => result.value?.dispose());
        assert.equal(result.status, OK);
        assert.ok(result.value !== null);
        assert.equal(typeof result.value.OpenAsset, "undefined");
        for (const invalid of [{ConnectTimeoutMs: 0}, {RequestTimeoutMs: 0}, {MaxRedirects: 0xffffffff}]) {
            const result = api.CreateRadientNetworkAssetResolver({...resolverInfo(), ...invalid});
            t.after(() => result.value?.dispose());
            assert.equal(result.status, INVALID);
            assert.equal(result.value, null);
        }
    });

    for (const explicit of [false, true]) {
        test(`network imports follow redirects and load buffers/textures (${explicit ? "explicit resolver" : "default routing"})`, async t => {
            let resolver;
            if (explicit) {
                const result = api.CreateRadientNetworkAssetResolver(resolverInfo());
                resolver = result.value;
                t.after(() => resolver?.dispose());
                assert.equal(result.status, OK);
            }
            const f = createAssetFixture(t, api, assert, constants, testData, {Assets: {pAssetResolver: resolver ?? null}});
            resolver?.dispose(); // The engine owns the resolver before any HTTP work starts.
            const importer = f.keep(f.value(f.engine.CreateSceneImporter(f.writer)));
            const entries = data.string("network.entries").split(",");
            const finals = data.string("network.finals").split(",");
            for (let i = 0; i < entries.length; ++i) {
                const asset = await f.ready(f.asset(f.manager.LoadScene({URI: base + entries[i]})));
                const root = f.value(importer.InstantiateScene(asset, {}));
                assert.equal(importer.ProcessPendingImports(), OK);
                const entity = sceneEntities(f, root).find(e => f.value(f.scene.HasComponent(e, constants.RADIENT_COMPONENT_TYPE_MESH)));
                assert.ok(entity !== undefined);
                const mesh = await f.ready(f.keep(f.value(f.scene.GetMesh(entity)).pMesh));
                const desc = mesh.GetDesc();
                desc.pPrimitives.forEach(primitive => f.keep(primitive.pMaterial));
                assert.equal(desc.pGeometries[0].VertexCount, data.number("network.vertexCount"));
                const material = desc.pPrimitives[0].pMaterial;
                const definition = f.keep(material.GetDefinition());
                const parameter = f.value(definition.FindParameter(constants.RadientStandardMaterialBaseColorTextureParameterNames.Texture));
                const texture = await f.ready(f.keep(f.value(material.GetTexture(parameter, 0))));
                assert.equal(texture.GetDesc().Width, data.number("network.textureWidth"));
                assert.equal(texture.GetReference().BaseURI, base + finals[i]);
            }
        });
    }

    test("network resolver redirect configuration reaches native loading", async t => {
        const created = api.CreateRadientNetworkAssetResolver({...resolverInfo(), MaxRedirects: 0});
        const resolver = created.value;
        t.after(() => resolver?.dispose());
        assert.equal(created.status, OK);
        const f = createAssetFixture(t, api, assert, constants, testData, {Assets: {pAssetResolver: resolver}});
        resolver.dispose();
        const loaded = f.manager.LoadScene({URI: base + data.string("network.entries").split(",")[0]});
        const asset = f.keep(loaded.value);
        assert.ok(asset !== null);
        assert.ok([OK, constants.RADIENT_STATUS.RADIENT_STATUS_PENDING, FAILED].includes(loaded.status));
        assert.equal(await f.loadStatus(asset), FAILED);
    });
}
