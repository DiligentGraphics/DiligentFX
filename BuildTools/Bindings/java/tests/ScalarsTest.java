// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ScalarsTest {
    public static void main(java.lang.String[] args) {
        run("Scalars.SignedBoundaries", () -> {
            equal(Byte.MIN_VALUE, EchoInt8(Byte.MIN_VALUE));
            equal(Byte.MAX_VALUE, EchoInt8(Byte.MAX_VALUE));
            equal(Short.MIN_VALUE, EchoInt16(Short.MIN_VALUE));
            equal(Short.MAX_VALUE, EchoInt16(Short.MAX_VALUE));
            equal(Integer.MIN_VALUE, EchoInt32(Integer.MIN_VALUE));
            equal(Integer.MAX_VALUE, EchoInt32(Integer.MAX_VALUE));
            equal(Long.MIN_VALUE, EchoInt64(Long.MIN_VALUE));
            equal(Long.MAX_VALUE, EchoInt64(Long.MAX_VALUE));
        });
        run("Scalars.UnsignedBoundaries", () -> {
            equal((short) 255, EchoUint8((short) 255));
            equal(65535, EchoUint16(65535));
            equal(0xFFFFFFFFL, EchoUint32(0xFFFFFFFFL));
            equal(0xFEDCBA9876543210L, EchoUint64(0xFEDCBA9876543210L));
            equal(-1L, EchoUint64(-1L));
            equal(0L, EchoUint64(0L));
        });
        run("Scalars.FloatingPointAndBoolean", () -> {
            equal(1.25f, EchoFloat(1.25f));
            equal(-2.5, EchoDouble(-2.5));
            equal(Double.POSITIVE_INFINITY, EchoDouble(Double.POSITIVE_INFINITY));
            check(Double.isNaN(EchoDouble(Double.NaN)));
            equal(Double.doubleToRawLongBits(-0.0), Double.doubleToRawLongBits(EchoDouble(-0.0)));
            check(InvertBool(false));
            check(!InvertBool(true));
        });
        run("Scalars.EnumsAndStatuses", () -> {
            equal((short) 241, TEST_MODE.TEST_MODE_LAST);
            equal(TEST_MODE.TEST_MODE_LAST, EchoTestMode(TEST_MODE.TEST_MODE_LAST));
            equal(TEST_STATUS.TEST_STATUS_FAILED, EchoTestStatus(TEST_STATUS.TEST_STATUS_FAILED));
            equal(TEST_STATUS.TEST_STATUS_PENDING, EchoTestStatus(TEST_STATUS.TEST_STATUS_PENDING));
        });
        run("Scalars.RangeChecksBeforeNativeCall", () -> {
            long calls = GetCallCount();
            throwsType(IllegalArgumentException.class, () -> EchoUint8((short) -1));
            throwsType(IllegalArgumentException.class, () -> EchoUint8((short) 256));
            equal(calls, GetCallCount());
            throwsType(IllegalArgumentException.class, () -> EchoUint16(65536));
            throwsType(IllegalArgumentException.class, () -> EchoUint32(-1));
            throwsType(IllegalArgumentException.class, () -> EchoUint32(0x100000000L));
            throwsType(IllegalArgumentException.class, () -> EchoTestMode((short) 256));
        });
        run("Scalars.OriginalJavaScriptNativeChecks", () -> {
            check(CheckScalars((byte) -101, 0xfedcba98L, -0x123456789abcdefL, 0xfedcba9876543210L,
                               1.25f, -2.5, true, MARSHAL_MODE.MARSHAL_MODE_LAST));
            equal(0xfedcba9876543210L, GetWideConstant());
            equal(MARSHAL_MODE.MARSHAL_MODE_LAST, EchoMode(MARSHAL_MODE.MARSHAL_MODE_LAST));
            for (int status : new int[]{MARSHAL_STATUS.MARSHAL_STATUS_OK,
                    MARSHAL_STATUS.MARSHAL_STATUS_PENDING, MARSHAL_STATUS.MARSHAL_STATUS_FAILED})
                equal(status, EchoStatus(status));
            MarshalDesc desc = new MarshalDesc();
            check(CheckDefaultDesc(desc));
            desc.Size = 0x123456789abcdef0L;
            desc.Count = 19;
            desc.Scale = -3.5f;
            desc.Weight = 0.125;
            desc.Enabled = false;
            desc.Mode = MARSHAL_MODE.MARSHAL_MODE_FIRST;
            check(CheckCustomDesc(desc));
            ReadWideOutputResult success = ReadWideOutput(false);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, success.returnValue);
            equal(0xfedcba9876543210L, success.pValue);
            ReadWideOutputResult failure = ReadWideOutput(true);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, failure.returnValue);
            equal(0L, failure.pValue);
            RecordValue(0xfedcba98L);
            equal(0xfedcba98L, GetRecordedValue());
            equal(0xfedcba9876543210L, EchoEntityID(0xfedcba9876543210L));
            equal(-1L, GetInvalidEntityID());
        });
        run("Scalars.NativeExceptionBoundary", () -> {
            throwsType(RuntimeException.class, BindingTests::ThrowNative);
            equal(19, EchoInt32(19));
        });
    }
}
