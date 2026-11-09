// The native and browser suites use the same manifest and capture names.
export function selectCases(manifest, filter = "*") {
    if (manifest.version !== 1 || !Array.isArray(manifest.tests)) throw new Error("Unsupported render manifest");
    const expression = new RegExp("^" + filter.split("*").map(part => part.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join(".*") + "$");
    const selected = manifest.tests.filter(test => expression.test(test.name));
    if (!selected.length) throw new Error(`No render cases match ${filter}`);
    return selected;
}

export function captures(test) {
    const times = test.animation?.captures ?? [{time: test.animation?.time ?? 0}];
    return times.flatMap((capture, index) => [null, ...(test.debugVisualizations ?? [])].map(visualization => ({
        ...capture, index, visualization,
        name: [test.name, capture.name, visualization, "wgpu"].filter(Boolean).join("_"),
    })));
}

export function cameraTransform(camera) {
    const vector = ([x, y, z]) => ({x, y, z});
    const normalize = v => { const length = Math.hypot(...v); return v.map(x => x / length); };
    const cross = (a, b) => [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];
    const target = camera.target ?? [0, 0, 0];
    const back = normalize(camera.eye.map((x, i) => x - target[i]));
    const right = normalize(cross(camera.up ?? [0, 1, 0], back));
    const up = cross(back, right);
    // Columns are the camera's local axes in world space. Convert the basis to
    // a quaternion, including rotations whose matrix trace is non-positive.
    const m = [[right[0], up[0], back[0]], [right[1], up[1], back[1]], [right[2], up[2], back[2]]];
    const trace = m[0][0] + m[1][1] + m[2][2];
    const q = [0, 0, 0, 0];
    if (trace > 0) {
        const s = Math.sqrt(trace + 1) * 2;
        q[3] = s / 4;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else {
        const i = m[0][0] > m[1][1] ? (m[0][0] > m[2][2] ? 0 : 2) : (m[1][1] > m[2][2] ? 1 : 2);
        const j = (i + 1) % 3, k = (i + 2) % 3;
        const s = Math.sqrt(1 + m[i][i] - m[j][j] - m[k][k]) * 2;
        q[i] = s / 4;
        q[j] = (m[j][i] + m[i][j]) / s;
        q[k] = (m[k][i] + m[i][k]) / s;
        q[3] = (m[k][j] - m[j][k]) / s;
    }
    return {Position: vector(camera.eye), Rotation: {x: q[0], y: q[1], z: q[2], w: q[3]}};
}
