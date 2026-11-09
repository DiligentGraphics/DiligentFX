import createRadient from "./Radient.mjs";

// Match the optional WebGPU capabilities enabled by Diligent's browser samples.
const optionalFeatures = [
    "depth-clip-control", "depth32float-stencil8", "texture-compression-bc",
    "indirect-first-instance", "shader-f16", "rg11b10ufloat-renderable",
    "bgra8unorm-storage", "float32-filterable", "float32-blendable",
    "texture-formats-tier1", "texture-formats-tier2",
    "unorm16-texture-formats", "snorm16-texture-formats",
];

/** Create one GPU-backed Radient engine and canvas swap chain in a new Wasm module. */
export default async function createRadientBrowser({
    canvas, moduleOptions = {}, engineCreateInfo = {},
    powerPreference = "high-performance", forceFallbackAdapter = false,
} = {}) {
    if (typeof HTMLCanvasElement === "undefined" || !(canvas instanceof HTMLCanvasElement)) {
        throw new TypeError("canvas must be an HTMLCanvasElement");
    }
    if (!globalThis.crossOriginIsolated) {
        throw new Error("Radient requires cross-origin isolation for its Wasm workers");
    }
    if (!navigator.gpu) {
        throw new Error("WebGPU is unavailable in this browser");
    }
    const adapter = await navigator.gpu.requestAdapter({powerPreference, forceFallbackAdapter});
    if (!adapter) throw new Error("No WebGPU adapter is available");
    const maxDimension = adapter.limits.maxTextureDimension2D;
    function validateSize(width, height) {
        if (!Number.isInteger(width) || !Number.isInteger(height) ||
            width <= 0 || height <= 0 || width > maxDimension || height > maxDimension) {
            throw new RangeError(`Canvas dimensions must be positive integers no larger than ${maxDimension}`);
        }
    }
    validateSize(canvas.width, canvas.height);
    const device = await adapter.requestDevice({
        requiredFeatures: optionalFeatures.filter(feature => adapter.features.has(feature)),
        requiredLimits: {
            maxTextureDimension2D: maxDimension,
            maxColorAttachmentBytesPerSample: adapter.limits.maxColorAttachmentBytesPerSample,
        },
    });
    let engine;
    let api;
    let wasm;
    function check(status, operation) {
        if (status < 0) throw new Error(`${operation} failed (Radient status ${status})`);
    }
    function alive() {
        if (!engine) throw new Error("The Radient browser instance has been disposed");
    }
    try {
        api = await createRadient({
            ...moduleOptions, canvas, preinitializedWebGPUDevice: device,
            onRuntimeInitialized() {
                wasm = this;
                moduleOptions.onRuntimeInitialized?.call(this);
            },
        });
        const result = api.CreateRadientBrowserEngine({WorkerThreadCount: 1, ...engineCreateInfo});
        check(result.status, "Creating the WebGPU engine");
        engine = result.value;
        if (!engine) throw new Error("The WebGPU engine was not created; build RadientWasm with WebGPU enabled");
    } catch (error) {
        wasm?.PThread.terminateAllThreads();
        device.destroy();
        throw error;
    }
    return {
        api, engine, canvas, device,
        createRenderTarget(renderer) {
            alive();
            const result = api.CreateRadientCanvasRenderTarget(engine, renderer);
            check(result.status, "Creating the canvas render target");
            return result.value;
        },
        // Call between frames. Existing render targets follow the swap chain.
        resize(width, height) {
            alive();
            validateSize(width, height);
            if (canvas.width !== width) canvas.width = width;
            if (canvas.height !== height) canvas.height = height;
            check(api.ResizeRadientCanvas(engine, width, height), "Resizing the canvas");
        },
        // Call after EndFrame, before awaiting or returning to the event loop.
        present() {
            alive();
            check(api.PresentRadientCanvas(engine), "Presenting the canvas");
        },
        // Dispose views, renderers, scenes, and assets first. Stop cancels uploads
        // so engine destruction cannot wait for staging space on this thread.
        dispose() {
            if (!engine) return;
            check(api.StopRadientBrowserEngine(engine), "Stopping the WebGPU engine");
            engine.dispose();
            engine = null;
            // Native engine shutdown joins its threads. Release the module's
            // idle/preloaded workers too, so they do not retain the Wasm memory.
            wasm.PThread.terminateAllThreads();
            device.destroy();
        },
    };
}
