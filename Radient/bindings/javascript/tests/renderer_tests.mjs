export function registerTests({test, assert, api, constants}) {
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
        const renderer = value(engine.CreateRenderer({Name: "Renderer ä¸–ç•Œ", MultiDrawBatchSize: 8}));
        const target = value(renderer.CreateRenderTarget({Name: "CPU target"}));
        const scene = value(engine.CreateScene({Desc: {Name: "View scene"}}));
        const view = value(renderer.CreateView({Name: "View", pScene: scene, pRenderTarget: target}));
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
        assert.equal(renderer.GetDesc().Name, "Renderer ä¸–ç•Œ");
        assert.equal(renderer.GetDesc().MultiDrawBatchSize, 8);
        assert.deepEqual(target.GetDesc(), {Name: "CPU target"});
        assert.deepEqual(target.GetSize(), {Width: 0, Height: 0});
        const before = snapshot();
        assert.equal(before.pScene.GetDesc().Name, "View scene");
        assert.equal(before.pRenderTarget.GetDesc().Name, "CPU target");
        assert.equal(view.SetScene(null), OK);
        assert.equal(view.SetRenderTarget(null), OK);
        assert.equal(snapshot().pScene, null);
        assert.equal(snapshot().pRenderTarget, null);
        assert.equal(before.pScene.GetDesc().Name, "View scene");
        assert.equal(view.SetScene(scene), OK);
        assert.equal(view.SetRenderTarget(target), OK);
        assert.equal(view.SetScene(scene), NO_CHANGE);
        const camera = 123n;
        assert.equal(view.SetCamera(camera), OK);
        assert.equal(snapshot().Camera, camera);
        const color = {x: 0.25, y: 0.5, z: 0.75, w: 1};
        assert.equal(view.SetClearColor(color), OK);
        color.x = 1;
        assert.equal(snapshot().ClearColor.x, 0.25);
        const debug = constants.RADIENT_DEBUG_VISUALIZATION.RADIENT_DEBUG_VISUALIZATION_BASE_COLOR;
        assert.equal(view.SetDebugVisualization(debug), OK);
        assert.equal(snapshot().DebugVisualization, debug);
        assert.equal(view.SetIBLEnabled(false), OK);
        assert.equal(snapshot().EnableIBL, false);
    });

    test("all view effect descriptors marshal nested settings and native defaults", t => {
        const {view, snapshot} = fixture(t);
        const cases = [
            ["Environment", {Intensity: 2, Yaw: 0.5}, "Intensity", 2],
            ["Skybox", {Exposure: 2}, "Exposure", 2],
            ["ToneMapping", {AgX: {Slope: 2}}, "AgX", {Saturation: 1, Slope: 2, Power: 1, Offset: 0}],
            ["Bloom", {Enabled: true, Intensity: 0.5}, "Intensity", 0.5],
            ["TemporalAntiAliasing", {Enabled: true, TemporalStabilityFactor: 0.5}, "TemporalStabilityFactor", 0.5],
            ["SSAO", {Enabled: true, EffectRadius: 2}, "EffectRadius", 2],
            ["SSR", {Enabled: true, MaxTraversalIntersections: 32}, "MaxTraversalIntersections", 32],
            ["DepthOfField", {Enabled: true, BokehKernelRingCount: 4}, "BokehKernelRingCount", 4],
        ];
        for (const [name, settings, field, expected] of cases) {
            assert.equal(view[`Set${name}`](settings), OK, name);
            assert.deepEqual(snapshot()[name][field], expected, name);
            assert.equal(view[`Set${name}`](settings), NO_CHANGE, name);
        }
    });

    test("frame calls marshal times and view references without a GPU", t => {
        const {renderer, view} = fixture(t);
        assert.ok(renderer.BeginFrame({Time: 100.25, DeltaTime: 0.125}) >= 0);
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
