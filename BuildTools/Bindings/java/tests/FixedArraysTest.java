// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;

public final class FixedArraysTest {
    private static TestFixedArrays input() {
        TestFixedArrays desc = new TestFixedArrays();
        desc.Matrix[13] = 42.25f;
        desc.Rows[1][3] = -7.5f;
        desc.Cube[1][0][1] = 27;
        desc.Children[0].Id = 11;
        desc.Children[0].Label = "first";
        desc.Children[1].Id = 22;
        desc.Children[1].Label = "second";
        desc.Children[1].Samples = new double[]{-11.5, 9.75};
        desc.Grid[0][0].Label = "grid zero";
        desc.Grid[1][0].Label = "grid one";
        return desc;
    }

    public static void main(java.lang.String[] args) {
        run("FixedArrays.NativeDefaults", () -> {
            TestFixedArrays desc = new TestFixedArrays();
            equal(16, desc.Matrix.length);
            for (int i = 0; i < 16; ++i) equal(i % 5 == 0 ? 1.f : 0.f, desc.Matrix[i]);
            check(Arrays.equals(new byte[]{-128, 127}, desc.SignedBytes));
            check(Arrays.equals(new short[]{0, 255}, desc.Bytes));
            check(Arrays.equals(new short[]{Short.MIN_VALUE, Short.MAX_VALUE}, desc.Shorts));
            check(Arrays.equals(new int[]{0, 65535}, desc.UnsignedShorts));
            check(Arrays.equals(new int[]{Integer.MIN_VALUE, Integer.MAX_VALUE}, desc.Ints));
            check(Arrays.equals(new long[]{0, 0xffffffffL}, desc.UnsignedInts));
            check(Arrays.equals(new long[]{Long.MIN_VALUE, Long.MAX_VALUE}, desc.Longs));
            check(Arrays.equals(new long[]{0, 0xfedcba9876543210L}, desc.WideValues));
            check(Arrays.equals(new boolean[]{true, false}, desc.Flags));
            check(Arrays.equals(new short[]{0, 241}, desc.Modes));
            check(Arrays.deepEquals(new float[][]{{1, 2, 3, 4}, {5, 6, 7, 8}}, desc.Rows));
            equal(8, desc.Cube[1][1][1]);
            equal(null, desc.Children[0].Label);
            check(Arrays.equals(new double[]{1.25, -2.5}, desc.Grid[1][0].Samples));
            check(desc.Children[0] != desc.Children[1]);
            TestFixedArrays another = new TestFixedArrays();
            desc.Matrix[0] = 19;
            equal(1.f, another.Matrix[0]);
        });
        run("FixedArrays.NativeInputValidation", () -> check(CheckFixedInputs(input())));
        run("FixedArrays.IndependentOutputSnapshots", () -> {
            TestFixedArrays source = input();
            TestFixedArrays copy = CopyFixedArrays(source).Result;
            check(CheckFixedInputs(copy));
            check(CheckFixedInputs(EchoFixedArrays(source)));
            check(source.Matrix != copy.Matrix);
            check(source.Rows[1] != copy.Rows[1]);
            check(source.Children[1] != copy.Children[1]);
            check(source.Grid[1][0].Samples != copy.Grid[1][0].Samples);
            source.Matrix[13] = 0;
            source.Children[1].Label = "changed";
            source.Children[1].Samples[0] = 0;
            source.Grid[1][0].Label = "changed";
            check(CheckFixedInputs(copy));
            // Heap-backed strings inside descriptor arrays must survive all
            // element conversions and the native call.
            source.Children[0].Label = "first ".repeat(64) + "\uD83D\uDE00";
            source.Grid[1][0].Label = "grid ".repeat(64);
            TestFixedArrays longNames = EchoFixedArrays(source);
            equal(source.Children[0].Label, longNames.Children[0].Label);
            equal(source.Grid[1][0].Label, longNames.Grid[1][0].Label);
            throwsType(IllegalArgumentException.class, BindingTests::GetIncompleteFixedArrays);
        });
        run("FixedArrays.ExactLengthsBeforeNativeCall", () -> {
            long calls = GetFixedCallCount();
            for (int length : new int[]{0, 15, 17}) {
                TestFixedArrays desc = input();
                desc.Matrix = new float[length];
                throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            }
            TestFixedArrays desc = input();
            desc.Matrix = null;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Matrix = new float[16];
            desc.Rows[1] = new float[3];
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Rows[1] = null;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Rows = new float[1][4];
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            equal(calls, GetFixedCallCount());
        });
        run("FixedArrays.ElementValidationBeforeNativeCall", () -> {
            long calls = GetFixedCallCount();
            TestFixedArrays desc = input();
            desc.Bytes[0] = 256;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Bytes[0] = -1;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Bytes[0] = 0;
            desc.UnsignedShorts[0] = 65536;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.UnsignedShorts[0] = 0;
            desc.UnsignedInts[0] = 0x100000000L;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.UnsignedInts[0] = 0;
            desc.Modes[0] = 256;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Modes[0] = 0;
            desc.Children[1].Label = null;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Children[1] = null;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            desc.Children = new TestFixedChild[0];
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(desc));
            TestFixedArrays nested = input();
            nested.Grid[1][0].Samples = null;
            throwsType(IllegalArgumentException.class, () -> CheckFixedInputs(nested));
            equal(calls, GetFixedCallCount());
        });
    }
}
