// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

// Case names and scenarios mirror data_blob_tests.mjs. Java-specific type checks
// replace dynamic-JavaScript argument checks; Wasm memory growth remains JS-only.
public final class DataBlobTest {
    private static RadientDataBlobCreateInfo info(byte[] data) {
        RadientDataBlobCreateInfo result = new RadientDataBlobCreateInfo();
        result.Size = data.length;
        result.pData = data;
        return result;
    }

    private static IRadientDataBlob blob(Resources r, boolean mutable, RadientDataBlobCreateInfo info) {
        if (mutable) {
            var result = CreateRadientMutableDataBlob(info);
            r.keep(result.ppBlob);
            ok(result.returnValue);
            check(result.ppBlob != null);
            return result.ppBlob;
        }
        var result = CreateRadientDataBlob(info);
        r.keep(result.ppBlob);
        ok(result.returnValue);
        check(result.ppBlob != null);
        return result.ppBlob;
    }

    private static ReadByteScope read(Resources r, IRadientDataBlob blob) {
        var result = blob.BeginRead();
        r.keep(result.ppData);
        ok(result.returnValue);
        check(result.ppData != null);
        equal(blob.GetSize(), result.ppData.byteLength());
        return result.ppData;
    }

    private static WriteByteScope write(Resources r, IRadientMutableDataBlob blob) {
        var result = blob.BeginWrite();
        r.keep(result.ppData);
        ok(result.returnValue);
        check(result.ppData != null);
        equal(blob.GetSize(), result.ppData.byteLength());
        return result.ppData;
    }

    private static void blockedRead(Resources r, IRadientDataBlob blob) {
        var result = blob.BeginRead();
        r.keep(result.ppData);
        equal(RADIENT_STATUS_INVALID_OPERATION, result.returnValue);
        equal(null, result.ppData);
    }

    private static void blockedWrite(Resources r, IRadientMutableDataBlob blob) {
        var result = blob.BeginWrite();
        r.keep(result.ppData);
        equal(RADIENT_STATUS_INVALID_OPERATION, result.returnValue);
        equal(null, result.ppData);
    }

    public static void main(String[] args) {
        run("owning factories copy initial data from the selected array view", () -> {
            try (Resources r = new Resources()) {
                for (boolean mutable : new boolean[]{false, true}) {
                    byte[] source = Fixtures.bytes("blob.source");
                    // Java byte[] has no subarray view: explicitly select the same bytes.
                    byte[] selected = Arrays.copyOfRange(source, 1, 4);
                    var object = blob(r, mutable, info(selected));
                    Arrays.fill(source, (byte)0);
                    Arrays.fill(selected, (byte)0);
                    equal(3L, object.GetSize());
                    equal(Fixtures.bytes("blob.initial"), read(r, object).toByteArray());
                }
            }
        });

        run("omitted and null initial data use Radient's zero initialization", () -> {
            try (Resources r = new Resources()) {
                for (boolean mutable : new boolean[]{false, true}) {
                    for (boolean explicitNull : new boolean[]{false, true}) {
                        RadientDataBlobCreateInfo data = new RadientDataBlobCreateInfo();
                        data.Size = 4;
                        if (explicitNull) data.pData = null;
                        equal(new byte[4], read(r, blob(r, mutable, data)).toByteArray());
                    }
                }
            }
        });

        run("default empty blobs have successful read and write scopes", () -> {
            try (Resources r = new Resources()) {
                var empty = blob(r, false, new RadientDataBlobCreateInfo());
                equal(0L, empty.GetSize());
                var reader = read(r, empty);
                equal(0L, reader.byteLength());
                equal(new byte[0], reader.toByteArray());
                var mutable = (IRadientMutableDataBlob)blob(r, true, new RadientDataBlobCreateInfo());
                // Preserve the previous Java check of a mutable empty read scope.
                var emptyRead = read(r, mutable);
                equal(new byte[0], emptyRead.toByteArray());
                emptyRead.close();
                var writer = write(r, mutable);
                equal(0L, writer.byteLength());
                writer.copyFrom(new byte[0]);
                blockedRead(r, mutable);
                writer.close();
                reader = read(r, mutable);
                equal(0L, reader.byteLength());
                reader.close();
                ok(mutable.Resize(4));
                equal(new byte[4], read(r, mutable).toByteArray());
            }
        });

        run("native-only callbacks, context, and reference storage are not exposed", () -> {
            String[] fields = Arrays.stream(RadientDataBlobCreateInfo.class.getFields()).map(f -> f.getName()).sorted().toArray(String[]::new);
            equal(new String[]{"Size", "pData"}, fields);
            check(Arrays.stream(diligent.radient.Radient.class.getClasses())
                .noneMatch(c -> c.getSimpleName().equals("RADIENT_DATA_BLOB_STORAGE_MODE")));
        });

        run("invalid byte ranges fail before entering the native factory", () -> {
            RadientDataBlobCreateInfo invalid = info(new byte[3]);
            invalid.Size = 4;
            throwsType(IllegalArgumentException.class, () -> CreateRadientDataBlob(invalid));
            throwsType(IllegalArgumentException.class, () -> CreateRadientMutableDataBlob(invalid));
            // A Java long carries the unsigned bits; with a non-null input array,
            // UINT64_MAX is rejected by marshalling before native code reads it.
            invalid.Size = -1L;
            throwsType(IllegalArgumentException.class, () -> CreateRadientDataBlob(invalid));
            throwsType(IllegalArgumentException.class, () -> CreateRadientMutableDataBlob(invalid));
        });

        run("native allocation-size rejection returns status and a null output", () -> {
            try (Resources r = new Resources()) {
                RadientDataBlobCreateInfo invalid = new RadientDataBlobCreateInfo();
                invalid.Size = -1L; // UINT64_MAX exceeds vector::max_size on native and Wasm.
                var immutable = CreateRadientDataBlob(invalid);
                r.keep(immutable.ppBlob);
                equal(RADIENT_STATUS_INVALID_ARGUMENT, immutable.returnValue);
                equal(null, immutable.ppBlob);
                var mutable = CreateRadientMutableDataBlob(invalid);
                r.keep(mutable.ppBlob);
                equal(RADIENT_STATUS_INVALID_ARGUMENT, mutable.returnValue);
                equal(null, mutable.ppBlob);
            }
        });

        run("read-only native blobs do not provide a mutable interface", () -> {
            try (Resources r = new Resources()) {
                RadientDataBlobCreateInfo data = new RadientDataBlobCreateInfo();
                data.Size = 2;
                var object = blob(r, false, data);
                equal(null, r.keep(object.QueryInterface(IRadientMutableDataBlob.class)));
                var reader = read(r, object);
                check(Arrays.stream(reader.getClass().getMethods())
                    .noneMatch(m -> m.getName().equals("copyFrom") || m.getName().equals("writeByte")));
                check(Arrays.stream(object.getClass().getMethods()).noneMatch(m -> m.getName().equals("EndRead")));
                // Retain the former Java-only base-query lifetime check in both languages.
                var retained = r.keep(object.QueryInterface(IRadientDataBlob.class));
                check(retained != null);
                reader.close();
                object.close();
                equal(2L, retained.GetSize());
                throwsType(IllegalStateException.class, object::GetSize);
            }
        });

        run("queried interface views share native access state", () -> {
            try (Resources r = new Resources()) {
                RadientDataBlobCreateInfo data = new RadientDataBlobCreateInfo();
                data.Size = 4;
                var object = (IRadientMutableDataBlob)blob(r, true, data);
                check(object instanceof IRadientDataBlob);
                var base = r.keep(object.QueryInterface(IRadientDataBlob.class));
                check(base != null);
                var queried = r.keep(base.QueryInterface(IRadientMutableDataBlob.class));
                check(queried != null);
                var first = read(r, base);
                var second = read(r, queried);
                blockedWrite(r, object);
                equal(RADIENT_STATUS_INVALID_OPERATION, object.Resize(4));
                first.close();
                blockedWrite(r, object);
                second.close();
                var writer = write(r, object);
                blockedRead(r, base);
                blockedWrite(r, queried);
                writer.copyFrom(Fixtures.bytes("blob.written"));
                writer.close();
                equal(Fixtures.bytes("blob.written"), read(r, base).toByteArray());
            }
        });

        run("resize preserves data, zeros added bytes, and discards truncated data", () -> {
            try (Resources r = new Resources()) {
                var object = (IRadientMutableDataBlob)blob(r, true, info(Fixtures.bytes("blob.initial")));
                var reader = read(r, object);
                equal(RADIENT_STATUS_INVALID_OPERATION, object.Resize(6));
                blockedWrite(r, object);
                reader.close();
                ok(object.Resize(6));
                reader = read(r, object);
                equal(Fixtures.bytes("blob.grown"), reader.toByteArray());
                reader.close();
                ok(object.Resize(1));
                ok(object.Resize(3));
                reader = read(r, object);
                equal(Fixtures.bytes("blob.truncated"), reader.toByteArray());
                reader.close();
                equal(RADIENT_STATUS_INVALID_ARGUMENT, object.Resize(-1L));
                equal(3L, object.GetSize());
                reader = read(r, object);
                equal(Fixtures.bytes("blob.truncated"), reader.toByteArray());
                reader.close();
                ok(object.Resize(0));
                equal(0L, read(r, object).byteLength());
            }
        });

        run("scopes retain real blobs after all interface wrappers are disposed", () -> {
            try (Resources r = new Resources()) {
                RadientDataBlobCreateInfo data = new RadientDataBlobCreateInfo();
                data.Size = 2;
                var object = (IRadientMutableDataBlob)blob(r, true, data);
                var writer = write(r, object);
                object.close();
                byte[] initial = Arrays.copyOf(Fixtures.bytes("blob.initial"), 2);
                writer.copyFrom(initial);
                equal(initial, writer.toByteArray());
                ok(writer.release());
                ok(writer.release());
                throwsType(IllegalStateException.class, () -> writer.readByte(0));
                throwsType(IllegalStateException.class, object::GetSize);
                var immutable = blob(r, false, info(new byte[]{(byte)254}));
                var reader = read(r, immutable);
                immutable.close();
                equal(254, reader.readByte(0));
                // Keep the previous Java regression's 255 byte and three-byte copy.
                var extra = (IRadientMutableDataBlob)blob(r, true, info(Fixtures.bytes("blob.initial")));
                var extraWriter = write(r, extra);
                extra.close();
                extraWriter.copyFrom(Fixtures.bytes("blob.retained"));
                equal(255, extraWriter.readByte(0));
                equal(Fixtures.bytes("blob.retained"), extraWriter.toByteArray());
            }
        });

        run("finally releases native write access when client processing fails", () -> {
            try (Resources r = new Resources()) {
                RadientDataBlobCreateInfo data = new RadientDataBlobCreateInfo();
                data.Size = 1;
                var object = (IRadientMutableDataBlob)blob(r, true, data);
                RuntimeException expected = new RuntimeException("processing failed");
                try {
                    try (WriteByteScope writer = write(r, object)) {
                        writer.writeByte(0, 17);
                        throw expected;
                    }
                } catch (RuntimeException error) {
                    check(error == expected);
                }
                equal(17, read(r, object).readByte(0));
            }
        });
    }
}
