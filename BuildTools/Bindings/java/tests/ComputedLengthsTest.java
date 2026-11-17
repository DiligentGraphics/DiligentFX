// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Arrays;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ComputedLengthsTest {
    public static void main(java.lang.String[] args) {
        run("ComputedLengths.BorrowedArraysUseOwnerCounts", () -> {
            float[] values;
            MarshalOutputArrayItem[] items;
            try (IMarshalArraySnapshots source = CreateMarshalArraySnapshots().ppObject) {
                values = source.GetAllValues();
                check(Arrays.equals(new float[]{-1.5f, 0.25f, 123.5f}, values));
                items = source.GetAllItems();
                equal(2, items.length);
                equal(41L, items[0].Id);
                equal("Aé水😀", items[0].Label);
                check(Arrays.equals(new float[]{0.25f, 123.5f}, items[0].pSamples));
                equal(null, items[1].Label);
                source.Update();
                equal(64f, source.GetAllValues()[1]);
                equal(99L, source.GetAllItems()[0].Id);
                source.SetValueCount(1);
                equal(1, source.GetAllValues().length);
                source.SetValueCount(0);
                equal(0, source.GetAllValues().length);
                for (long count : new long[]{0x80000000L, -1L}) {
                    source.SetValueCount(count);
                    throwsType(IllegalArgumentException.class, source::GetAllValues);
                }
                source.SetValueCount(3);
                equal(3, source.GetAllValues().length);
            }
            equal(0L, GetLiveArraySnapshotCount());
            // Snapshots survive later native mutations and destruction of their owner.
            equal(0.25f, values[1]);
            equal(41L, items[0].Id);
            equal("Aé水😀", items[0].Label);
        });

        run("ComputedLengths.OutputsQueryTheSourceParameter", () -> {
            IMarshalComputedOutputs_FillMatricesResult saved;
            try (IMarshalComputedOutputs receiver = CreateComputedOutputs(9).ppObject;
                 IMarshalComputedOutputs source = CreateComputedOutputs(2).ppObject) {
                long queries = GetComputedOutputQueryCount();
                long calls = GetComputedOutputCallCount();
                saved = receiver.FillMatrices(source, false, false, false);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, saved.returnValue);
                equal(2, saved.pMatrices.length); // Receiver's count is deliberately different.
                for (int matrix = 0; matrix < 2; ++matrix) {
                    for (int component = 0; component < 16; ++component) {
                        equal((float)(100 * matrix + component + 1), saved.pMatrices[matrix].Data[component]);
                    }
                }
                equal(queries + 2, GetComputedOutputQueryCount());
                equal(calls + 1, GetComputedOutputCallCount());

                // The native output parameter comes before both its count source and
                // the descriptor input. Java still exposes only the two inputs.
                MarshalDesc options = new MarshalDesc();
                options.Size = 41;
                check(Arrays.equals(new long[]{41, 42}, FillComputedValues(source, options).pValues));
                source.SetCount(3);
                queries = GetComputedOutputQueryCount();
                IMarshalComputedOutputs_FillMatricesResult reset = receiver.FillMatrices(source, false, false, true);
                equal(3, reset.pMatrices.length);
                equal(216f, reset.pMatrices[2].Data[15]);
                equal(queries + 2, GetComputedOutputQueryCount());
                equal(0L, source.GetValueCount());
            }
            equal(0L, GetLiveComputedOutputs());
            equal(116f, saved.pMatrices[1].Data[15]);
        });

        run("ComputedLengths.ZeroAndInvalidCounts", () -> {
            try (IMarshalComputedOutputs source = CreateComputedOutputs(0).ppObject) {
                equal(0, source.FillMatrices(source, false, false, false).pMatrices.length);
                equal(0, FillComputedValues(source, new MarshalDesc()).pValues.length);
                long calls = GetComputedOutputCallCount();
                for (long count : new long[]{0x80000000L, -1L}) {
                    source.SetCount(count);
                    throwsType(IllegalArgumentException.class, () -> source.FillMatrices(source, false, false, false));
                    throwsType(IllegalArgumentException.class, () -> FillComputedValues(source, new MarshalDesc()));
                }
                equal(calls, GetComputedOutputCallCount());
                source.SetCount(1);
                equal(1, source.FillMatrices(source, false, false, false).pMatrices.length);
            }
            equal(0L, GetLiveComputedOutputs());
        });

        run("ComputedLengths.NativeFailureKeepsZeroInitializedOutputs", () -> {
            try (IMarshalComputedOutputs source = CreateComputedOutputs(2).ppObject) {
                IMarshalComputedOutputs_FillMatricesResult failed = source.FillMatrices(source, true, false, false);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, failed.returnValue);
                equal(2, failed.pMatrices.length);
                for (MarshalComputedMatrix matrix : failed.pMatrices) {
                    check(Arrays.equals(new float[16], matrix.Data));
                }
                equal(116f, source.FillMatrices(source, false, false, false).pMatrices[1].Data[15]);
            }
            equal(0L, GetLiveComputedOutputs());
        });

        run("ComputedLengths.InvalidInputsDoNotQueryOrInvokeNativeCode", () -> {
            IMarshalComputedOutputs source = CreateComputedOutputs(2).ppObject;
            try {
                long queries = GetComputedOutputQueryCount();
                long calls = GetComputedOutputCallCount();
                throwsType(IllegalArgumentException.class, () -> source.FillMatrices(null, false, false, false));
                throwsType(IllegalArgumentException.class, () -> FillComputedValues(null, new MarshalDesc()));
                throwsType(IllegalArgumentException.class, () -> FillComputedValues(source, null));
                MarshalDesc invalid = new MarshalDesc();
                invalid.Mode = 256; // Cannot fit the native Uint8 enum.
                throwsType(IllegalArgumentException.class, () -> FillComputedValues(source, invalid));
                equal(queries, GetComputedOutputQueryCount());
                equal(calls, GetComputedOutputCallCount());
                source.close();
                throwsType(IllegalStateException.class, () -> FillComputedValues(source, new MarshalDesc()));
                equal(queries, GetComputedOutputQueryCount());
                equal(calls, GetComputedOutputCallCount());
            } finally { source.close(); }
            equal(0L, GetLiveComputedOutputs());
        });
    }
}
