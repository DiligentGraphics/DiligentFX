// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Arrays;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class RawBytesTest {
    private static MarshalBufferDesc descriptor(byte[] data, long count) {
        MarshalBufferDesc result = new MarshalBufferDesc();
        result.pBytes = data;
        result.ByteCount = count;
        return result;
    }

    private static IMarshalBytes buffer(byte[] data) {
        return CreateMarshalBytes(descriptor(data, data.length)).ppBuffer;
    }

    public static void main(java.lang.String[] args) {
        run("RawBytes.AllByteValuesAndSelectedPrefixes", () -> {
            byte[] data = new byte[256];
            long checksum = 0;
            for (int i = 0; i < data.length; ++i) {
                data[i] = (byte)i;
                checksum = (checksum * 31 + i) & 0xFFFFFFFFL;
            }
            equal(checksum, ChecksumBytes(data, data.length, false));
            equal(checksum ^ 0xFFFFFFFFL, ChecksumBytes(data, data.length, true));
            byte[] saved = EchoByteSpan(data, data.length);
            check(saved != data);
            check(Arrays.equals(data, saved));
            check(Arrays.equals(new byte[]{0, 1, 2}, EchoByteSpan(data, 3)));
            data[255] = 0;
            equal((byte)-1, saved[255]);
            // Typed Uint8 arrays retain their numeric short[] representation.
            equal(true, CheckBytes(3, new short[]{17, 128, 254}));
            throwsType(IllegalArgumentException.class, () -> CheckBytes(3, new short[]{17, -128, 254}));
        });

        run("RawBytes.NullAndRequiredEmptyStorage", () -> {
            equal(null, EchoByteSpan(null, 0));
            equal(null, EchoByteSpan(null, 7));
            equal(0, EchoByteSpan(new byte[0], 0).length);
            equal(0L, ChecksumBytes(null, 0, false));
            // Required empty inputs must still supply a real native pointer.
            equal(true, ByteSpanHasStorage(null, 0));
            equal(true, ByteSpanHasStorage(new byte[0], 0));
            equal(true, ByteSpanHasStorage(new byte[]{1}, 0));
            MarshalBufferDesc defaults = new MarshalBufferDesc();
            equal(null, defaults.pBytes);
            equal(null, EchoBufferDesc(defaults).pBytes);
            defaults.pBytes = new byte[0];
            equal(0, EchoBufferDesc(defaults).pBytes.length);
            defaults.pBytes = null;
            defaults.ByteCount = 3;
            try (IMarshalBytes zeroed = CreateMarshalBytes(defaults).ppBuffer) {
                check(Arrays.equals(new byte[3], zeroed.GetData()));
            }
            equal(0L, GetLiveBufferCount());
        });

        run("RawBytes.DescriptorsAndOwnerSnapshots", () -> {
            byte[] input = {17, (byte)128, (byte)254, 99};
            MarshalBufferDesc desc = descriptor(input, 3);
            MarshalBufferDesc copy = EchoBufferDesc(desc);
            byte[] saved;
            try (IMarshalBytes source = CreateMarshalBytes(desc).ppBuffer) {
                input[0] = 0;
                saved = source.GetData();
                check(Arrays.equals(new byte[]{17, (byte)128, (byte)254}, saved));
                check(Arrays.equals(saved, copy.pBytes));
                equal(3L, copy.ByteCount);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, source.Resize(1));
                check(Arrays.equals(new byte[]{17}, source.GetData()));
                equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, source.Resize(0));
                equal(null, source.GetData());
            }
            equal(0L, GetLiveBufferCount());
            equal((byte)254, saved[2]);
            equal((byte)128, copy.pBytes[1]);
        });

        run("RawBytes.FilledOutputsAndUnwrittenTails", () -> {
            byte[] prefix = {17, (byte)128, (byte)254};
            for (int capacity : new int[]{0, 1, 2, 8}) {
                FillByteSpanResult result = FillByteSpan(capacity, false);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, result.returnValue);
                equal((long)Math.min(capacity, 3), result.Written);
                check(Arrays.equals(Arrays.copyOf(prefix, Math.min(capacity, 3)), result.pBytes));
                FillByteCapacityResult full = FillByteCapacity(capacity, false);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, full.returnValue);
                check(Arrays.equals(Arrays.copyOf(prefix, capacity), full.pBytes));
                FillByteCapacityResult failed = FillByteCapacity(capacity, true);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, failed.returnValue);
                check(Arrays.equals(new byte[capacity], failed.pBytes));
            }
            throwsType(IllegalArgumentException.class, () -> FillByteSpan(2, true));
            byte[] large = FillByteCapacity(512 * 1024, false).pBytes;
            equal(512 * 1024, large.length);
            equal((byte)254, large[2]);
            equal((byte)0, large[large.length - 1]);
        });

        run("RawBytes.ComputedCountsAndLargeCounts", () -> {
            byte[] data = {0, 127, (byte)128, (byte)255};
            try (IMarshalBytes source = buffer(data)) {
                check(Arrays.equals(data, CopyBufferBytes(source).pBytes));
                long calls = GetByteSpanCallCount();
                for (long count : new long[]{0x80000000L, -1L}) {
                    source.SetReportedSize(count);
                    throwsType(IllegalArgumentException.class, source::GetData);
                    throwsType(IllegalArgumentException.class, () -> CopyBufferBytes(source));
                }
                equal(calls, GetByteSpanCallCount());
                source.SetReportedSize(data.length);
                check(Arrays.equals(data, source.GetData()));
            }
            equal(0L, GetLiveBufferCount());
        });

        run("RawBytes.InvalidSpansDoNotReachNativeCode", () -> {
            byte[] data = {1, 2};
            long calls = GetByteSpanCallCount();
            throwsType(IllegalArgumentException.class, () -> ChecksumBytes(null, 1, false));
            for (long count : new long[]{3, 0x80000000L, -1L}) {
                throwsType(IllegalArgumentException.class, () -> ChecksumBytes(data, count, false));
                throwsType(IllegalArgumentException.class, () -> EchoBufferDesc(descriptor(data, count)));
            }
            for (long capacity : new long[]{0x80000000L, -1L}) {
                throwsType(IllegalArgumentException.class, () -> FillByteSpan(capacity, false));
                throwsType(IllegalArgumentException.class, () -> FillByteCapacity(capacity, false));
            }
            equal(calls, GetByteSpanCallCount());
        });

        run("RawBytes.NestedSnapshotsAndFailureCleanup", () -> {
            MarshalDesc objectDesc = new MarshalDesc();
            objectDesc.Size = 23;
            IMarshalObject object = CreateMarshalObject(objectDesc, 0, false).ppObject;
            MarshalBytePacket saved = null;
            try {
                MarshalBytePacket packet = new MarshalBytePacket();
                packet.pObject = object;
                packet.Buffer = descriptor(new byte[]{0, (byte)255}, 2);
                packet.Label = "\uD800";
                long calls = GetByteSpanCallCount();
                throwsType(IllegalArgumentException.class, () -> EchoBytePacket(packet, false));
                equal(calls, GetByteSpanCallCount());
                equal(2L, object.GetRefCount());
                packet.Label = "valid";
                for (int i = 0; i < 3; ++i) {
                    throwsType(IllegalArgumentException.class, () -> EchoBytePacket(packet, true));
                    equal(2L, object.GetRefCount());
                    equal(1L, GetLiveObjectCount());
                }
                saved = EchoBytePacket(packet, false);
                equal(3L, object.GetRefCount());
                packet.Buffer.pBytes[1] = 0;
                object.close();
                equal(23L, saved.pObject.GetSize());
                check(Arrays.equals(new byte[]{0, (byte)255}, saved.Buffer.pBytes));
            } finally {
                object.close();
                if (saved != null) saved.pObject.close();
            }
            equal(0L, GetLiveObjectCount());
        });
    }
}
