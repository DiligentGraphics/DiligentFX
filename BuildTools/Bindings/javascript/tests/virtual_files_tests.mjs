export const moduleOptions = {files: {
    "/bindings/nested/世界.bin": new Uint8Array([1, 2, 255]),
    "/bindings/empty": new Uint8Array(),
}};

export function registerTests({test, assert, api, constants}) {
    test("module initialization preserves callback failures without a worker pool", async () => {
        const expected = new Error("Initialization callback failed");
        let error;
        try {
            await constants.default({onRuntimeInitialized() { throw expected; }});
        } catch (caught) {
            error = caught;
        }
        assert.equal(error, expected);
    });
    test("preloaded virtual files are readable by ordinary native file IO", () => {
        moduleOptions.files["/bindings/nested/世界.bin"].fill(0);
        assert.equal(api.SumFileBytes("/bindings/nested/世界.bin"), 258);
        assert.equal(api.SumFileBytes("/bindings/empty"), 0);
        assert.equal(api.SumFileBytes("/bindings/missing"), 0xffffffff);
    });
    test("invalid virtual file inputs fail before native initialization", async () => {
        for (const files of [{"relative": new Uint8Array()}, {"/../file": new Uint8Array()}, {"/file": [1, 2]}]) {
            let error;
            try { await constants.default({files}); } catch (caught) { error = caught; }
            assert.ok(error instanceof TypeError);
        }
    });
}
