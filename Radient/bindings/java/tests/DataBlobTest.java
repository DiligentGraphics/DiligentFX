// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class DataBlobTest {
    private static RadientDataBlobCreateInfo info(byte[] data) {
        RadientDataBlobCreateInfo result = new RadientDataBlobCreateInfo();
        result.Size = data.length;
        result.pData = data;
        return result;
    }

    private static byte[] read(IRadientDataBlob blob) {
        var result = blob.BeginRead();
        try (ReadByteScope scope = result.ppData) {
            ok(result.returnValue);
            check(scope != null);
            equal(blob.GetSize(), scope.byteLength());
            return scope.toByteArray();
        }
    }

    @SuppressWarnings("try")
    public static void main(String[] args) {
        run("DataBlob.CopiesInputAndSupportsReadOnlyQueries", () -> {
            byte[] data = {17, (byte)128, (byte)254};
            var result = CreateRadientDataBlob(info(data));
            try (IRadientDataBlob blob = result.ppBlob) {
                ok(result.returnValue);
                check(blob != null);
                data[0] = 0;
                equal(new byte[]{17, (byte)128, (byte)254}, read(blob));
                try (IRadientMutableDataBlob mutable = blob.QueryInterface(IRadientMutableDataBlob.class)) {
                    equal(null, mutable);
                }
                try (IRadientDataBlob retained = blob.QueryInterface(IRadientDataBlob.class)) {
                    check(retained != null);
                    blob.close();
                    equal(3L, retained.GetSize());
                }
                throwsType(IllegalStateException.class, blob::GetSize);
            }
        });

        run("DataBlob.EmptyScopesAndZeroInitialization", () -> {
            RadientDataBlobCreateInfo info = new RadientDataBlobCreateInfo();
            var empty = CreateRadientMutableDataBlob(info);
            try (IRadientMutableDataBlob blob = empty.ppBlob) {
                ok(empty.returnValue);
                equal(new byte[0], read(blob));
                var acquired = blob.BeginWrite();
                try (WriteByteScope scope = acquired.ppData) {
                    ok(acquired.returnValue);
                    check(scope != null);
                    equal(0L, scope.byteLength());
                    scope.copyFrom(new byte[0]);
                    var blocked = blob.BeginRead();
                    try (ReadByteScope unused = blocked.ppData) {
                        equal(RADIENT_STATUS_INVALID_OPERATION, blocked.returnValue);
                        equal(null, unused);
                    }
                }
                ok(blob.Resize(4));
                equal(new byte[4], read(blob));
            }
            info.Size = 4;
            var initialized = CreateRadientDataBlob(info);
            try (IRadientDataBlob blob = initialized.ppBlob) {
                ok(initialized.returnValue);
                equal(new byte[4], read(blob));
            }
        });

        run("DataBlob.AccessExclusionResizeAndScopeLifetime", () -> {
            var result = CreateRadientMutableDataBlob(info(new byte[]{17, (byte)128, (byte)254}));
            try (IRadientMutableDataBlob blob = result.ppBlob) {
                ok(result.returnValue);
                var acquired = blob.BeginRead();
                try (ReadByteScope scope = acquired.ppData) {
                    ok(acquired.returnValue);
                    equal(RADIENT_STATUS_INVALID_OPERATION, blob.Resize(6));
                    var blocked = blob.BeginWrite();
                    try (WriteByteScope unused = blocked.ppData) {
                        equal(RADIENT_STATUS_INVALID_OPERATION, blocked.returnValue);
                        equal(null, unused);
                    }
                }
                ok(blob.Resize(6));
                equal(new byte[]{17, (byte)128, (byte)254, 0, 0, 0}, read(blob));
                ok(blob.Resize(1));
                ok(blob.Resize(3));
                equal(new byte[]{17, 0, 0}, read(blob));
                var writable = blob.BeginWrite();
                try (WriteByteScope scope = writable.ppData) {
                    ok(writable.returnValue);
                    blob.close(); // The scope independently retains the real native blob.
                    scope.copyFrom(new byte[]{(byte)255, 42, 7});
                    equal(255, scope.readByte(0));
                    equal(new byte[]{(byte)255, 42, 7}, scope.toByteArray());
                    ok(scope.release());
                    ok(scope.release());
                    throwsType(IllegalStateException.class, () -> scope.readByte(0));
                }
            }
        });

        run("DataBlob.InvalidInputDoesNotReachNativeAllocation", () -> {
            RadientDataBlobCreateInfo invalid = info(new byte[]{1});
            invalid.Size = 2;
            throwsType(IllegalArgumentException.class, () -> CreateRadientDataBlob(invalid));
            invalid.Size = -1L;
            throwsType(IllegalArgumentException.class, () -> CreateRadientMutableDataBlob(invalid));
        });
    }
}
