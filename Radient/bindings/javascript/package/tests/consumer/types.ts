import loadRadient, {type PackageOptions, type Bindings} from "@diligentgraphics/radient";
import createRadientBrowser, {type RadientBrowser} from "@diligentgraphics/radient/browser";

// Compile-only contract checks against declarations from the installed tarball.
function checkTypes(canvas: HTMLCanvasElement, assets: URL) {
    const options: PackageOptions = {assetBaseURL: assets};
    const api: Promise<Bindings> = loadRadient(options);
    const browser: Promise<RadientBrowser> = createRadientBrowser({canvas, assetBaseURL: assets});
    // @ts-expect-error The asset location is required.
    loadRadient({});
    // @ts-expect-error Asset locations must be strings or URLs.
    loadRadient({assetBaseURL: 123});
    // @ts-expect-error Browser initialization requires a canvas.
    createRadientBrowser({assetBaseURL: assets});
    // @ts-expect-error A generic element is not a canvas.
    createRadientBrowser({canvas: document.createElement("div"), assetBaseURL: assets});
    return {api, browser};
}

// @ts-expect-error Runtime loading is an internal helper, not a public export.
type InternalHelper = typeof import("@diligentgraphics/radient").importRuntime;
