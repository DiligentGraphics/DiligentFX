import type {Bindings, ModuleOptions} from "./dist/Radient.mjs";
import {importRuntime} from "./runtime-loader.mjs";

export type * from "./dist/Radient.mjs";
export type PackageOptions = ModuleOptions & {
    /** Absolute URL (or file: URL in Node) of copied runtime assets, ending in '/'. */
    assetBaseURL: string | URL;
};

export default async function loadRadient({assetBaseURL, ...options}: PackageOptions): Promise<Bindings> {
    const {default: create} = await importRuntime(assetBaseURL, "Radient.mjs");
    return create(options);
}
