// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.lang.reflect.Modifier;
import diligent.radient.Radient;
import static diligent.radient.Radient.*;
import static diligent.radient.tests.TestSupport.*;

public final class ExportsTest {
    private static void constant(String name, long expected) {
        try {
            var field = Radient.class.getField(name);
            // Check the declared type too: assigning an int to a Java long would
            // otherwise hide a regression in the generated integer mapping.
            equal(long.class, field.getType());
            check(Modifier.isStatic(field.getModifiers()) && Modifier.isFinal(field.getModifiers()));
            equal(expected, field.getLong(null));
        } catch (ReflectiveOperationException error) {
            throw new AssertionError("Missing or inaccessible constant: " + name, error);
        }
    }

    private static INTERFACE_ID engineID() {
        INTERFACE_ID expected = new INTERFACE_ID();
        expected.Data1 = Fixtures.integer("exports.engineID.Data1");
        expected.Data2 = (int)Fixtures.integer("exports.engineID.Data2");
        expected.Data3 = (int)Fixtures.integer("exports.engineID.Data3");
        int[] bytes = Fixtures.ints("exports.engineID.Data4");
        equal(8, bytes.length);
        expected.Data4 = new short[bytes.length];
        for (int i = 0; i < bytes.length; ++i) expected.Data4[i] = (short)bytes[i];
        return expected;
    }

    public static void main(String[] args) {
        run("standalone invalid IDs preserve their native 64-bit representation", () -> {
            for (String name : new String[]{"InvalidRadientHandle", "InvalidRadientComponentTypeID", "InvalidRadientFrameID"})
                constant(name, Fixtures.integer("exports.invalidID"));
        });

        run("automatically discovered interface IDs preserve native fields", () -> {
            var expected = engineID();
            var id = IID_RadientEngine();
            descriptorEqual(expected, id);
            // Java returns independent mutable snapshots; JS exports a frozen
            // descriptor whose array accessor returns a copy.
            var copy = IID_RadientEngine();
            check(id != copy && id.Data4 != copy.Data4);
            id.Data1 = 0;
            id.Data2 = 0;
            id.Data3 = 0;
            id.Data4[0] = 0;
            descriptorEqual(expected, copy);
            descriptorEqual(expected, IID_RadientEngine());
        });

        run("the active morph-target limit is a numeric named export and module member", () -> {
            constant("RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT", Fixtures.integer("exports.maxActiveMorphTargets"));
        });
    }
}
