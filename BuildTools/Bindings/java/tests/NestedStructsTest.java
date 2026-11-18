// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

// Corresponds to javascript/tests/nested_struct_tests.mjs.
public final class NestedStructsTest {
    private static void values(MarshalNestedDesc desc, float... expected) {
        equal(12, expected.length);
        for (int i = 0; i < expected.length; ++i)
            equal(expected[i], ReadNestedValue(desc, i));
    }

    public static void main(java.lang.String[] args) {
        run("NestedStructs.EnclosingNativeDefaults", () -> {
            MarshalNestedDesc desc = new MarshalNestedDesc();
            values(desc, 0, 0, 0, 1, 1, 1, 4, 5, 6, 2, 3, 4);
            equal(17L, GetNestedTag(desc));
            equal("parent default", GetNestedName(desc));
            equal(0xffffffffL, GetNestedChecksum(desc));
            equal(3L, desc.Data.ByteCount);
            equal(2L, new MarshalNestedData().ByteCount);
            check(desc.Transform != desc.Alternate);
            check(desc.Transform.Position != desc.Alternate.Position);
        });
        run("NestedStructs.OverridesPreserveSiblings", () -> {
            MarshalNestedDesc desc = new MarshalNestedDesc();
            desc.Tag = 99;
            desc.Transform.Position.x = -7.5f;
            desc.Transform.Scale.y = 8;
            desc.Alternate.Position.z = 11;
            desc.Alternate.Scale.x = 12;
            values(desc, -7.5f, 0, 0, 1, 8, 1, 4, 5, 11, 12, 3, 4);
            equal(99L, GetNestedTag(desc));
            equal(-7.5f, desc.Transform.Position.x);
            values(new MarshalNestedDesc(), 0, 0, 0, 1, 1, 1, 4, 5, 6, 2, 3, 4);
            // Java descriptors are complete values, so assigning the same child
            // to two fields passes that child's values to both native fields.
            desc.Alternate = desc.Transform;
            values(desc, -7.5f, 0, 0, 1, 8, 1, -7.5f, 0, 0, 1, 8, 1);
        });
        run("NestedStructs.StringAndByteCounts", () -> {
            MarshalNestedDesc desc = new MarshalNestedDesc();
            desc.Data.Name = "Aé水😀".repeat(100);
            desc.Data.pBytes = new short[]{17, 128, 254};
            equal(desc.Data.Name, GetNestedName(desc));
            equal(1035L, GetNestedChecksum(desc));
            desc.Data.ByteCount = 2;
            equal(273L, GetNestedChecksum(desc));
            desc.Data.ByteCount = 0;
            desc.Data.pBytes = new short[0];
            equal(0L, GetNestedChecksum(desc));
            desc.Data.pBytes = null;
            desc.Data.ByteCount = 3;
            equal(0xffffffffL, GetNestedChecksum(desc));
            desc.Data.Name = null;
            equal(null, GetNestedName(desc));
            desc.Data.Name = "";
            equal("", GetNestedName(desc));
        });
        run("NestedStructs.ValidationPrecedesNativeCalls", () -> {
            long calls = GetNestedCallCount();
            MarshalNestedDesc desc = new MarshalNestedDesc();
            desc.Transform = null;
            throwsType(IllegalArgumentException.class, () -> GetNestedTag(desc));
            desc.Transform = new MarshalNestedTransform();
            desc.Transform.Position = null;
            throwsType(IllegalArgumentException.class, () -> GetNestedTag(desc));
            desc.Transform.Position = new MarshalNestedVector();
            desc.Data.pBytes = new short[2]; // Parent default count is three.
            throwsType(IllegalArgumentException.class, () -> GetNestedTag(desc));
            desc.Data.pBytes = new short[]{17, 128, 256};
            throwsType(IllegalArgumentException.class, () -> GetNestedTag(desc));
            desc.Data.pBytes = new short[]{17, 128, 254};
            desc.Data.Name = "allocated".repeat(100);
            throwsType(IllegalArgumentException.class, () -> ReadNestedValue(desc, -1));
            desc.Data.ByteCount = -1;
            throwsType(IllegalArgumentException.class, () -> GetNestedTag(desc));
            equal(calls, GetNestedCallCount());
            desc.Data.ByteCount = 3;
            equal(1035L, GetNestedChecksum(desc));
        });
    }
}
