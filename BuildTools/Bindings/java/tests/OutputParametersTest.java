// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;

// Corresponds to outputparameters_tests.mjs, outputbuffers_tests.mjs and
// the conversion-failure case in borrowedinterfaces_tests.mjs.
public final class OutputParametersTest {
    public static void main(java.lang.String[] args) {
        run("OutputParameters.ScalarReferencesPreserveStatus", () -> {
            ReadReferenceOutputResult pending = ReadReferenceOutput(MARSHAL_STATUS.MARSHAL_STATUS_PENDING);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, pending.returnValue);
            equal(0xfedcba9876543210L, pending.Value);
            ReadReferenceOutputResult failed = ReadReferenceOutput(MARSHAL_STATUS.MARSHAL_STATUS_FAILED);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, failed.returnValue);
            equal(0L, failed.Value);
        });
        run("OutputParameters.StructOutputsPreserveDefaults", () -> {
            ReadRecordOutputResult result = ReadRecordOutput(true, false, false);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, result.returnValue);
            equal(0xfedcba9876543210L, result.Value.Value);
            check(Arrays.equals(new float[]{1, 2.5f, 0, 1}, result.Value.Data));
            equal("native output", result.Value.Label);
            ReadRecordOutputResult failed = ReadRecordOutput(false, false, false);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, failed.returnValue);
            equal(17L, failed.Value.Value);
            equal("default", failed.Value.Label);
            check(Arrays.equals(new float[]{1, 0, 0, 1}, failed.Value.Data));
            MarshalOutputRecord pointer = MakeRecordOutput().pValue;
            equal(42L, pointer.Value);
            check(Arrays.equals(new float[]{1, 0, -3, 1}, pointer.Data));
            for (int i = 0; i < 20; ++i) {
                throwsType(IllegalArgumentException.class, () -> ReadRecordOutput(true, false, true));
                equal("native output", ReadRecordOutput(true, false, false).Value.Label);
            }
        });
        run("OutputParameters.CapacityWrittenCountAndMultipleOutputs", () -> {
            FillIdsResult result = FillIds(5, false, false);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, result.returnValue);
            equal(2L, result.Written);
            check(Arrays.equals(new long[]{0xfedcba9876543210L, 0xfedcba9876543211L}, result.pIds));
            equal(0, FillIds(0, false, false).pIds.length);
            FillIdsResult one = FillIds(1, false, false);
            equal(1L, one.Written);
            check(Arrays.equals(new long[]{0xfedcba9876543210L}, one.pIds));
            throwsType(IllegalArgumentException.class, () -> FillIds(2, true, false));
            MarshalOutputRecord[] records = FillRecords(2).pRecords;
            equal(101L, records[1].Value);
            equal("default", records[0].Label);
            check(Arrays.equals(new float[]{1, 0, 0, 1}, records[0].Data));
            records[0].Data[0] = 100;
            equal(1.f, records[1].Data[0]);
            FillMultipleResult multiple = FillMultiple();
            equal(9L, multiple.Count);
            equal("multiple", multiple.ppLabel);
        });
        run("OutputParameters.StaticArraysPreserveNullAndCopyValues", () -> {
            equal(null, GetStaticOutputArray(0));
            equal(null, GetStaticOutputArray(4));
            check(Arrays.equals(new double[]{-1.25}, GetStaticOutputArray(1)));
            double[] values = GetStaticOutputArray(3);
            check(Arrays.equals(new double[]{-1.25, 0.1, 123456.789}, values));
            values[0] = 0;
            equal(-1.25, GetStaticOutputArray(1)[0]);
        });
        run("OutputParameters.PartialConversionReleasesAllOwnedOutputs", () -> {
            long live = GetLiveObjectCount();
            long destroyed = GetDestructionCount();
            throwsType(IllegalArgumentException.class, BindingTests::MakeFailingOutputs);
            equal(live, GetLiveObjectCount());
            equal(destroyed + 2, GetDestructionCount());
        });
    }
}
