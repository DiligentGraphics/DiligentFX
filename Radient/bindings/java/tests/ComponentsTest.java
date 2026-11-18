// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

// Mirrors components_tests.mjs, including every field, defaults, failures,
// revisions, and snapshot independence. Expected values come from shared fixtures.
public final class ComponentsTest {
    private static boolean has(IRadientScene scene, long entity, long type) {
        var result = scene.HasComponent(entity, type);
        ok(result.returnValue);
        return result.HasComponent;
    }

    public static void main(String[] args) {
        run("camera descriptors round-trip all fields and return independent snapshots", () -> {
            try (SceneFixture f = new SceneFixture()) {
                RadientEntityDesc entityInfo = new RadientEntityDesc();
                entityInfo.Name = "Camera";
                long entity = f.entity(entityInfo);
                RadientCameraComponent camera = Fixtures.camera();
                var before = f.scene.GetSceneRevisions();
                ok(f.writer.SetCamera(entity, camera));
                check(f.scene.GetSceneRevisions().Cameras > before.Cameras);
                check(has(f.scene, entity, RADIENT_COMPONENT_TYPE_CAMERA));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                var snapshot = f.scene.GetCamera(entity);
                ok(snapshot.returnValue);
                descriptorEqual(Fixtures.camera(), snapshot.Camera);
                camera.ClippingRange.y = Fixtures.number("camera.updatedFarClip");
                snapshot.Camera.ClippingRange.x = 4;
                var unchanged = f.scene.GetCamera(entity);
                ok(unchanged.returnValue);
                descriptorEqual(Fixtures.vector2("camera.ClippingRange"), unchanged.Camera.ClippingRange);
                ok(f.writer.SetCamera(entity, camera));
                var updated = f.scene.GetCamera(entity);
                ok(updated.returnValue);
                equal(Fixtures.number("camera.updatedFarClip"), updated.Camera.ClippingRange.y);
                equal(RADIENT_STATUS_NO_CHANGE, f.writer.SetCamera(entity, camera));
                // Preserve removal of the edited camera, in addition to defaults below.
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_CAMERA));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_CAMERA));
                equal(RADIENT_STATUS_NOT_FOUND, f.scene.GetCamera(entity).returnValue);
            }
        });

        run("light descriptors preserve nested color, booleans, and all scalar fields", () -> {
            try (SceneFixture f = new SceneFixture()) {
                RadientEntityDesc entityInfo = new RadientEntityDesc();
                entityInfo.Name = "Light";
                long entity = f.entity(entityInfo);
                RadientLightComponent light = Fixtures.light();
                var before = f.scene.GetSceneRevisions();
                ok(f.writer.SetLight(entity, light));
                check(f.scene.GetSceneRevisions().Lights > before.Lights);
                check(has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                var snapshot = f.scene.GetLight(entity);
                ok(snapshot.returnValue);
                descriptorEqual(Fixtures.light(), snapshot.Light);
                light.Color.x = 1;
                snapshot.Light.Color.y = 1;
                var unchanged = f.scene.GetLight(entity);
                ok(unchanged.returnValue);
                descriptorEqual(Fixtures.vector3("light.Color"), unchanged.Light.Color);
                ok(f.writer.SetLight(entity, light));
                var updated = f.scene.GetLight(entity);
                ok(updated.returnValue);
                equal(1.f, updated.Light.Color.x);
                equal(RADIENT_STATUS_NO_CHANGE, f.writer.SetLight(entity, light));
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_LIGHT));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                equal(RADIENT_STATUS_NOT_FOUND, f.scene.GetLight(entity).returnValue);
            }
        });

        run("default components, native validation, and removal keep their public contracts", () -> {
            try (SceneFixture f = new SceneFixture()) {
                long entity = f.entity(new RadientEntityDesc());
                RadientCameraComponent defaults = new RadientCameraComponent();
                equal(RADIENT_CAMERA_PROJECTION.RADIENT_CAMERA_PROJECTION_PERSPECTIVE, defaults.Projection);
                equal(Fixtures.number("camera.defaultFocalLength"), defaults.FocalLength);
                descriptorEqual(Fixtures.vector2("camera.defaultClippingRange"), defaults.ClippingRange);
                RadientLightComponent lightDefaults = new RadientLightComponent();
                equal(RADIENT_LIGHT_TYPE.RADIENT_LIGHT_TYPE_DIRECTIONAL, lightDefaults.Type);
                equal(Fixtures.number("light.defaultIntensity"), lightDefaults.Intensity);
                equal(false, lightDefaults.Normalize);
                equal(false, lightDefaults.EnableColorTemperature);
                descriptorEqual(Fixtures.vector3("light.defaultColor"), lightDefaults.Color);
                ok(f.writer.SetCamera(entity, defaults));
                ok(f.writer.SetLight(entity, lightDefaults));
                var camera = f.scene.GetCamera(entity);
                var light = f.scene.GetLight(entity);
                ok(camera.returnValue);
                ok(light.returnValue);
                descriptorEqual(defaults, camera.Camera);
                descriptorEqual(lightDefaults, light.Light);

                // One native rejection per component; exhaustive validation is native-tested.
                RadientCameraComponent invalidCamera = new RadientCameraComponent();
                invalidCamera.ClippingRange = Fixtures.vector2("camera.invalidClippingRange");
                RadientLightComponent invalidLight = new RadientLightComponent();
                invalidLight.Intensity = Fixtures.number("light.invalidIntensity");
                equal(RADIENT_STATUS_INVALID_ARGUMENT, f.writer.SetCamera(entity, invalidCamera));
                equal(RADIENT_STATUS_INVALID_ARGUMENT, f.writer.SetLight(entity, invalidLight));
                var unchangedCamera = f.scene.GetCamera(entity);
                var unchangedLight = f.scene.GetLight(entity);
                ok(unchangedCamera.returnValue);
                ok(unchangedLight.returnValue);
                descriptorEqual(camera.Camera, unchangedCamera.Camera);
                descriptorEqual(light.Light, unchangedLight.Light);
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_CAMERA));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_CAMERA));
                check(has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                var removedCamera = f.scene.GetCamera(entity);
                equal(RADIENT_STATUS_NOT_FOUND, removedCamera.returnValue);
                descriptorEqual(camera.Camera, removedCamera.Camera);
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_LIGHT));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                var removedLight = f.scene.GetLight(entity);
                equal(RADIENT_STATUS_NOT_FOUND, removedLight.returnValue);
                descriptorEqual(light.Light, removedLight.Light);
            }
        });
    }
}
