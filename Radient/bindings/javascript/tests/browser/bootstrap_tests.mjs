// Browser-only smoke test. Image capture and glTF golden comparisons belong to
// the rendering suites; this checks the reusable WebGPU/canvas adapter itself.
export function registerTests({test, assert}) {
    test("WebGPU engine attaches, presents, resizes existing targets, and shuts down", async t => {
        const {default: createRadientBrowser} = await import("/generated/RadientBrowser.mjs");
        const canvas = document.createElement("canvas");
        canvas.width = 48;
        canvas.height = 32;
        document.body.append(canvas);
        t.after(() => canvas.remove());
        const browser = await createRadientBrowser({canvas});
        t.after(() => browser.dispose());
        const errors = [];
        browser.device.addEventListener("uncapturederror", event => errors.push(event.error.message));
        const {RADIENT_STATUS_OK: OK} = browser.api.RADIENT_STATUS;
        const result = browser.engine.CreateRenderer({});
        assert.equal(result.status, OK);
        const renderer = result.value;
        t.after(() => renderer.dispose());
        const target = browser.createRenderTarget(renderer);
        t.after(() => target.dispose());
        const sceneResult = browser.engine.CreateScene({});
        assert.equal(sceneResult.status, OK);
        const scene = sceneResult.value;
        t.after(() => scene.dispose());
        const viewResult = renderer.CreateView({pScene: scene, pRenderTarget: target, EnableIBL: false});
        assert.equal(viewResult.status, OK);
        const view = viewResult.value;
        t.after(() => view.dispose());
        assert.deepEqual(target.GetSize(), {Width: 48, Height: 32});
        assert.ok(renderer.BeginFrame({Time: 1, DeltaTime: 1 / 60}) >= 0);
        try {
            assert.ok(renderer.Render({pView: view}) >= 0);
        } finally {
            assert.equal(renderer.EndFrame(), OK);
        }
        browser.present();
        await browser.device.queue.onSubmittedWorkDone();

        browser.resize(80, 40);
        assert.equal(canvas.width, 80);
        assert.equal(canvas.height, 40);
        assert.deepEqual(target.GetSize(), {Width: 80, Height: 40});
        assert.throws(() => browser.resize(0, 40), RangeError);
        assert.deepEqual(target.GetSize(), {Width: 80, Height: 40});
        assert.ok(renderer.BeginFrame({Time: 2, DeltaTime: 1}) >= 0);
        try {
            assert.ok(renderer.Render({pView: view}) >= 0);
        } finally {
            assert.equal(renderer.EndFrame(), OK);
        }
        browser.present();
        await browser.device.queue.onSubmittedWorkDone();
        assert.deepEqual(errors, []);
        view.dispose();
        scene.dispose();
        target.dispose();
        renderer.dispose();
        browser.dispose();
        assert.equal((await browser.device.lost).reason, "destroyed");
        browser.dispose(); // Idempotent, including the unconditional cleanup.
        assert.throws(() => browser.resize(80, 40), Error);
    });
}
