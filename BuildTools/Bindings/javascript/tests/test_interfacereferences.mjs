import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";
import {registerTests} from "./interfacereferences_tests.mjs";

const constants = await import(pathToFileURL(process.argv[2]));
const api = await constants.default();
registerTests({test, assert, api, constants});
