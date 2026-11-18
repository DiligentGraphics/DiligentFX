import {readFixtures} from "./fixtures.mjs";
import {createSceneFixture} from "./scene_test_helpers.mjs";

export function createAssetFixture(t, api, assert, constants, testData) {
    const data = readFixtures(testData);
    const {RADIENT_STATUS_OK: OK, RADIENT_STATUS_PENDING: PENDING} = constants.RADIENT_STATUS;
    const fixture = createSceneFixture(t, api, assert, OK);
    const owned = [];
    t.after(() => { for (const object of owned.reverse()) object?.dispose(); });
    const keep = object => { owned.push(object); return object; };
    const manager = keep(fixture.value(fixture.engine.GetAssetManager()));
    function blob(values) {
        const bytes = new Uint8Array(values.buffer, values.byteOffset, values.byteLength);
        return keep(fixture.value(api.CreateRadientDataBlob({Size: BigInt(bytes.length), pData: bytes})));
    }
    function asset(result) {
        assert.ok(result.status === OK || result.status === PENDING, `Asset creation status: ${result.status}`);
        assert.ok(result.value !== null);
        return keep(result.value);
    }
    async function ready(object) {
        const deadline = Date.now() + 15000;
        let status;
        while ((status = api.GetRadientAssetLoadStatus(object)) === PENDING && Date.now() < deadline) {
            await new Promise(resolve => setTimeout(resolve, 1));
        }
        assert.equal(status, OK);
        return object;
    }
    function triangle(extra = {}) {
        const positions = blob(new Float32Array(data.numbers("mesh.positions")));
        return {
            Name: data.string("mesh.name"),
            VertexData: {
                VertexCount: 3,
                ppVertexBuffers: [positions],
                VertexLayout: {
                    pAttributes: [{Semantic: "POSITION", ComponentType: constants.RADIENT_VERTEX_COMPONENT_TYPE.RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, ComponentCount: 3}],
                    AttributeCount: 1, pBuffers: [{}], BufferCount: 1,
                },
            },
            pPrimitives: [{Name: data.string("mesh.primitiveName"), ElementCount: 3}], PrimitiveCount: 1,
            ...extra,
        };
    }
    function morphTarget(deltas, offset) {
        return {
            Desc: {AttributeCount: 1, pAttributes: [{Semantic: constants.RadientMorphTargetPositionSemantic, ComponentCount: 3}]},
            pAttributeData: [{pDeltaBuffer: deltas, ByteOffset: offset}],
        };
    }
    return {...fixture, data, manager, keep, blob, asset, ready, triangle, morphTarget};
}
