// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.function.BiPredicate;
import java.util.function.Supplier;

// The native CheckArray functions are also the input oracles in input_array_tests.mjs.
public final class InputArraysTest {
    private static <T> void type(java.lang.String name, T values, Supplier<T> empty, BiPredicate<T, Long> invoke) {
        run("InputArrays." + name, () -> {
            check(invoke.test(values, 3L));
            check(invoke.test(values, 2L));
            // At count zero the native fixture reports whether storage is null.
            // Unlike JavaScript, Java normalizes required empty input to non-null
            // storage, including a Java null supplied with count zero.
            check(!invoke.test(empty.get(), 0L));
            check(!invoke.test(null, 0L));
            long calls = GetArrayCallCount();
            throwsType(IllegalArgumentException.class, () -> invoke.test(values, 4L));
            throwsType(IllegalArgumentException.class, () -> invoke.test(values, -1L));
            throwsType(IllegalArgumentException.class, () -> invoke.test(values, 0x100000000L));
            throwsType(IllegalArgumentException.class, () -> invoke.test(null, 1L));
            equal(calls, GetArrayCallCount());
            check(invoke.test(values, 3L));
        });
    }

    public static void main(java.lang.String[] args) {
        type("Int8", new byte[]{-128, 17, 127}, () -> new byte[0], BindingTests::CheckArrayInt8);
        type("Uint8", new short[]{0, 128, 255}, () -> new short[0], BindingTests::CheckArrayUint8);
        type("Int16", new short[]{-32768, 17, 32767}, () -> new short[0], BindingTests::CheckArrayInt16);
        type("Uint16", new int[]{0, 32768, 65535}, () -> new int[0], BindingTests::CheckArrayUint16);
        type("Int32", new int[]{Integer.MIN_VALUE, 17, Integer.MAX_VALUE}, () -> new int[0], BindingTests::CheckArrayInt32);
        type("Uint32", new long[]{0, 0x80000000L, 0xffffffffL}, () -> new long[0], BindingTests::CheckArrayUint32);
        type("Int64", new long[]{-0x7000000000000001L, 17, 0x7123456789abcdefL}, () -> new long[0], BindingTests::CheckArrayInt64);
        type("Uint64", new long[]{0, Long.MIN_VALUE, 0xfedcba9876543210L}, () -> new long[0], BindingTests::CheckArrayUint64);
        type("Float32", new float[]{-1.5f, 0.25f, 123.5f}, () -> new float[0], BindingTests::CheckArrayFloat32);
        type("Float64", new double[]{-1.25, 0.1, 123456.789}, () -> new double[0], BindingTests::CheckArrayFloat64);
        type("Bool", new boolean[]{true, false, true}, () -> new boolean[0], BindingTests::CheckArrayBool);
        type("Mode", new short[]{2, 241, 2}, () -> new short[0], BindingTests::CheckArrayMode);
        run("InputArrays.WidenedElementsValidateOnlyTheSelectedPrefix", () -> {
            check(CheckArrayUint8(new short[]{0, 128, 256}, 2));
            check(CheckArrayUint16(new int[]{0, 32768, 65536}, 2));
            check(CheckArrayUint32(new long[]{0, 0x80000000L, 0x100000000L}, 2));
            check(CheckArrayMode(new short[]{2, 241, 256}, 2));
            long calls = GetArrayCallCount();
            throwsType(IllegalArgumentException.class, () -> CheckArrayUint8(new short[]{0, 128, 256}, 3));
            throwsType(IllegalArgumentException.class, () -> CheckArrayUint8(new short[]{-1}, 1));
            throwsType(IllegalArgumentException.class, () -> CheckArrayUint16(new int[]{0, 32768, 65536}, 3));
            throwsType(IllegalArgumentException.class, () -> CheckArrayUint32(new long[]{0, 0x80000000L, 0x100000000L}, 3));
            throwsType(IllegalArgumentException.class, () -> CheckArrayMode(new short[]{2, 241, 256}, 3));
            equal(calls, GetArrayCallCount());
        });
    }
}
