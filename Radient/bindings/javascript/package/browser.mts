/// <reference types="@webgpu/types" preserve="true" />
import type {BrowserOptions, RadientBrowser} from "./dist/RadientBrowser.mjs";
import {importRuntime} from "./runtime-loader.mjs";

export type {RadientBrowser} from "./dist/RadientBrowser.mjs";
export type PackageBrowserOptions = BrowserOptions & {
    /** Absolute URL of copied runtime assets, ending in '/'. */
    assetBaseURL: string | URL;
};

export default async function createRadientBrowser({assetBaseURL, ...options}: PackageBrowserOptions): Promise<RadientBrowser> {
    const {default: create} = await importRuntime(assetBaseURL, "RadientBrowser.mjs");
    return create(options);
}
