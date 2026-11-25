# Radient SDK

Prebuilt JavaScript/TypeScript bindings and threaded WebAssembly with WebGPU.
No native compiler or Emscripten installation is needed by consumers.

Copy the matching runtime assets into your application's static directory during
its build (for example, Vite's `public` directory):

```sh
npm install @diligentgraphics/radient
npx radient-copy-assets public/radient
```

```ts
import createRadientBrowser from "@diligentgraphics/radient/browser";

const browser = await createRadientBrowser({
    canvas: document.querySelector<HTMLCanvasElement>("canvas")!,
    assetBaseURL: new URL("/radient/", location.href),
});
```

Use your application's deployment prefix instead of `/` when hosting below a
subdirectory. `assetBaseURL` must be absolute and end with `/`. Keep these files
together: the Wasm binary, its JavaScript runtime, workers, and binding helpers
belong to one package version. Do not bundle or transform the copied files.
Serve `.wasm` as `application/wasm` and `.mjs` as JavaScript. Version the asset
URL when upgrading, so cached files from different releases cannot be mixed.

Hosting requires HTTPS (localhost works for development), WebGPU, and these
response headers:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Remote models must allow CORS. Local paths refer to the Wasm virtual filesystem;
`moduleOptions.files` preloads a map of absolute paths to `Uint8Array` values.
Relative GLTF dependencies must also be supplied, preserving directory structure.
Runtime file-picker mounting helpers are not included yet.

The default engine uses one preloaded worker. Keep that default unless you build
an SDK with a larger pool. Asset loading is asynchronous: poll status between
frames while yielding to the browser. Release acquired interface wrappers with
`dispose()`; garbage collection does not release native objects. Dispose scene,
asset, renderer, and view wrappers before `browser.dispose()`.

For CPU-only use, import the default loader from `@diligentgraphics/radient` with
`{assetBaseURL, ...moduleOptions}`. It returns the generated API. In Node,
`assetBaseURL` can be a `file:` URL. GPU browser initialization uses the separate
`/browser` entry point.

`build-info.json` records checkout revisions at packaging time, configuration, and
runtime hashes. Rebuild after source changes before packaging; checkout metadata
does not prove that pre-existing binaries are up to date.
Third-party license texts are included under `licenses/`.
