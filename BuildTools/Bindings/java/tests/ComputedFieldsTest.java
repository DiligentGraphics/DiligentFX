// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Arrays;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ComputedFieldsTest {
    private static MarshalComputedBufferDesc bytes() {
        MarshalComputedBufferDesc desc = new MarshalComputedBufferDesc();
        desc.pBytes = new byte[]{0, 127, (byte)128, (byte)255, 17, 31, 99};
        return desc;
    }

    public static void main(java.lang.String[] args) {
        run("ComputedFields.NestedCountsAndTypedSnapshots", () -> {
            MarshalNestedCountDesc desc = new MarshalNestedCountDesc();
            desc.pValues = new long[]{2, 3};
            desc.pComputed = new long[]{5, 7, 11, 99};
            equal(28L, SumNestedCount(desc));
            MarshalNestedCountDesc copy = EchoNestedCount(desc);
            check(Arrays.equals(new long[]{5, 7, 11}, copy.pComputed));
            desc.pComputed[0] = 0;
            equal(5L, copy.pComputed[0]);
            desc.Counts.ItemCount = 3;
            desc.pValues = new long[]{2, 3, 7};
            desc.pPair = new long[]{11, 13};
            equal(153L, SumNestedCount(desc));
            long calls = GetArrayCallCount();
            desc.pComputed = new long[3];
            throwsType(IllegalArgumentException.class, () -> SumNestedCount(desc));
            equal(calls, GetArrayCallCount());
            desc.pComputed = null;
            equal(null, EchoNestedCount(desc).pComputed);
        });

        run("ComputedFields.NativeDefaultsAndBytePrefixes", () -> {
            long queries = GetComputedBufferCountQueryCount();
            MarshalComputedBufferDesc defaults = new MarshalComputedBufferDesc();
            equal(queries + 1, GetComputedBufferCountQueryCount());
            equal(2L, defaults.ElementSize);
            equal(3L, defaults.ElementCount);
            equal(null, defaults.pBytes);

            MarshalComputedBufferDesc desc = bytes();
            long checksum = 0;
            for (int i = 0; i < 6; ++i) checksum = (checksum * 31 + (desc.pBytes[i] & 255)) & 0xffffffffL;
            queries = GetComputedBufferCountQueryCount();
            equal(checksum, GetComputedBufferChecksum(desc));
            equal(queries + 1, GetComputedBufferCountQueryCount());
            queries = GetComputedBufferCountQueryCount();
            MarshalComputedBufferDesc copy = EchoComputedBuffer(desc, false, false);
            equal(queries + 2, GetComputedBufferCountQueryCount());
            check(Arrays.equals(Arrays.copyOf(desc.pBytes, 6), copy.pBytes));
            desc.pBytes[1] = 0;
            equal((byte)127, copy.pBytes[1]);
            desc.ElementSize = 1;
            desc.ElementCount = 2;
            equal(2, EchoComputedBuffer(desc, false, false).pBytes.length);
        });

        run("ComputedFields.NullAndEmptyAreDistinct", () -> {
            MarshalComputedBufferDesc desc = new MarshalComputedBufferDesc();
            equal(null, EchoComputedBuffer(desc, false, false).pBytes);
            equal(0L, GetComputedBufferChecksum(desc));
            desc.ElementCount = 0;
            desc.pBytes = new byte[0];
            equal(0, EchoComputedBuffer(desc, false, false).pBytes.length);
            desc.pBytes = null;
            equal(null, EchoComputedBuffer(desc, false, false).pBytes);
            desc.ElementSize = 0;
            desc.ElementCount = 7;
            desc.pBytes = new byte[]{9};
            equal(0, EchoComputedBuffer(desc, false, false).pBytes.length);
        });

        run("ComputedFields.InheritedAdapter", () -> {
            MarshalDerivedComputedBufferDesc desc = new MarshalDerivedComputedBufferDesc();
            desc.pBytes = new byte[]{1, 2, 3, 4, 5, 6};
            long queries = GetComputedBufferCountQueryCount();
            MarshalDerivedComputedBufferDesc copy = EchoDerivedComputedBuffer(desc);
            equal(queries + 2, GetComputedBufferCountQueryCount());
            equal(17L, copy.Tag);
            check(Arrays.equals(desc.pBytes, copy.pBytes));
            check(desc.pBytes != copy.pBytes);
        });

        run("ComputedFields.BoundsFailuresAndReferenceRollback", () -> {
            try (IMarshalObject object = CreateMarshalObject(new MarshalDesc(), 0, false).ppObject) {
                MarshalComputedBufferDesc desc = bytes();
                desc.pObject = object;
                long calls = GetByteSpanCallCount();
                desc.ElementCount = 4;
                throwsType(IllegalArgumentException.class, () -> GetComputedBufferChecksum(desc));
                equal(calls, GetByteSpanCallCount());
                equal(2L, object.GetRefCount());
                desc.ElementSize = 0xffffffffL;
                desc.ElementCount = 0xffffffffL;
                throwsType(IllegalArgumentException.class, () -> GetComputedBufferChecksum(desc));
                equal(calls, GetByteSpanCallCount());
                equal(2L, object.GetRefCount());
                desc.ElementSize = 2;
                desc.ElementCount = 3;
                // Native output conversion retains pObject before its computed
                // byte count or the later UTF-8 label can reject the snapshot.
                throwsType(IllegalArgumentException.class, () -> EchoComputedBuffer(desc, true, false));
                throwsType(IllegalArgumentException.class, () -> EchoComputedBuffer(desc, false, true));
                equal(2L, object.GetRefCount());
                MarshalComputedBufferDesc copy = EchoComputedBuffer(desc, false, false);
                try (IMarshalObject retained = copy.pObject) {
                    equal(3L, object.GetRefCount());
                    equal(object.GetSize(), retained.GetSize());
                }
                equal(2L, object.GetRefCount());
            }
            equal(0L, GetLiveObjectCount());
        });
    }
}
