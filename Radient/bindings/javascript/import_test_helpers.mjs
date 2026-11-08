// Build a small real GLB containing one triangle and a translation animation.
// Keeping the binary layout here makes the imported bytes independently readable.
export function makeAnimatedTriangleGLB() {
    const binary = new Float32Array([
        0, 0, 0, 1, 0, 0, 0, 1, 0, // POSITION, 36 bytes
        0, 1,                     // animation times, 8 bytes
        0, 0, 0, 2, 0, 0,        // translation keys, 24 bytes
    ]);
    const document = {
        asset: {version: "2.0"}, scene: 0, scenes: [{nodes: [0]}],
        nodes: [{name: "Animated triangle 世界", mesh: 0}],
        meshes: [{primitives: [{attributes: {POSITION: 0}, mode: 4}]}],
        buffers: [{byteLength: binary.byteLength}],
        bufferViews: [
            {buffer: 0, byteOffset: 0, byteLength: 36},
            {buffer: 0, byteOffset: 36, byteLength: 8},
            {buffer: 0, byteOffset: 44, byteLength: 24},
        ],
        accessors: [
            {bufferView: 0, componentType: 5126, count: 3, type: "VEC3", min: [0, 0, 0], max: [1, 1, 0]},
            {bufferView: 1, componentType: 5126, count: 2, type: "SCALAR", min: [0], max: [1]},
            {bufferView: 2, componentType: 5126, count: 2, type: "VEC3"},
        ],
        animations: [{name: "Move", samplers: [{input: 1, output: 2, interpolation: "LINEAR"}],
            channels: [{sampler: 0, target: {node: 0, path: "translation"}}]}],
    };
    const json = new TextEncoder().encode(JSON.stringify(document));
    const jsonLength = Math.ceil(json.length / 4) * 4;
    const bytes = new Uint8Array(12 + 8 + jsonLength + 8 + binary.byteLength);
    const words = new DataView(bytes.buffer);
    words.setUint32(0, 0x46546c67, true);
    words.setUint32(4, 2, true);
    words.setUint32(8, bytes.length, true);
    words.setUint32(12, jsonLength, true);
    words.setUint32(16, 0x4e4f534a, true);
    bytes.fill(0x20, 20, 20 + jsonLength);
    bytes.set(json, 20);
    words.setUint32(20 + jsonLength, binary.byteLength, true);
    words.setUint32(24 + jsonLength, 0x004e4942, true);
    bytes.set(new Uint8Array(binary.buffer), 28 + jsonLength);
    return bytes;
}

export function sceneEntities(f, root) {
    const result = [root];
    for (let i = 0; i < result.length; ++i) {
        const count = f.value(f.scene.GetChildCount(result[i]));
        result.push(...f.value(f.scene.GetChildren(result[i], 0, count)).pChildren);
    }
    return result;
}
