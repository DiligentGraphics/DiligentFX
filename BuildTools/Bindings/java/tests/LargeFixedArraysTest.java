// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;

// Run with the default JVM stack: increasing -Xss would hide this regression.
public final class LargeFixedArraysTest {
    public static void main(java.lang.String[] args) {
        run("LargeFixedArrays.Defaults", () -> {
            TestLargeFixedArray desc = new TestLargeFixedArray();
            equal(512 * 256 * 3 + 13, desc.Pixels.length);
            for (short pixel : desc.Pixels) check(pixel == 0);
        });
        run("LargeFixedArrays.InputAndSnapshot", () -> {
            TestLargeFixedArray source = new TestLargeFixedArray();
            for (int i = 0; i < source.Pixels.length; ++i) {
                source.Pixels[i] = (short)((i * 37 + 11) & 255);
            }
            check(CheckLargeFixedInput(source));
            TestLargeFixedArray copy = EchoLargeFixedArray(source);
            check(source.Pixels != copy.Pixels);
            check(Arrays.equals(source.Pixels, copy.Pixels));
            int last = source.Pixels.length - 1;
            short firstValue = source.Pixels[0];
            short lastValue = source.Pixels[last];
            source.Pixels[0] = 0;
            source.Pixels[last] = 0;
            equal(firstValue, copy.Pixels[0]);
            equal(lastValue, copy.Pixels[last]);
        });
        run("LargeFixedArrays.ValidateFinalChunkBeforeNativeCall", () -> {
            TestLargeFixedArray desc = new TestLargeFixedArray();
            desc.Pixels[desc.Pixels.length - 1] = 256;
            long calls = GetFixedCallCount();
            throwsType(IllegalArgumentException.class, () -> CheckLargeFixedInput(desc));
            equal(calls, GetFixedCallCount());
        });
        run("LargeFixedArrays.DescriptorDefaults", () -> {
            TestLargeDescriptorArray desc = new TestLargeDescriptorArray();
            equal(400 * 400, desc.Pixels.length);
            for (TestFixedPixel pixel : desc.Pixels) equal(17L, pixel.Value);
            check(desc.Pixels[0] != desc.Pixels[desc.Pixels.length - 1]);
        });
        run("LargeFixedArrays.DescriptorInputAndSnapshot", () -> {
            TestLargeDescriptorArray source = new TestLargeDescriptorArray();
            for (int i = 0; i < source.Pixels.length; ++i) source.Pixels[i].Value = i * 37L + 11;
            check(CheckLargeDescriptorArray(source));
            TestLargeDescriptorArray copy = EchoLargeDescriptorArray(source);
            check(source.Pixels != copy.Pixels);
            for (int i = 0; i < source.Pixels.length; ++i) {
                equal(source.Pixels[i].Value, copy.Pixels[i].Value);
                check(source.Pixels[i] != copy.Pixels[i]);
            }
            source.Pixels[0].Value = 0;
            source.Pixels[source.Pixels.length - 1].Value = 0;
            check(CheckLargeDescriptorArray(copy));
        });
        run("LargeFixedArrays.DescriptorValidationBeforeNativeCall", () -> {
            TestLargeDescriptorArray desc = new TestLargeDescriptorArray();
            int last = desc.Pixels.length - 1;
            desc.Pixels[last].Value = 0x100000000L;
            long calls = GetFixedCallCount();
            throwsType(IllegalArgumentException.class, () -> CheckLargeDescriptorArray(desc));
            desc.Pixels[last] = null;
            throwsType(IllegalArgumentException.class, () -> CheckLargeDescriptorArray(desc));
            equal(calls, GetFixedCallCount());
        });
    }
}
