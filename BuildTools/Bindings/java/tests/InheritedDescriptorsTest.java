// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;

// Corresponds to javascript/tests/inheriteddescriptors_tests.mjs.
public final class InheritedDescriptorsTest {
    public static void main(java.lang.String[] args) {
        run("InheritedDescriptors.DefaultsAcrossTwoBaseClasses", () -> {
            MarshalLeafDesc input = new MarshalLeafDesc();
            MarshalDerivedDesc derived = input;
            MarshalBaseDesc base = derived;
            equal(0xfedcba9876543210L, base.Tag);
            equal(0L, base.Count);
            equal(2.5f, derived.Scale);
            equal(true, input.Enabled);
            MarshalLeafDesc copy = EchoInherited(input);
            equal(base.Tag, copy.Tag);
            equal(0L, copy.Count);
            equal(derived.Scale, copy.Scale);
            equal(input.Enabled, copy.Enabled);
            equal(null, copy.pValues);
        });
        run("InheritedDescriptors.CountFieldsDriveDerivedArrays", () -> {
            MarshalLeafDesc input = new MarshalLeafDesc();
            input.Tag = 11;
            input.Scale = 7;
            input.Enabled = false;
            input.Count = 2;
            input.pValues = new long[]{2, 3};
            equal(16L, SumInherited(input));
            MarshalLeafDesc copy = EchoInherited(input);
            equal(11L, copy.Tag);
            equal(2L, copy.Count);
            equal(7.f, copy.Scale);
            equal(false, copy.Enabled);
            check(Arrays.equals(new long[]{2, 3}, copy.pValues));
            input.pValues[0] = 100;
            equal(2L, copy.pValues[0]);
            input.Count = 3;
            throwsType(IllegalArgumentException.class, () -> SumInherited(input));
            input.Count = 0;
            input.pValues = new long[0];
            equal(0, EchoInherited(input).pValues.length);
        });
    }
}
