// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.io.IOException;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;
import static diligent.radient.Radient.*;

// Data is shared with the Node/browser suites; each array or descriptor is fresh
// so snapshot/input mutation tests cannot modify another case's expectations.
final class Fixtures {
    private static final Properties DATA = load();
    private Fixtures() {}

    private static Properties load() {
        String path = System.getProperty("radient.test.fixtures");
        if (path == null) throw new AssertionError("Shared integration fixtures were not supplied");
        try (Reader reader = Files.newBufferedReader(Path.of(path), StandardCharsets.UTF_8)) {
            Properties data = new Properties();
            data.load(reader);
            return data;
        } catch (IOException error) {
            throw new AssertionError("Cannot load integration fixtures: " + path, error);
        }
    }

    static String string(String key) {
        String value = DATA.getProperty(key);
        if (value == null) throw new AssertionError("Missing fixture: " + key);
        return value;
    }

    static float number(String key) { return Float.parseFloat(string(key)); }
    static long integer(String key) { return Long.parseLong(string(key)); }
    static boolean bool(String key) {
        String value = string(key);
        if (!value.equals("true") && !value.equals("false"))
            throw new AssertionError("Invalid boolean: " + key);
        return Boolean.parseBoolean(value);
    }

    static float[] floats(String key) {
        String[] values = string(key).split(",");
        float[] result = new float[values.length];
        for (int i = 0; i < values.length; ++i) result[i] = Float.parseFloat(values[i]);
        return result;
    }

    static int[] ints(String key) {
        String[] values = string(key).split(",");
        int[] result = new int[values.length];
        for (int i = 0; i < values.length; ++i) result[i] = Integer.parseInt(values[i]);
        return result;
    }

    static Path assetPath(String name) {
        return Path.of(System.getProperty("radient.test.fixtures")).toAbsolutePath()
            .resolveSibling("assets").resolve(name);
    }

    static byte[] assetBytes(String name) {
        try { return Files.readAllBytes(assetPath(name)); }
        catch (IOException error) { throw new AssertionError("Cannot read shared asset: " + name, error); }
    }

    static byte[] bytes(String key) {
        String[] values = string(key).split(",");
        byte[] result = new byte[values.length];
        for (int i = 0; i < values.length; ++i) {
            int value = Integer.parseInt(values[i]);
            if (value < 0 || value > 255) throw new AssertionError("Invalid byte: " + key);
            result[i] = (byte)value;
        }
        return result;
    }

    static RadientFloat4 vector4(String key) {
        float[] values = floats(key);
        TestSupport.equal(4, values.length);
        RadientFloat4 result = new RadientFloat4();
        result.x = values[0]; result.y = values[1]; result.z = values[2]; result.w = values[3];
        return result;
    }

    static RadientFloat3 vector3(String key) {
        float[] values = floats(key);
        TestSupport.equal(3, values.length);
        RadientFloat3 result = new RadientFloat3();
        result.x = values[0]; result.y = values[1]; result.z = values[2];
        return result;
    }

    static RadientFloat2 vector2(String key) {
        float[] values = floats(key);
        TestSupport.equal(2, values.length);
        RadientFloat2 result = new RadientFloat2();
        result.x = values[0]; result.y = values[1];
        return result;
    }

    static RadientCameraComponent camera() {
        RadientCameraComponent result = new RadientCameraComponent();
        result.Projection = RADIENT_CAMERA_PROJECTION.RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
        result.HorizontalAperture = number("camera.HorizontalAperture");
        result.VerticalAperture = number("camera.VerticalAperture");
        result.HorizontalApertureOffset = number("camera.HorizontalApertureOffset");
        result.VerticalApertureOffset = number("camera.VerticalApertureOffset");
        result.FocalLength = number("camera.FocalLength");
        result.ClippingRange = vector2("camera.ClippingRange");
        result.FStop = number("camera.FStop");
        result.FocusDistance = number("camera.FocusDistance");
        return result;
    }

    static RadientLightComponent light() {
        RadientLightComponent result = new RadientLightComponent();
        result.Type = RADIENT_LIGHT_TYPE.RADIENT_LIGHT_TYPE_SPOT;
        result.Color = vector3("light.Color");
        result.Intensity = number("light.Intensity");
        result.Range = number("light.Range");
        result.Exposure = number("light.Exposure");
        result.Diffuse = number("light.Diffuse");
        result.Specular = number("light.Specular");
        result.Normalize = bool("light.Normalize");
        result.EnableColorTemperature = bool("light.EnableColorTemperature");
        result.ColorTemperature = number("light.ColorTemperature");
        result.Radius = number("light.Radius");
        result.Angle = number("light.Angle");
        result.InnerConeAngle = number("light.InnerConeAngle");
        result.OuterConeAngle = number("light.OuterConeAngle");
        result.ShapingFocus = number("light.ShapingFocus");
        return result;
    }
}
