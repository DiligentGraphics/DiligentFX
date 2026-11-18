// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;
import java.util.Objects;

// Exercises the original fixed_array_tests.mjs fixture, including native
// per-type input checks and read-only output descriptors.
public final class FixedArrayMarshallingTest {
    private static Object[] arrays(MarshalFixedArrays value) {
        return new Object[]{value.ValuesInt8, value.ValuesUint8, value.ValuesInt16, value.ValuesUint16,
            value.ValuesInt32, value.ValuesUint32, value.ValuesInt64, value.ValuesUint64,
            value.ValuesFloat32, value.ValuesFloat64, value.ValuesBool, value.ValuesMARSHAL_MODE, value.ValuesChar};
    }

    public static void main(java.lang.String[] args) {
        run("FixedArrayMarshalling.EveryScalarTypeUsesNativeDefaultsAndChecks", () -> {
            ResetFixedArrays();
            Object[] defaults = {new byte[]{-128, 17, 127}, new short[]{0, 128, 255},
                new short[]{-32768, 17, 32767}, new int[]{0, 32768, 65535},
                new int[]{Integer.MIN_VALUE, 17, Integer.MAX_VALUE}, new long[]{0, 0x80000000L, 0xffffffffL},
                new long[]{-0x7000000000000001L, 17, 0x7123456789abcdefL}, new long[]{0, Long.MIN_VALUE, 0xfedcba9876543210L},
                new float[]{-1.5f, 0.25f, 123.5f}, new double[]{-1.25, 0.1, 123456.789},
                new boolean[]{true, false, true}, new short[]{2, 241, 2}, new byte[]{65, 0, -1}};
            check(Arrays.deepEquals(defaults, arrays(new MarshalFixedArrays())));
            check(Arrays.deepEquals(defaults, arrays(GetFixedArrays(false))));
            MarshalFixedArrays input = new MarshalFixedArrays();
            input.ValuesInt8 = new byte[]{-100, -2, 111};
            input.ValuesUint8 = new short[]{42, 200, 250};
            input.ValuesInt16 = new short[]{-30000, -20, 30000};
            input.ValuesUint16 = new int[]{12, 40000, 60000};
            input.ValuesInt32 = new int[]{-2000000000, -20, 2000000000};
            input.ValuesUint32 = new long[]{13, 0x81234567L, 0xfedcba98L};
            input.ValuesInt64 = new long[]{-0x6123456789abcdefL, -30, 0x6123456789abcdefL};
            input.ValuesUint64 = new long[]{25, 0x8123456789abcdefL, 0xeeeeeeeeeeeeeeeeL};
            input.ValuesFloat32 = new float[]{-2.5f, 1.75f, 456.25f};
            input.ValuesFloat64 = new double[]{-2.25, 0.2, 987654.321};
            input.ValuesBool = new boolean[]{false, true, false};
            input.ValuesMARSHAL_MODE = new short[]{241, 2, 241};
            input.ValuesChar = new byte[]{66, -128, 0};
            Object[] original = arrays(input);
            Object[] copied = arrays(EchoFixedArrays(input, false));
            for (int type = 0; type < original.length; ++type) {
                check(CheckFixedInputs(input, type));
                check(Objects.deepEquals(original[type], copied[type]));
                check(original[type] != copied[type]);
            }
            check(Arrays.deepEquals(defaults, arrays(GetFixedArrays(false))));
        });
        run("FixedArrayMarshalling.MatrixAndMultidimensionalIndexing", () -> {
            MarshalFixedMatrix matrix = new MarshalFixedMatrix();
            for (int i = 0; i < 16; ++i) equal(i % 5 == 0 ? 1.f : 0.f, matrix.Data[i]);
            for (int i = 0; i < 16; ++i) matrix.Data[i] = i + 0.5f;
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    equal(row * 4 + column + 0.5f, ReadFixedMatrix(matrix, row, column));
            check(Arrays.equals(matrix.Data, EchoFixedMatrix(matrix).Data));
            MarshalFixedArrays input = new MarshalFixedArrays();
            input.Rows = new float[][]{{11, 12, 13, 14}, {21, 22, 23, 24}};
            input.Cube = new float[][][]{{{11, 12}, {13, 14}}, {{21, 22}, {23, 24}}};
            for (int row = 0; row < 2; ++row)
                for (int column = 0; column < 4; ++column)
                    equal(input.Rows[row][column], ReadFixedRow(input, row, column));
            for (int plane = 0; plane < 2; ++plane)
                for (int row = 0; row < 2; ++row)
                    for (int column = 0; column < 2; ++column)
                        equal(input.Cube[plane][row][column], ReadFixedCube(input, plane, row, column));
            MarshalFixedArrays copy = EchoFixedArrays(input, false);
            check(Arrays.deepEquals(input.Rows, copy.Rows));
            check(Arrays.deepEquals(input.Cube, copy.Cube));
            input.Rows[0][0] = 0;
            input.Cube[1][1][1] = 0;
            equal(11.f, copy.Rows[0][0]);
            equal(24.f, copy.Cube[1][1][1]);
        });
        run("FixedArrayMarshalling.NestedElementsPreserveParentDefaultsAndStorage", () -> {
            MarshalFixedArrays input = new MarshalFixedArrays();
            input.Children[1].Id = 73;
            input.Children[1].Samples = new float[]{8, 9};
            equal(11L, ReadFixedChild(input, 0));
            equal(73L, ReadFixedChild(input, 1));
            equal(4.f, ReadFixedSample(input, 0, 1));
            equal(8.f, ReadFixedSample(input, 1, 0));
            java.lang.String text = "Aé水😀".repeat(100);
            input.Children[0].Label = text;
            input.Children[0].pExtra = new short[]{-2, 30000};
            input.Children[0].ExtraCount = 2;
            input.Grid[0][0].Id = 41;
            input.Grid[1][1].Id = 99;
            input.Grid[1][1].Label = text;
            MarshalFixedArrays copy = EchoFixedArrays(input, false);
            equal(text, copy.Children[0].Label);
            equal("second", copy.Children[1].Label);
            check(Arrays.equals(new short[]{-2, 30000}, copy.Children[0].pExtra));
            equal(41L, copy.Grid[0][0].Id);
            equal(99L, copy.Grid[1][1].Id);
            equal(text, copy.Grid[1][1].Label);
            check(Arrays.equals(new float[]{1, 2}, copy.Grid[0][1].Samples));
            check(copy.Matrices[0].Data != copy.Matrices[1].Data);
            input.Children[0].pExtra[0] = 99;
            input.Grid[1][1].Label = "changed";
            for (int i = 0; i < 20; ++i) {
                throwsType(IllegalArgumentException.class, () -> EchoFixedArrays(input, true));
                equal("changed", EchoFixedArrays(input, false).Grid[1][1].Label);
            }
            equal((short) -2, copy.Children[0].pExtra[0]);
            equal(text, copy.Grid[1][1].Label);
        });
        run("FixedArrayMarshalling.SnapshotsSurviveNativeMutation", () -> {
            ResetFixedArrays();
            try {
                MarshalFixedArrays first = GetFixedArrays(false);
                UpdateFixedArrays();
                MarshalFixedArrays second = GetFixedArrays(false);
                equal(10.f, second.ValuesFloat32[0]);
                equal(20.f, second.Rows[1][2]);
                equal(30L, second.Children[0].Id);
                equal(40.f, second.Children[0].Samples[1]);
                equal(50L, second.Grid[1][0].Id);
                equal(60.f, second.Matrices[1].Data[15]);
                equal(-1.5f, first.ValuesFloat32[0]);
                equal(7.f, first.Rows[1][2]);
                equal(11L, first.Children[0].Id);
                equal("first", first.Children[0].Label);
                equal(1.f, first.Matrices[1].Data[15]);
                second.Rows[0][0] = 99;
                equal(1.f, GetFixedArrays(false).Rows[0][0]);
            } finally {
                ResetFixedArrays();
            }
        });
        run("FixedArrayMarshalling.ConstArraysAndElementsAreIndependentSnapshots", () -> {
            MarshalFixedReadOnly result = GetFixedReadOnlyArrays();
            check(Arrays.equals(new int[]{10, 20, 30}, result.Values));
            equal(5L, result.Items[0].Value);
            equal(9L, result.Items[1].Value);
            result.Values[1] = 99;
            result.Items[0].Value = 100;
            MarshalFixedReadOnly next = GetFixedReadOnlyArrays();
            check(Arrays.equals(new int[]{10, 20, 30}, next.Values));
            equal(5L, next.Items[0].Value);
            check(result.Items[0] != next.Items[0]);
            MarshalFixedReadOnly defaults = new MarshalFixedReadOnly();
            equal(5L, defaults.Items[0].Value);
            equal(13L, new MarshalFixedReadOnlyElement().Value);
        });
        run("FixedArrayMarshalling.InvalidDimensionsAndLateFields", () -> {
            long calls = GetFixedCallCount();
            MarshalFixedArrays input = new MarshalFixedArrays();
            for (int length : new int[]{0, 1, 3}) {
                input.Children = new MarshalFixedChild[length];
                throwsType(IllegalArgumentException.class, () -> ReadFixedChild(input, 0));
            }
            input.Children = new MarshalFixedChild[]{new MarshalFixedChild(), null};
            input.Children[0].Label = "allocated".repeat(100);
            throwsType(IllegalArgumentException.class, () -> ReadFixedChild(input, 0));
            input.Children[1] = new MarshalFixedChild();
            input.Children[1].Samples = new float[1];
            throwsType(IllegalArgumentException.class, () -> ReadFixedChild(input, 0));
            input.Children[1].Samples = new float[2];
            input.Children[1].Label = null;
            throwsType(IllegalArgumentException.class, () -> ReadFixedChild(input, 0));
            input.Children[1].Label = "restored";
            input.Rows[1] = new float[3];
            throwsType(IllegalArgumentException.class, () -> ReadFixedRow(input, 0, 0));
            input.Rows[1] = new float[4];
            input.Cube[1] = new float[1][2];
            throwsType(IllegalArgumentException.class, () -> ReadFixedCube(input, 0, 0, 0));
            equal(calls, GetFixedCallCount());
        });
    }
}
