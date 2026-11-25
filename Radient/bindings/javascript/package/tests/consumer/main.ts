import loadRadient, {type NativeObject, type IRadientAsset} from "@diligentgraphics/radient";
import createRadientBrowser from "@diligentgraphics/radient/browser";

const config = JSON.parse(document.querySelector("#config")!.textContent!);
const results: {name: string; errors: string[]}[] = [];
// Both bundlers are served under this prefix by the package test harness.
const assetBaseURL = new URL("/viewer/radient/", location.href);
const delay = () => new Promise(resolve => setTimeout(resolve, 1));
function check(status: number) {
    if (status < 0) throw new Error(`Radient status ${status}`);
    return status;
}
function value<T>(result: {status: number; value: T | null}): T {
    check(result.status);
    if (result.value === null) throw new Error("Missing result");
    return result.value;
}
async function test(name: string, run: () => Promise<void>) {
    const result = {name, errors: [] as string[]};
    try { await run(); } catch (error) { result.errors.push(error instanceof Error ? error.stack ?? error.message : String(error)); }
    results.push(result);
}

await test("installed root export initializes and creates an owning blob", async () => {
    // Do not leave a second worker pool alive during the GPU test.
    let wasm: any;
    const api = await loadRadient({assetBaseURL, ...{
        onRuntimeInitialized(this: unknown) { wasm = this; },
    }});
    try {
        const blob = value(api.CreateRadientDataBlob({Size: 3n, pData: new Uint8Array([1, 2, 3])}));
        try { if (blob.GetSize() !== 3n) throw new Error("Incorrect blob size"); }
        finally { blob.dispose(); }
    } finally { wasm.PThread.terminateAllThreads(); }
});

await test("installed browser export imports local/network GLTF and submits WebGPU frames", async () => {
    const base = config.test.networkBase;
    const bytes = async (path: string) => {
        const response = await fetch(base + path);
        if (!response.ok) throw new Error(`Fixture HTTP ${response.status}`);
        return new Uint8Array(await response.arrayBuffer());
    };
    // Use the shared HTTP fixture bytes for local-file loading as well.
    const files = Object.fromEntries(await Promise.all(["scene.gltf", "mesh.bin", "image.tga"].map(async name =>
        [`/local/${name}`, await bytes(`/model/${name}`)])));
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 64;
    document.body.append(canvas);
    const browser = await createRadientBrowser({canvas, assetBaseURL, moduleOptions: {files}});
    const owned: NativeObject[] = [];
    const keep = <T extends NativeObject>(object: T): T => { owned.push(object); return object; };
    const errors: string[] = [];
    browser.device.addEventListener("uncapturederror", event => errors.push(event.error.message));
    try {
        const {api, engine} = browser;
        const renderer = keep(value(engine.CreateRenderer({})));
        const target = keep(browser.createRenderTarget(renderer));
        const scene = keep(value(engine.CreateScene({})));
        const writer = keep(value(engine.CreateSceneWriter(scene)));
        const importer = keep(value(engine.CreateSceneImporter(writer)));
        const view = keep(value(renderer.CreateView({pScene: scene, pRenderTarget: target, EnableIBL: false})));
        const camera = value(writer.CreateEntity({Transform: {Position: {x: 0, y: 0, z: 3}}}));
        check(writer.SetCamera(camera, {ClippingRange: {x: 0.1, y: 100}}));
        check(view.SetCamera(camera));
        let frame = 0;
        async function renderUntilReady(asset: IRadientAsset) {
            const deadline = performance.now() + 45000;
            for (;;) {
                const imported = check(importer.ProcessPendingImports());
                check(writer.CommitChanges());
                const loaded = check(api.GetRadientAssetLoadStatus(asset));
                const begun = check(renderer.BeginFrame({Time: frame++ / 60, DeltaTime: 1 / 60}));
                let rendered: number;
                try { rendered = check(renderer.Render({pView: view})); }
                finally { check(renderer.EndFrame()); }
                browser.present();
                await browser.device.queue.onSubmittedWorkDone();
                await delay();
                if (errors.length) throw new Error(errors.join("\n"));
                if (imported === 0 && loaded === 0 && begun === 0 && rendered === 0) break;
                if (performance.now() > deadline) throw new Error("Import/render timed out");
            }
        }
        for (const uri of ["/local/scene.gltf", base + "/entry.glb"]) {
            const previousRoots = scene.GetRootEntityCount();
            const imported = value(importer.ImportScene({URI: uri}, {}));
            const asset = keep(imported.ppScene!);
            await renderUntilReady(asset);
            if (scene.GetRootEntityCount() !== previousRoots + 1 ||
                value(scene.GetChildCount(imported.RootEntity)) === 0) {
                throw new Error("Scene hierarchy was not instantiated");
            }
        }
        browser.resize(80, 48);
        if (target.GetSize().Width !== 80) throw new Error("Canvas resize failed");
    } finally {
        for (const object of owned.reverse()) object.dispose();
        browser.dispose();
        canvas.remove();
    }
});

await fetch("/result", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({
    tests: results, userAgent: navigator.userAgent, crossOriginIsolated,
    sharedMemory: typeof SharedArrayBuffer !== "undefined" && crossOriginIsolated,
})});
