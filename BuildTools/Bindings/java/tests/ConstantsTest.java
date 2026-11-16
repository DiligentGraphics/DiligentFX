// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ConstantsTest {
    public static void main(java.lang.String[] args) {
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
            TestConstantSnapshot nativeValues = new TestConstantSnapshot();
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
