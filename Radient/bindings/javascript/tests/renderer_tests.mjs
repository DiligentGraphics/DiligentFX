import {readFixtures} from "./fixtures.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const data = readFixtures(testData);
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_NO_CHANGE: NO_CHANGE,
        RADIENT_STATUS_INVALID_ARGUMENT: INVALID_ARGUMENT} = constants.RADIENT_STATUS;

    function fixture(t) {
        const owned = [];
        t.after(() => { for (const object of owned.reverse()) object.dispose(); });
        function value(result) {
            assert.equal(result.status, OK);
            assert.ok(result.value);
            owned.push(result.value);
            return result.value;
        }
        const engine = value(api.CreateRadientEngine({WorkerThreadCount: 1}));
        const renderer = value(engine.CreateRenderer({
            Name: data.string("renderer.name"),
            MultiDrawBatchSize: data.number("renderer.batchSize"),
        }));
        const target = value(renderer.CreateRenderTarget({Name: data.string("renderer.targetName")}));
        const scene = value(engine.CreateScene({Desc: {Name: data.string("renderer.sceneName")}}));
        const view = value(renderer.CreateView({Name: data.string("renderer.viewName"), pScene: scene, pRenderTarget: target}));
        function snapshot() {
            const desc = view.GetDesc();
            for (const object of [desc.pScene, desc.pRenderTarget, desc.Environment.pEnvironmentMap, desc.Skybox.pTexture]) {
                if (object) owned.push(object);
            }
            return desc;
        }
        return {engine, renderer, target, scene, view, snapshot};
    }

    test("renderer and view descriptors retain interfaces and copy settings", t => {
        const {renderer, target, scene, view, snapshot} = fixture(t);
        assert.equal(renderer.GetDesc().Name, data.string("renderer.name"));
        assert.equal(renderer.GetDesc().MultiDrawBatchSize, data.number("renderer.batchSize"));
        assert.deepEqual(target.GetDesc(), {Name: data.string("renderer.targetName")});
        assert.deepEqual(target.GetSize(), {Width: 0, Height: 0});
        const before = snapshot();
        assert.equal(before.pScene.GetDesc().Name, data.string("renderer.sceneName"));
        assert.equal(before.pRenderTarget.GetDesc().Name, data.string("renderer.targetName"));
        assert.equal(view.SetScene(null), OK);
        assert.equal(view.SetRenderTarget(null), OK);
        assert.equal(snapshot().pScene, null);
        assert.equal(snapshot().pRenderTarget, null);
        assert.equal(before.pScene.GetDesc().Name, data.string("renderer.sceneName"));
        assert.equal(view.SetScene(scene), OK);
        assert.equal(view.SetRenderTarget(target), OK);
        assert.equal(view.SetScene(scene), NO_CHANGE);
        const camera = BigInt(data.string("renderer.camera"));
        assert.equal(view.SetCamera(camera), OK);
        assert.equal(snapshot().Camera, camera);
        const color = data.vector("renderer.clearColor");
        assert.equal(view.SetClearColor(color), OK);
        color.x = data.number("renderer.mutatedColorX");
        assert.equal(snapshot().ClearColor.x, data.vector("renderer.clearColor").x);
        const debug = constants.RADIENT_DEBUG_VISUALIZATION.RADIENT_DEBUG_VISUALIZATION_BASE_COLOR;
        assert.equal(view.SetDebugVisualization(debug), OK);
        assert.equal(snapshot().DebugVisualization, debug);
        assert.equal(view.SetIBLEnabled(false), OK);
        assert.equal(snapshot().EnableIBL, false);
    });

    test("all view effect descriptors marshal nested settings and native defaults", t => {
        const {view, snapshot} = fixture(t);
        const [Saturation, Slope, Power, Offset] = data.numbers("renderer.agx");
        const cases = [
            ["Environment", {Intensity: data.number("renderer.environmentIntensity"), Yaw: data.number("renderer.environmentYaw")},
                "Intensity", data.number("renderer.environmentIntensity")],
            ["Skybox", {Exposure: data.number("renderer.skyboxExposure")},
                "Exposure", data.number("renderer.skyboxExposure")],
            ["ToneMapping", {AgX: {Slope}}, "AgX", {Saturation, Slope, Power, Offset}],
            ["Bloom", {Enabled: true, Intensity: data.number("renderer.bloomIntensity")},
                "Intensity", data.number("renderer.bloomIntensity")],
            ["TemporalAntiAliasing", {Enabled: true, TemporalStabilityFactor: data.number("renderer.taaStability")},
                "TemporalStabilityFactor", data.number("renderer.taaStability")],
            ["SSAO", {Enabled: true, EffectRadius: data.number("renderer.ssaoRadius")},
                "EffectRadius", data.number("renderer.ssaoRadius")],
            ["SSR", {Enabled: true, MaxTraversalIntersections: data.number("renderer.ssrIntersections")},
                "MaxTraversalIntersections", data.number("renderer.ssrIntersections")],
            ["DepthOfField", {Enabled: true, BokehKernelRingCount: data.number("renderer.dofRings")},
                "BokehKernelRingCount", data.number("renderer.dofRings")],
        ];
        for (const [name, settings, field, expected] of cases) {
            assert.equal(view[`Set${name}`](settings), OK, name);
            assert.deepEqual(snapshot()[name][field], expected, name);
            assert.equal(view[`Set${name}`](settings), NO_CHANGE, name);
        }
    });

    test("frame calls marshal times and view references without a GPU", t => {
        const {renderer, view} = fixture(t);
        assert.ok(renderer.BeginFrame({
            Time: data.number("renderer.frameTime"),
            DeltaTime: data.number("renderer.frameDeltaTime"),
        }) >= 0);
        try {
            // A CPU target has no attachment and consequently has zero size.
            assert.equal(renderer.Render({pView: view}), INVALID_ARGUMENT);
            assert.throws(() => renderer.Render({pView: null}), TypeError);
        } finally {
            assert.equal(renderer.EndFrame(), OK);
        }
        assert.ok(renderer.BeginFrame({}) >= 0);
        assert.equal(renderer.EndFrame(), OK);
    });
}
