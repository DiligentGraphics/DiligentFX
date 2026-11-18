import {readFixtures} from "./fixtures.mjs";
import {createSceneFixture} from "./scene_test_helpers.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const data = readFixtures(testData);
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
            HorizontalAperture: data.number("camera.HorizontalAperture"),
            VerticalAperture: data.number("camera.VerticalAperture"),
            HorizontalApertureOffset: data.number("camera.HorizontalApertureOffset"),
            VerticalApertureOffset: data.number("camera.VerticalApertureOffset"),
            FocalLength: data.number("camera.FocalLength"),
            FStop: data.number("camera.FStop"),
            FocusDistance: data.number("camera.FocusDistance"),
            ClippingRange: data.vector("camera.ClippingRange"),
        };
        const revisions = scene.GetSceneRevisions();
        assert.equal(writer.SetCamera(entity, camera), OK);
        assert.ok(scene.GetSceneRevisions().Cameras > revisions.Cameras);
        assert.equal(typeof CAMERA, "bigint");
        assert.equal(value(scene.HasComponent(entity, CAMERA)), true);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), false);
        assert.deepEqual(value(scene.GetCamera(entity)), camera);
        const snapshot = value(scene.GetCamera(entity));
        camera.ClippingRange.y = data.number("camera.updatedFarClip");
        snapshot.ClippingRange.x = 4;
        assert.deepEqual(value(scene.GetCamera(entity)).ClippingRange, data.vector("camera.ClippingRange"));
        assert.equal(writer.SetCamera(entity, camera), OK);
        assert.equal(value(scene.GetCamera(entity)).ClippingRange.y, data.number("camera.updatedFarClip"));
        assert.equal(writer.SetCamera(entity, camera), NO_CHANGE);
        assert.equal(writer.RemoveComponent(entity, CAMERA), OK);
        assert.equal(value(scene.HasComponent(entity, CAMERA)), false);
        assert.equal(scene.GetCamera(entity).status, NOT_FOUND);
    });

    test("light descriptors preserve nested color, booleans, and all scalar fields", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const entity = value(writer.CreateEntity({Name: "Light"}));
        const light = {
            Type: SPOT, Color: data.vector("light.Color"),
            Intensity: data.number("light.Intensity"),
            Range: data.number("light.Range"),
            Exposure: data.number("light.Exposure"),
            Diffuse: data.number("light.Diffuse"),
            Specular: data.number("light.Specular"),
            ColorTemperature: data.number("light.ColorTemperature"),
            Radius: data.number("light.Radius"),
            Angle: data.number("light.Angle"),
            InnerConeAngle: data.number("light.InnerConeAngle"),
            OuterConeAngle: data.number("light.OuterConeAngle"),
            ShapingFocus: data.number("light.ShapingFocus"),
            Normalize: data.boolean("light.Normalize"),
            EnableColorTemperature: data.boolean("light.EnableColorTemperature"),
        };
        const revisions = scene.GetSceneRevisions();
        assert.equal(writer.SetLight(entity, light), OK);
        assert.ok(scene.GetSceneRevisions().Lights > revisions.Lights);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), true);
        assert.deepEqual(value(scene.GetLight(entity)), light);
        const snapshot = value(scene.GetLight(entity));
        light.Color.x = 1;
        snapshot.Color.y = 1;
        assert.deepEqual(value(scene.GetLight(entity)).Color, data.vector("light.Color"));
        assert.equal(writer.SetLight(entity, light), OK);
        assert.equal(value(scene.GetLight(entity)).Color.x, 1);
        assert.equal(writer.SetLight(entity, light), NO_CHANGE);
        assert.equal(writer.RemoveComponent(entity, LIGHT), OK);
        assert.equal(value(scene.HasComponent(entity, LIGHT)), false);
        assert.equal(scene.GetLight(entity).status, NOT_FOUND);
    });

    test("default components, native validation, and removal keep their public contracts", t => {
        const {scene, writer, value} = createSceneFixture(t, api, assert, OK);
        const entity = value(writer.CreateEntity({}));
        assert.equal(writer.SetCamera(entity, {}), OK);
        assert.equal(writer.SetLight(entity, {}), OK);
        const camera = value(scene.GetCamera(entity));
        const light = value(scene.GetLight(entity));
        assert.equal(camera.Projection, PERSPECTIVE);
        assert.equal(camera.FocalLength, data.number("camera.defaultFocalLength"));
        assert.deepEqual(camera.ClippingRange, {x: Math.fround(data.vector("camera.defaultClippingRange").x), y: data.vector("camera.defaultClippingRange").y});
        assert.equal(light.Type, DIRECTIONAL);
        assert.deepEqual(light.Color, data.vector("light.defaultColor"));
        assert.equal(light.Intensity, data.number("light.defaultIntensity"));
        assert.equal(light.Normalize, false);
        assert.equal(light.EnableColorTemperature, false);
        // One native validation case per component checks status propagation;
        // exhaustive range/finite-value validation belongs to the native tests.
        assert.equal(writer.SetCamera(entity, {ClippingRange: data.vector("camera.invalidClippingRange")}), INVALID_ARGUMENT);
        assert.equal(writer.SetLight(entity, {Intensity: data.number("light.invalidIntensity")}), INVALID_ARGUMENT);
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
