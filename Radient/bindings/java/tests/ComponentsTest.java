// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class ComponentsTest {
    private static boolean has(IRadientScene scene, long entity, long type) {
        var result = scene.HasComponent(entity, type);
        ok(result.returnValue);
        return result.HasComponent;
    }

    public static void main(String[] args) {
        run("Components.CameraDefaultsUpdatesAndSnapshots", () -> {
            try (SceneFixture f = new SceneFixture()) {
                long entity = f.entity(new RadientEntityDesc());
                RadientCameraComponent camera = new RadientCameraComponent();
                equal(RADIENT_CAMERA_PROJECTION.RADIENT_CAMERA_PROJECTION_PERSPECTIVE, camera.Projection);
                equal(50.f, camera.FocalLength);
                equal(0.1f, camera.ClippingRange.x);
                camera.Projection = RADIENT_CAMERA_PROJECTION.RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
                camera.VerticalAperture = 18;
                camera.HorizontalAperture = 24;
                camera.ClippingRange.x = 0.5f;
                camera.ClippingRange.y = 256;
                var before = f.scene.GetSceneRevisions();
                ok(f.writer.SetCamera(entity, camera));
                check(f.scene.GetSceneRevisions().Cameras > before.Cameras);
                check(has(f.scene, entity, RADIENT_COMPONENT_TYPE_CAMERA));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                var snapshot = f.scene.GetCamera(entity);
                ok(snapshot.returnValue);
                equal(camera.Projection, snapshot.Camera.Projection);
                equal(18.f, snapshot.Camera.VerticalAperture);
                equal(24.f, snapshot.Camera.HorizontalAperture);
                snapshot.Camera.ClippingRange.x = 4;
                camera.ClippingRange.y = 512;
                var unchanged = f.scene.GetCamera(entity);
                ok(unchanged.returnValue);
                equal(0.5f, unchanged.Camera.ClippingRange.x);
                equal(256.f, unchanged.Camera.ClippingRange.y);
                ok(f.writer.SetCamera(entity, camera));
                equal(RADIENT_STATUS_NO_CHANGE, f.writer.SetCamera(entity, camera));
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_CAMERA));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_CAMERA));
                equal(RADIENT_STATUS_NOT_FOUND, f.scene.GetCamera(entity).returnValue);
            }
        });

        run("Components.LightNestedValuesAndBooleanFields", () -> {
            try (SceneFixture f = new SceneFixture()) {
                long entity = f.entity(new RadientEntityDesc());
                RadientLightComponent light = new RadientLightComponent();
                equal(RADIENT_LIGHT_TYPE.RADIENT_LIGHT_TYPE_DIRECTIONAL, light.Type);
                equal(1.f, light.Intensity);
                equal(false, light.EnableColorTemperature);
                light.Type = RADIENT_LIGHT_TYPE.RADIENT_LIGHT_TYPE_SPOT;
                light.Color.x = 0.25f;
                light.Color.y = 0.5f;
                light.Color.z = 0.75f;
                light.Intensity = 8;
                light.Range = 32;
                light.Normalize = true;
                light.EnableColorTemperature = true;
                light.ColorTemperature = 5000;
                light.InnerConeAngle = 0.25f;
                light.OuterConeAngle = 1;
                var before = f.scene.GetSceneRevisions();
                ok(f.writer.SetLight(entity, light));
                check(f.scene.GetSceneRevisions().Lights > before.Lights);
                check(has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                var snapshot = f.scene.GetLight(entity);
                ok(snapshot.returnValue);
                equal(light.Type, snapshot.Light.Type);
                equal(8.f, snapshot.Light.Intensity);
                equal(32.f, snapshot.Light.Range);
                equal(true, snapshot.Light.Normalize);
                equal(true, snapshot.Light.EnableColorTemperature);
                equal(5000.f, snapshot.Light.ColorTemperature);
                light.Color.x = 1;
                snapshot.Light.Color.y = 1;
                var unchanged = f.scene.GetLight(entity);
                ok(unchanged.returnValue);
                equal(0.25f, unchanged.Light.Color.x);
                equal(0.5f, unchanged.Light.Color.y);
                equal(0.75f, unchanged.Light.Color.z);
                ok(f.writer.RemoveComponent(entity, RADIENT_COMPONENT_TYPE_LIGHT));
                check(!has(f.scene, entity, RADIENT_COMPONENT_TYPE_LIGHT));
                equal(RADIENT_STATUS_NOT_FOUND, f.scene.GetLight(entity).returnValue);
            }
        });
    }
}
