import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";
const {registerTests} = await import(pathToFileURL(process.argv[3]));

const constants = await import(pathToFileURL(process.argv[2]));
let wasm;
const api = await constants.default({onRuntimeInitialized() { wasm = this; }});
registerTests({test, assert, api, constants, wasm});
