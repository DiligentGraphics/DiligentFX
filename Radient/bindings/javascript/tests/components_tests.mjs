import {createSceneFixture} from "./scene_test_helpers.mjs";

export function registerTests({test, assert, api, constants}) {
    const {
        RADIENT_STATUS_OK: OK,
        RADIENT_STATUS_NO_CHANGE: NO_CHANGE,
        RADIENT_STATUS_NOT_FOUND: NOT_FOUND,
        RADIENT_STATUS_INVALID_ARGUMENT: INVALID_ARGUMENT,
    } = constants.RADIENT_STATUS;
    const {RADIENT_COMPONENT_TYPE_CAMERA: CAMERA, RADIENT_COMPONENT_TYPE_LIGHT: LIGHT} = constants;
    const {RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC: ORTHOGRAPHIC,
           RADIENT_CAMERA_PROJECTION_PERSPECTIVE: PERSPECTIVE} = constants.RADIENT_CAMERA_PROJECTION;
    const {RADIENT_LIGHT_TYPE_SPOT: SPOT, RADIENT_LIGHT_TYPE_DIRECTIONAL: DIRECTIONAL} = constants.RADIENT_LIGHT_TYPE;

    test("camera descriptors round-trip all fields and return independent snapshots", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const entity = value(writer.CreateEntity({Name: "Camera"}));
        const camera = {
            Projection: ORTHOGRAPHIC,
            HorizontalAperture: 24, VerticalAperture: 18,
            HorizontalApertureOffset: -0.5, VerticalApertureOffset: 0.25,
            FocalLength: 35, ClippingRange: {x: 0.5, y: 256}, FStop: 2, FocusDistance: 16,
        };
        const revisions = scene.GetSceneRevisions();
        assert.equal(writer.SetCamera(entity, camera), OK);
        assert.ok(scene.GetSceneRevisions().Cameras > revisions.Cameras);
        assert.equal(typeof CAMERA, "bigint");
        assert.equal(value(scene.HasComponent(entity, CAMERA)), true);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), false);
        assert.deepEqual(value(scene.GetCamera(entity)), camera);
        const snapshot = value(scene.GetCamera(entity));
        camera.ClippingRange.y = 512;
        snapshot.ClippingRange.x = 4;
        assert.deepEqual(value(scene.GetCamera(entity)).ClippingRange, {x: 0.5, y: 256});
        assert.equal(writer.SetCamera(entity, camera), OK);
        assert.equal(value(scene.GetCamera(entity)).ClippingRange.y, 512);
        assert.equal(writer.SetCamera(entity, camera), NO_CHANGE);
    });

    test("light descriptors preserve nested color, booleans, and all scalar fields", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const entity = value(writer.CreateEntity({Name: "Light"}));
        const light = {
            Type: SPOT, Color: {x: 0.25, y: 0.5, z: 0.75},
            Intensity: 8, Range: 32, Exposure: -2, Diffuse: 0.5, Specular: 0.25,
            Normalize: true, EnableColorTemperature: true, ColorTemperature: 5000,
            Radius: 0.25, Angle: 0.5, InnerConeAngle: 0.25, OuterConeAngle: 1, ShapingFocus: 2,
        };
        const revisions = scene.GetSceneRevisions();
        assert.equal(writer.SetLight(entity, light), OK);
        assert.ok(scene.GetSceneRevisions().Lights > revisions.Lights);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), true);
        assert.deepEqual(value(scene.GetLight(entity)), light);
        const snapshot = value(scene.GetLight(entity));
        light.Color.x = 1;
        snapshot.Color.y = 1;
        assert.deepEqual(value(scene.GetLight(entity)).Color, {x: 0.25, y: 0.5, z: 0.75});
        assert.equal(writer.SetLight(entity, light), OK);
        assert.equal(value(scene.GetLight(entity)).Color.x, 1);
        assert.equal(writer.SetLight(entity, light), NO_CHANGE);
    });

    test("default components, native validation, and removal keep their public contracts", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const entity = value(writer.CreateEntity({}));
        assert.equal(writer.SetCamera(entity, {}), OK);
        assert.equal(writer.SetLight(entity, {}), OK);
        const camera = value(scene.GetCamera(entity));
        const light = value(scene.GetLight(entity));
        assert.equal(camera.Projection, PERSPECTIVE);
        assert.equal(camera.FocalLength, 50);
        assert.deepEqual(camera.ClippingRange, {x: Math.fround(0.1), y: 1000});
        assert.equal(light.Type, DIRECTIONAL);
        assert.deepEqual(light.Color, {x: 1, y: 1, z: 1});
        assert.equal(light.Intensity, 1);
        assert.equal(light.Normalize, false);
        assert.equal(light.EnableColorTemperature, false);
        // One native validation case per component checks status propagation;
        // exhaustive range/finite-value validation belongs to the native tests.
        assert.equal(writer.SetCamera(entity, {ClippingRange: {x: 10, y: 1}}), INVALID_ARGUMENT);
        assert.equal(writer.SetLight(entity, {Intensity: -1}), INVALID_ARGUMENT);
        assert.deepEqual(value(scene.GetCamera(entity)), camera);
        assert.deepEqual(value(scene.GetLight(entity)), light);
        assert.equal(writer.RemoveComponent(entity, CAMERA), OK);
        assert.equal(value(scene.HasComponent(entity, CAMERA)), false);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), true);
        assert.deepEqual(scene.GetCamera(entity), {status: NOT_FOUND, value: camera});
        assert.equal(writer.RemoveComponent(entity, LIGHT), OK);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), false);
        assert.deepEqual(scene.GetLight(entity), {status: NOT_FOUND, value: light});
    });
}
