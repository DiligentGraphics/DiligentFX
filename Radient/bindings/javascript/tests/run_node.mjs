import {readFile} from "node:fs/promises";
import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";
const {registerTests, moduleOptions = {}, assetFiles = []} = await import(pathToFileURL(process.argv[3]));

const moduleURL = pathToFileURL(process.argv[2]);
const constants = await import(moduleURL);
const testData = await readFile(new URL("../../tests/integration/fixtures.properties", import.meta.url), "utf8");
const assets = Object.fromEntries(await Promise.all(assetFiles.map(async name => [name,
    new Uint8Array(await readFile(new URL("../../tests/integration/assets/" + name, import.meta.url)))])));
const options = assetFiles.length ? {...moduleOptions, files: {...moduleOptions.files,
    ...Object.fromEntries(assetFiles.map(name => ["/assets/" + name, assets[name]]))}} : moduleOptions;
let wasm;
const api = await constants.default({...options, onRuntimeInitialized() { wasm = this; }});
registerTests({test, assert, api, constants, wasm, moduleURL, testData, assets});
