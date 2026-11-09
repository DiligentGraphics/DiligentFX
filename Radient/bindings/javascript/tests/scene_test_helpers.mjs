// Use one worker, matching the test module's preloaded pthread pool. These
// fixtures exercise the production engine and scene without creating a GPU.
export function createSceneFixture(t, api, assert, OK, name = "Scene") {
    let engine;
    let scene;
    let writer;
    t.after(() => {
        writer?.dispose();
        scene?.dispose();
        engine?.dispose();
    });
    function value(result) {
        assert.equal(result.status, OK);
        assert.ok(result.value !== null);
        return result.value;
    }
    engine = value(api.CreateRadientEngine({WorkerThreadCount: 1}));
    scene = value(engine.CreateScene({Desc: {Name: name}}));
    writer = value(engine.CreateSceneWriter(scene));
    return {engine, scene, writer, value};
}
