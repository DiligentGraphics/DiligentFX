import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";
const {registerTests, moduleOptions = {}} = await import(pathToFileURL(process.argv[3]));

const moduleURL = pathToFileURL(process.argv[2]);
const constants = await import(moduleURL);
let wasm;
const api = await constants.default({...moduleOptions, onRuntimeInitialized() { wasm = this; }});
registerTests({test, assert, api, constants, wasm, moduleURL});
