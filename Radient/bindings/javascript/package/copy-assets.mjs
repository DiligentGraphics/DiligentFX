#!/usr/bin/env node
import {cp, mkdir} from "node:fs/promises";
import {resolve} from "node:path";

const destination = process.argv[2];
if (!destination || process.argv.length !== 3) {
    console.error("Usage: radient-copy-assets <destination-directory>");
    process.exitCode = 1;
} else {
    await mkdir(resolve(destination), {recursive: true});
    await cp(new URL("./dist/", import.meta.url), resolve(destination), {recursive: true});
}
