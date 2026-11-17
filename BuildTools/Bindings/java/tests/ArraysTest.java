// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Arrays;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ArraysTest {
    public static void main(java.lang.String[] args) {
        run("Arrays.InputAndSnapshot", () -> {
            int[] values = {Integer.MIN_VALUE, 17, Integer.MAX_VALUE};
            equal(16L, SumValues(values, values.length));
            int[] result = EchoValues(values, values.length);
            check(Arrays.equals(values, result));
            check(result != values);
            values[0] = 0;
            equal(Integer.MIN_VALUE, result[0]);
            check(Arrays.equals(new byte[]{-128, 127}, EchoSignedBytes(new byte[]{-128, 127}, 2)));
            check(Arrays.equals(new float[]{-1.25f, 3.5f}, EchoFloats(new float[]{-1.25f, 3.5f}, 2)));
            check(Arrays.equals(new short[]{0, 241}, EchoModes(new short[]{0, 241}, 2)));
            throwsType(IllegalArgumentException.class, () -> EchoModes(new short[]{256}, 1));
            equal(258L, SumBytes(new short[]{0, 3, 255}, 3));
            check(Arrays.equals(new boolean[]{true, false}, EchoBools(new boolean[]{true, false}, 2)));
            check(Arrays.equals(new double[]{-1.5, Double.NaN}, EchoDoubles(new double[]{-1.5, Double.NaN}, 2)));
            check(Arrays.equals(new long[]{-1, Long.MIN_VALUE}, EchoWideValues(new long[]{-1, Long.MIN_VALUE}, 2)));
        });
        run("Arrays.RequiredEmptyInputHasNativeStorage", () -> {
            // The fixture checks the pointer even when native code reads no elements.
            check(HasRequiredArrayStorage(null, 0));
            check(HasRequiredArrayStorage(new int[0], 0));
            check(HasRequiredArrayStorage(new int[]{17}, 0));
            check(HasRequiredArrayStorage(new int[]{17}, 1));
            throwsType(IllegalArgumentException.class, () -> HasRequiredArrayStorage(null, 1));
        });
        run("Arrays.NullableInputPreservesAbsence", () -> {
            check(IsNullArray(null, 0));
            check(!IsNullArray(new int[0], 0));
            check(!IsNullArray(new int[]{17}, 1));
            check(IsNullArray(null, 3));
        });
        run("Arrays.NullableResultPreservesAbsence", () -> {
            equal(null, GetNullArray(0));
            equal(null, GetNullArray(3));
            equal(null, EchoOptionalValues(null, 0));
            equal(0, EchoOptionalValues(new int[0], 0).length);
            check(Arrays.equals(new int[]{17, 31}, EchoOptionalValues(new int[]{17, 31}, 2)));
            throwsType(IllegalArgumentException.class, () -> EchoOptionalValues(new int[0], 1));
        });
        run("Arrays.CountValidation", () -> {
            equal(0L, SumValues(null, 0));
            equal(0, EchoValues(new int[0], 0).length);
            equal(5L, SumValues(new int[]{5, 7}, 1));
            throwsType(IllegalArgumentException.class, () -> SumValues(null, 1));
            throwsType(IllegalArgumentException.class, () -> SumValues(new int[2], 3));
            throwsType(IllegalArgumentException.class, () -> SumValues(new int[0], -1));
            throwsType(IllegalArgumentException.class, () -> SumValues(new int[0], 0x80000000L));
            throwsType(IllegalArgumentException.class, () -> SumBytes(new short[]{256}, 1));
        });
        run("Arrays.CapacityOnlyOutputsInitializeUnwrittenElements", () -> {
            for (int capacity : new int[]{0, 1, 5, 257}) {
                for (int iteration = 0; iteration < 3; ++iteration) {
                    FillCapacityValuesResult partial = FillCapacityValues(capacity, true);
                    equal(TEST_STATUS.TEST_STATUS_OK, partial.returnValue);
                    equal(capacity, partial.pValues.length);
                    for (int i = 0; i < capacity; ++i)
                        equal(i < 2 ? 31L + 16L * i : 0L, partial.pValues[i]);
                    FillCapacityValuesResult failed = FillCapacityValues(capacity, false);
                    equal(TEST_STATUS.TEST_STATUS_FAILED, failed.returnValue);
                    equal(capacity, failed.pValues.length);
                    for (long value : failed.pValues) equal(0L, value);
                }
            }
        });
        run("Arrays.CallerFilledOutputs", () -> {
            FillValuesResult values = FillValues(5);
            equal(TEST_STATUS.TEST_STATUS_OK, values.returnValue);
            equal(3L, values.Written);
            check(Arrays.equals(new int[]{65535, 65534, 65533}, values.pValues));
            FillValuesResult shortResult = FillValues(1);
            equal(TEST_STATUS.TEST_STATUS_PENDING, shortResult.returnValue);
            check(Arrays.equals(new int[]{65535}, shortResult.pValues));
            equal(0, FillValues(0).pValues.length);
            throwsType(IllegalArgumentException.class, () -> InvalidWrittenCount(1));
        });
    }
}
