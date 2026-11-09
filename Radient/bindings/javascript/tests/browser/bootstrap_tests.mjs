// Browser-only lifecycle tests. Image capture and glTF golden comparisons belong to
// the rendering suites; this checks the reusable WebGPU/canvas adapter itself.
// Observe the resources created by one initialization attempt. Always clean up
// after a failed assertion too, so a regression cannot leak workers into later tests.
function trackInitializationResources(t) {
    const NativeWorker = globalThis.Worker;
    const requestDevice = GPUAdapter.prototype.requestDevice;
    const workers = new Set();
    const devices = [];
    const destroyedDevices = new Set();
    let workerCount = 0;
    t.after(() => {
        globalThis.Worker = NativeWorker;
        GPUAdapter.prototype.requestDevice = requestDevice;
        for (const worker of workers) worker.terminate();
        for (const device of devices) device.destroy();
    });
    globalThis.Worker = class extends NativeWorker {
        constructor(...args) {
            super(...args);
            workers.add(this);
            ++workerCount;
        }
        terminate() {
            workers.delete(this);
            super.terminate();
        }
    };
    GPUAdapter.prototype.requestDevice = async function(...args) {
        const device = await requestDevice.apply(this, args);
        devices.push(device);
        const destroy = device.destroy;
        device.destroy = function() {
            destroyedDevices.add(this);
            destroy.call(this);
        };
        return device;
    };
    return {workers, devices, destroyedDevices, get workerCount() { return workerCount; }};
}

export function registerTests({test, assert}) {
    for (const [name, options] of [
        ["missing Wasm", {locateFile(path, prefix) {
            return prefix + (path.endsWith(".wasm") ? "missing.wasm" : path);
        }}],
        ["conflicting virtual file paths", {files: {
            "/conflict": new Uint8Array([1]),
            "/conflict/child": new Uint8Array([2]),
        }}],
    ]) {
        test(`WebGPU initialization releases workers and device after ${name}`, async t => {
            const {default: createRadientBrowser} = await import("/generated/RadientBrowser.mjs");
            const resources = trackInitializationResources(t);
            let initialized = false;
            let error;
            try {
                const browser = await createRadientBrowser({
                    canvas: document.createElement("canvas"),
                    moduleOptions: {...options, onRuntimeInitialized() { initialized = true; }},
                });
                t.after(() => browser.dispose());
            } catch (caught) {
                error = caught;
            }
            assert.ok(error, "Initialization must reject");
            assert.equal(initialized, false, "The failure must precede onRuntimeInitialized");
            assert.equal(resources.devices.length, 1);
            assert.ok(resources.destroyedDevices.has(resources.devices[0]), "The GPU device must be destroyed");
            assert.equal((await resources.devices[0].lost).reason, "destroyed");
            assert.ok(resources.workerCount > 0, "The module must have preloaded workers before failing");
            assert.equal(resources.workers.size, 0, "All preloaded workers must be terminated");
        });
    }
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
