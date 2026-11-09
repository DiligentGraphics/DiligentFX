import type {Bindings, ModuleOptions, RadientEngineCreateInfo, IRadientEngine,
    IRadientRenderer, IRadientRenderTarget} from "./Radient.mjs";

export interface BrowserOptions {
    canvas: HTMLCanvasElement;
    moduleOptions?: ModuleOptions;
    /** Defaults to one worker, matching the default preloaded Wasm worker pool. */
    engineCreateInfo?: RadientEngineCreateInfo;
    powerPreference?: GPUPowerPreference;
    forceFallbackAdapter?: boolean;
    /** Allow native image readback from the canvas swap chain. Defaults to false. */
    enableCapture?: boolean;
}

export interface RadientBrowser {
    readonly api: Bindings;
    readonly engine: IRadientEngine;
    readonly canvas: HTMLCanvasElement;
    /** Observe device.lost and uncapturederror here; automatic recovery is not provided. */
    readonly device: GPUDevice;
    createRenderTarget(renderer: IRadientRenderer): IRadientRenderTarget;
    /** Resize between frames using physical pixel dimensions. */
    resize(width: number, height: number): void;
    /** Present after EndFrame, before yielding to the browser event loop. */
    present(): void;
    /** Stop uploads and release the engine/device after disposing application objects. */
    dispose(): void;
}

export default function createRadientBrowser(options: BrowserOptions): Promise<RadientBrowser>;
