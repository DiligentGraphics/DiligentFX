type RuntimeModules = {
    "Radient.mjs": typeof import("./dist/Radient.mjs");
    "RadientBrowser.mjs": typeof import("./dist/RadientBrowser.mjs");
};

// Keep Emscripten outside application bundling: workers must import the same
// runtime files at stable URLs. Both Vite and Webpack must preserve this import.
// Restrict filenames to modules with known types;
// the dynamic URL itself cannot be resolved by the TypeScript compiler.
export async function importRuntime<File extends keyof RuntimeModules>(
    assetBaseURL: string | URL, file: File
): Promise<RuntimeModules[File]> {
    const base = new URL(assetBaseURL);
    if (!base.pathname.endsWith("/")) throw new TypeError("assetBaseURL must end with '/'");
    return import(/* @vite-ignore */ /* webpackIgnore: true */ new URL(file, base).href);
}
