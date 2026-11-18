// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;

// These are the same declarations as defaultarguments_tests.mjs. Java requires
// explicit arguments; native defaults must not prevent binding the full signature.
public final class DefaultArgumentsTest {
    public static void main(java.lang.String[] args) {
        run("DefaultArguments.ExplicitValuesDoNotEvaluateNativeDefaults", () -> {
            long before = NextDefaultValue();
            equal(17L, WithDefaults(10, true, 7));
            equal(993L, WithDefaults(1000, false, 7));
            equal(before + 1, NextDefaultValue());
        });
        run("DefaultArguments.DescriptorsPointersStringsAndOutputs", () -> {
            equal(0L, DefaultPointer(null));
            MarshalDesc desc = new MarshalDesc();
            equal(0xfedcba9876543210L, DefaultDescriptor(desc));
            desc.Size = 23;
            equal(23L, DefaultPointer(desc));
            equal(23L, DefaultDescriptor(desc));
            MarshalDesc copy = DefaultBorrowedDescriptor(desc);
            desc.Size = 99;
            equal(23L, copy.Size);
            check(Arrays.equals(new float[]{1, 2, 3}, DefaultArrayCount(3)));
            equal(0xfedcba9876543210L, DefaultOutput(true).Value);
            equal(7L, DefaultOutput(false).Value);
            equal(4L, DefaultStringLength("test"));
            throwsType(IllegalArgumentException.class, () -> DefaultStringLength(null));
            throwsType(IllegalArgumentException.class, () -> DefaultDescriptor(null));
        });
    }
}
