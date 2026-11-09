import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";
import {registerTests} from "./wasm_binary_tests.mjs";

const moduleURL = pathToFileURL(process.argv[2]);
const constants = await import(moduleURL);
registerTests({test, assert, constants, moduleURL});
