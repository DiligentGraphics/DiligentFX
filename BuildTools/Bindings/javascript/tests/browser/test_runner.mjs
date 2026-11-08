// Suites register the same cases with node:test or this browser runner. Cleanup
// always runs, even after an assertion fails, and cleanup failures fail the case.
export async function runTests(register) {
    const cases = [];
    register((name, callback) => cases.push({name, callback}));
    if (cases.length === 0) throw new Error("The suite did not register any tests");
    const results = [];
    for (const {name, callback} of cases) {
        const cleanup = [];
        const errors = [];
        try {
            await callback({after(action) { cleanup.push(action); }});
        } catch (error) {
            errors.push(String(error?.stack ?? error));
        } finally {
            for (const action of cleanup.reverse()) {
                try { await action(); }
                catch (error) { errors.push(String(error?.stack ?? error)); }
            }
        }
        results.push({name, errors});
    }
    return results;
}
