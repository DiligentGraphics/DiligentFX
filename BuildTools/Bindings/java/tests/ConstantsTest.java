// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ConstantsTest {
    public static void main(java.lang.String[] args) {
        run("Constants.CharacterArraysPreserveBytesAndPadding", () -> {
            MarshalCharacterConstantDesc value = MarshalCharacterConstant();
            check(java.util.Arrays.equals(new byte[]{'f', 'o', 'o', 0, 0, 0, 0, 0}, value.Name));
            check(java.util.Arrays.equals(new byte[]{(byte)0xc3, (byte)0xa9, 0, 0, 0, 0, 0, 0}, value.Utf8));
            check(java.util.Arrays.equals(new byte[]{'a', 0, 'b', 0, 0, 0, 0, 0}, value.Embedded));
            check(java.util.Arrays.equals(new byte[]{(byte)0xc3, (byte)0xa9, 0, 0, 0, 0, 0, 0}, value.Signed));
            check(java.util.Arrays.equals(new short[]{0xc3, 0xa9, 0, 0, 0, 0, 0, 0}, value.Unsigned));
            check(java.util.Arrays.equals(new byte[]{'a', 0, 'b', 0, 0, 0, 0, 0}, value.SignedAlias));
            check(java.util.Arrays.equals(new short[]{'a', 0, 'b', 0, 0, 0, 0, 0}, value.UnsignedAlias));
            check(java.util.Arrays.equals(new short[8], value.Empty));
            equal(2, value.Rows.length);
            equal(8, value.Rows[0].length);
            equal(8, value.Rows[1].length);
            check(CheckCharacterConstant(value));
            value.Name[0] = 0;
            value.Utf8[7] = 1;
            value.Rows[1][0] = 0;
            check(!CheckCharacterConstant(value));
            check(CheckCharacterConstant(MarshalCharacterConstant()));
        });
        run("Constants.StandaloneByteArraysRemainNumeric", () -> {
            byte[] signed = MarshalSignedByteConstant();
            short[] unsigned = MarshalUnsignedByteConstant();
            check(java.util.Arrays.equals(new byte[]{(byte)0xc3, (byte)0xa9, 0, 0, 0, 0, 0, 0}, signed));
            check(java.util.Arrays.equals(new short[]{0xc3, 0xa9, 0, 0, 0, 0, 0, 0}, unsigned));
            check(java.util.Arrays.equals(new byte[]{'a', 0, 'b', 0, 0, 0, 0, 0}, MarshalSignedAliasConstant()));
            check(java.util.Arrays.equals(new short[]{'a', 0, 'b', 0, 0, 0, 0, 0}, MarshalUnsignedAliasConstant()));
            signed[0] = 0;
            unsigned[0] = 0;
            equal((byte)0xc3, MarshalSignedByteConstant()[0]);
            equal((short)0xc3, MarshalUnsignedByteConstant()[0]);
        });
        run("Constants.LargeArrayInitializersCompileAndRemainIndependent", () -> {
            int[] zeros = MarshalZeroLookup();
            int[] values = MarshalPatternLookup();
            int[] pattern = {17, 65535, 0, 32768};
            equal(16384, zeros.length);
            equal(16384, values.length);
            for (int i = 0; i < values.length; ++i) {
                equal(0, zeros[i]);
                equal(pattern[i % pattern.length], values[i]);
            }
            zeros[16383] = 1;
            values[128] = 0;
            equal(0, MarshalZeroLookup()[16383]);
            equal(17, MarshalPatternLookup()[128]);
            MarshalLargeConstantDesc descriptor = MarshalLargeConstant();
            check(CheckLargeConstant(descriptor));
            descriptor.Values[16383] = 0;
            check(!CheckLargeConstant(descriptor));
            check(CheckLargeConstant(MarshalLargeConstant()));
        });
        run("Constants.StringContentsAndNativeValues", () -> {
            equal("Translation", MarshalAnimationName);
            equal(MarshalAnimationName, MarshalAnimationNamePointer);
            equal("quote\" slash\\ line\n\t\u00e9 \ud83d\ude42", MarshalEscapedName);
            equal(GetEscapedExportName(), MarshalEscapedName);
            equal("", MarshalEmptyName);
            equal("a\0b", MarshalEmbeddedNullName);
            // This case label also verifies that strings are compile-time constants.
            switch (MarshalAnimationNamePointer) {
                case MarshalAnimationName: break;
                default: throw new AssertionError("Unexpected property name");
            }
        });
        run("Constants.ArrayAndDescriptorSnapshots", () -> {
            long[] lookup = MarshalLookup();
            check(java.util.Arrays.equals(new long[]{0L, 0xfedcba9876543210L}, lookup));
            lookup[1] = 0;
            equal(0xfedcba9876543210L, MarshalLookup()[1]);
            MarshalExportNames names = MarshalNames();
            equal("Translation", names.Name);
            equal(-1L, names.Invalid);
            check(java.util.Arrays.equals(new long[]{3, 5}, names.Parts));
            equal(8L, SumExportParts(names));
            names.Name = "changed";
            names.Invalid = 0;
            names.Parts[0] = 99;
            MarshalExportNames fresh = MarshalNames();
            check(names != fresh && names.Parts != fresh.Parts);
            equal("Translation", fresh.Name);
            equal(-1L, fresh.Invalid);
            equal(8L, SumExportParts(fresh));
        });
        run("Constants.InheritedFieldsAndDefaults", () -> {
            MarshalExportLeaf explicit = MarshalInherited();
            equal(0xfedcba9876543210L, explicit.A);
            equal(13L, explicit.B);
            equal(1.5f, explicit.C);
            check(CheckInheritedExport(explicit, false));
            MarshalExportLeaf defaults = MarshalInheritedDefault();
            equal(5L, defaults.A);
            equal(7L, defaults.B);
            equal(0.5f, defaults.C);
            check(CheckInheritedExport(defaults, true));
            explicit.A = 0;
            defaults.C = 0;
            check(CheckInheritedExport(MarshalInherited(), false));
            check(CheckInheritedExport(MarshalInheritedDefault(), true));
        });
        run("Constants.NullNestedNameIsRejectedSafely", () -> {
            check(!CheckNestedExport(new MarshalExportGroup()));
            MarshalExportGroup value = MarshalNestedConstant();
            value.Named.Name = null;
            check(!CheckNestedExport(value));
        });
        run("Constants.NestedSnapshotsAreIndependent", () -> {
            MarshalExportGroup value = MarshalNestedConstant();
            check(CheckNestedExport(value));
            equal(11L, value.Leaves[0].A);
            equal(5L, value.Leaves[1].A);
            check(java.util.Arrays.deepEquals(new short[][]{{-2, 3}, {4, -5}}, value.Grid));
            value.Named.Parts[1] = 99;
            value.Leaves[0].A = 0;
            value.Leaves[1] = null;
            value.Grid[0][0] = 0;
            value.Grid[1] = null;
            MarshalExportGroup fresh = MarshalNestedConstant();
            check(value.Named != fresh.Named && value.Named.Parts != fresh.Named.Parts);
            check(value.Leaves != fresh.Leaves && value.Leaves[0] != fresh.Leaves[0]);
            check(value.Grid != fresh.Grid && value.Grid[0] != fresh.Grid[0]);
            check(CheckNestedExport(fresh));
        });
        run("Constants.IntegerWidthsAliasesAndEnums", () -> {
            equal((byte)-128, TestConstantSignedByte);
            equal((short)255, TestConstantByte);
            equal((short)-32768, TestConstantShort);
            equal(65535, TestConstantUnsignedShort);
            equal(Integer.MIN_VALUE, TestConstantInt);
            equal(0xffffffffL, TestConstantUnsignedInt);
            equal(Long.MIN_VALUE, TestConstantLong);
            equal(-1L, TestConstantInvalidID);
            equal(0xfedcba9876543210L, TestConstantWideID);
            equal(0x10000055L, TestConstantMask);
            equal(true, TestConstantEnabled);
            equal(false, TestConstantDisabled);
            equal(TEST_MODE.TEST_MODE_LAST, TestConstantMode);
            // A case label requires a Java compile-time constant expression.
            switch (TestConstantMode) {
                case TestConstantMode: break;
                default: throw new AssertionError("Unexpected mode");
            }
        });
        run("Constants.FloatingPointValues", () -> {
            equal(0.1f, TestConstantFloat);
            equal(0.1, TestConstantDouble);
            equal(Float.floatToRawIntBits(-0.0f), Float.floatToRawIntBits(TestConstantNegativeZero));
            equal(Double.doubleToRawLongBits(-0.0), Double.doubleToRawLongBits(TestConstantNegativeDoubleZero));
            equal(Float.MAX_VALUE, TestConstantFloatMax);
            equal(Double.MAX_VALUE, TestConstantDoubleMax);
            equal(Float.MIN_NORMAL * 0.5f, TestConstantFloatSubnormal);
            equal(Double.MIN_NORMAL * 0.5, TestConstantDoubleSubnormal);
            equal(Float.POSITIVE_INFINITY, TestConstantInfinity);
            equal(Double.NEGATIVE_INFINITY, TestConstantNegativeInfinity);
            check(Float.isNaN(TestConstantNotANumber));
        });
        run("Constants.MatchNativeCompiler", () -> {
            TestConstantSnapshot nativeValues = GetConstantSnapshot();
            equal(nativeValues.SignedByte, TestConstantSignedByte);
            equal(nativeValues.Byte, TestConstantByte);
            equal(nativeValues.Short, TestConstantShort);
            equal(nativeValues.UnsignedShort, TestConstantUnsignedShort);
            equal(nativeValues.Int, TestConstantInt);
            equal(nativeValues.UnsignedInt, TestConstantUnsignedInt);
            equal(nativeValues.Long, TestConstantLong);
            equal(nativeValues.InvalidID, TestConstantInvalidID);
            equal(nativeValues.WideID, TestConstantWideID);
            equal(nativeValues.Mask, TestConstantMask);
            equal(nativeValues.Enabled, TestConstantEnabled);
            equal(nativeValues.Disabled, TestConstantDisabled);
            equal(nativeValues.Mode, TestConstantMode);
            equal(nativeValues.Float, TestConstantFloat);
            equal(nativeValues.Double, TestConstantDouble);
            equal(nativeValues.NegativeZero, TestConstantNegativeZero);
            equal(nativeValues.NegativeDoubleZero, TestConstantNegativeDoubleZero);
            equal(nativeValues.FloatMax, TestConstantFloatMax);
            equal(nativeValues.DoubleMax, TestConstantDoubleMax);
            equal(nativeValues.FloatSubnormal, TestConstantFloatSubnormal);
            equal(nativeValues.DoubleSubnormal, TestConstantDoubleSubnormal);
            equal(nativeValues.Infinity, TestConstantInfinity);
            equal(nativeValues.NegativeInfinity, TestConstantNegativeInfinity);
            equal(nativeValues.NotANumber, TestConstantNotANumber);
            equal(Float.floatToRawIntBits(nativeValues.NegativeZero), Float.floatToRawIntBits(TestConstantNegativeZero));
            equal(Double.doubleToRawLongBits(nativeValues.NegativeDoubleZero), Double.doubleToRawLongBits(TestConstantNegativeDoubleZero));
        });
    }
}
