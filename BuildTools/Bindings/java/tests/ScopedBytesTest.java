// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.lang.management.ManagementFactory;
import java.lang.management.ThreadInfo;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ScopedBytesTest {
    private static IMarshalBytes buffer(byte... data) {
        MarshalBufferDesc desc = new MarshalBufferDesc();
        desc.pBytes = data;
        desc.ByteCount = data.length;
        return CreateMarshalBytes(desc).ppBuffer;
    }

    private static void await(CountDownLatch signal) {
        try { check(signal.await(10, TimeUnit.SECONDS)); }
        catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new AssertionError(error);
        }
    }

    private static void join(Thread thread) {
        try { thread.join(10000); }
        catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new AssertionError(interrupted);
        }
        check(!thread.isAlive());
    }

    private static void awaitScopeMonitor(Thread thread, ReadByteScope access) {
        long deadline = java.lang.System.nanoTime() + TimeUnit.SECONDS.toNanos(10);
        while (thread.isAlive() && java.lang.System.nanoTime() < deadline) {
            ThreadInfo info = ManagementFactory.getThreadMXBean().getThreadInfo(thread.getId());
            if (info != null && info.getThreadState() == Thread.State.BLOCKED &&
                info.getLockOwnerId() == Thread.currentThread().getId() &&
                info.getLockInfo().getIdentityHashCode() == java.lang.System.identityHashCode(access)) return;
            try { Thread.sleep(1); }
            catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new AssertionError(interrupted);
            }
        }
        throw new AssertionError("Direct byte operation did not wait for the scope monitor");
    }

    private static void closeBeforeDirectOperation(Consumer<WriteByteScope> operation) {
        IMarshalBytes owner = buffer((byte)23);
        WriteByteScope access = owner.MapWrite().ppData;
        owner.close();
        CountDownLatch entered = new CountDownLatch(1);
        AtomicReference<Throwable> error = new AtomicReference<>();
        Thread caller = new Thread(() -> {
            entered.countDown();
            try {
                operation.accept(access);
                error.set(new AssertionError("Direct operation succeeded after close won the monitor"));
            } catch (IllegalStateException closed) {
                // Close must win before this blocked direct operation can use storage.
            } catch (Throwable failure) {
                error.set(failure);
            }
        }, "direct-byte-operation");
        caller.setDaemon(true);
        long releases = GetBufferReleaseCount();
        try {
            // Warm up the exact entry point before using its monitor as a gate.
            operation.accept(access);
            synchronized (access) {
                caller.start();
                await(entered);
                awaitScopeMonitor(caller, access);
                access.close();
                equal(releases + 1, GetBufferReleaseCount());
                equal(0L, GetLiveBufferCount());
            }
        } finally {
            // Also safe for a failed synchronization assertion: no native call
            // remains active when its scope is released.
            try { join(caller); }
            finally { if (!caller.isAlive()) access.close(); }
        }
        if (error.get() != null) throw new AssertionError(error.get());
        equal(releases + 1, GetBufferReleaseCount());
        equal(0L, GetLiveBufferCount());
    }

    public static void main(java.lang.String[] args) {
        run("ScopedBytes.ReleaseMethodsAreNotPublicOnBaseOrDerived", () -> {
            for (Class<?> type : new Class<?>[]{IMarshalReadRelease.class, IMarshalBytes.class, IMarshalDerivedBytes.class}) {
                for (java.lang.reflect.Method method : type.getMethods()) {
                    check(!method.getName().equals("UnmapRead"));
                    check(!method.getName().equals("UnmapWrite"));
                }
            }
        });
        run("ScopedBytes.DerivedInterfaceRetainsInheritedScopeUntilClose", () -> {
            long releases = GetBufferReleaseCount();
            try (IMarshalBytes owner = buffer((byte)23);
                 IMarshalDerivedBytes derived = owner.QueryInterface(IMarshalDerivedBytes.class)) {
                try (ReadByteScope access = derived.MapRead().ppData) {
                    equal(1L, owner.ReaderCount());
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, derived.Resize(2));
                    equal(23, access.readByte(0));
                }
                equal(0L, owner.ReaderCount());
                equal(releases + 1, GetBufferReleaseCount());
                equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, derived.Resize(2));
            }
            equal(0L, GetLiveBufferCount());
        });
        run("ScopedBytes.ReadCopiesAndNativeAccessLifetime", () -> {
            long releases = GetBufferReleaseCount();
            try (IMarshalBytes owner = buffer((byte)0, (byte)127, (byte)128, (byte)255)) {
                IMarshalBytes_MapReadResult result = owner.MapRead();
                equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, result.returnValue);
                ReadByteScope access = result.ppData;
                try (access) {
                    equal(1L, owner.ReaderCount());
                    equal(3L, owner.RefCount());
                    check(!(access instanceof WriteByteScope));
                    equal(4L, access.byteLength());
                    equal(0, access.readByte(0));
                    equal(127, access.readByte(1));
                    equal(128, access.readByte(2));
                    equal(255, access.readByte(3));
                    byte[] copied = new byte[2];
                    access.copyTo(copied, 2);
                    check(Arrays.equals(new byte[]{(byte)128, (byte)255}, copied));
                    byte[] snapshot = access.toByteArray();
                    snapshot[0] = 99;
                    equal(0, access.readByte(0));
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, owner.Resize(2));
                    try (ReadByteScope second = owner.MapRead().ppData) {
                        equal(2L, owner.ReaderCount());
                        equal(255, second.readByte(3));
                    }
                    equal(1L, owner.ReaderCount());
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, owner.MapWrite().returnValue);
                    equal(null, owner.MapWrite().ppData);
                }
                equal(0L, owner.ReaderCount());
                equal(releases + 2, GetBufferReleaseCount());
                check(access.isClosed());
                throwsType(IllegalStateException.class, access::byteLength);
                throwsType(IllegalStateException.class, () -> access.readByte(0));
                throwsType(IllegalStateException.class, () -> access.copyTo(new byte[0]));
                throwsType(IllegalStateException.class, access::toByteArray);
                access.close();
                equal(releases + 2, GetBufferReleaseCount());
                equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, owner.Resize(2));
            }
            equal(0L, GetLiveBufferCount());
        });

        run("ScopedBytes.WriteAccessPreservesEveryByte", () -> {
            try (IMarshalBytes owner = buffer(new byte[256])) {
                byte[] snapshot;
                IMarshalBytes_MapWriteResult result = owner.MapWrite();
                // Success is adapter-defined, not restricted to status zero.
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, result.returnValue);
                WriteByteScope access = result.ppData;
                try (access) {
                    equal(true, owner.IsWriting());
                    for (int i = 0; i < 256; ++i) access.writeByte(i, i);
                    byte[] all = new byte[256];
                    access.copyTo(all);
                    for (int i = 0; i < 256; ++i) equal((byte)i, all[i]);
                    access.copyFrom(new byte[]{17, (byte)128, (byte)254}, 12);
                    equal(17, access.readByte(12));
                    equal(254, access.readByte(14));
                    access.copyFrom(new byte[]{(byte)255});
                    equal(255, access.readByte(0));
                    snapshot = access.toByteArray();
                    equal(null, owner.MapRead().ppData);
                    equal(null, owner.MapWrite().ppData);
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, owner.Resize(1));
                }
                equal(false, owner.IsWriting());
                check(Arrays.equals(snapshot, owner.GetData()));
                throwsType(IllegalStateException.class, () -> access.writeByte(0, 1));
                throwsType(IllegalStateException.class, () -> access.copyFrom(new byte[0]));
                owner.Resize(0);
                equal((byte)254, snapshot[14]);
            }
        });

        run("ScopedBytes.EmptyAccessAndRangeValidation", () -> {
            try (IMarshalBytes owner = buffer()) {
                try (ReadByteScope read = owner.MapRead().ppData) {
                    check(read != null);
                    equal(0L, read.byteLength());
                    equal(0, read.toByteArray().length);
                    read.copyTo(new byte[0]);
                    throwsType(IllegalArgumentException.class, () -> read.readByte(0));
                }
                try (WriteByteScope write = owner.MapWrite().ppData) {
                    write.copyFrom(new byte[0]);
                    throwsType(IllegalArgumentException.class, () -> write.writeByte(0, 0));
                }
                owner.Resize(4);
                try (WriteByteScope access = owner.MapWrite().ppData) {
                    for (long offset : new long[]{-1, 4, Long.MAX_VALUE, Long.MIN_VALUE}) {
                        throwsType(IllegalArgumentException.class, () -> access.readByte(offset));
                        throwsType(IllegalArgumentException.class, () -> access.writeByte(offset, 1));
                        throwsType(IllegalArgumentException.class, () -> access.copyTo(new byte[1], offset));
                        throwsType(IllegalArgumentException.class, () -> access.copyFrom(new byte[1], offset));
                    }
                    access.copyFrom(new byte[0], 4);
                    access.copyTo(new byte[0], 4);
                    throwsType(IllegalArgumentException.class, () -> access.copyTo(new byte[2], 3));
                    throwsType(IllegalArgumentException.class, () -> access.copyFrom(new byte[2], 3));
                    throwsType(IllegalArgumentException.class, () -> access.copyTo(null));
                    throwsType(IllegalArgumentException.class, () -> access.copyFrom(null));
                    throwsType(IllegalArgumentException.class, () -> access.writeByte(0, -1));
                    throwsType(IllegalArgumentException.class, () -> access.writeByte(0, 256));
                    check(Arrays.equals(new byte[4], access.toByteArray()));
                }
            }
        });

        run("ScopedBytes.ScopesRetainClosedOwners", () -> {
            IMarshalBytes owner = buffer((byte)23);
            ReadByteScope first = owner.MapRead().ppData;
            ReadByteScope second = null;
            try {
                second = owner.MapRead().ppData;
                owner.close();
                equal(1L, GetLiveBufferCount());
                equal(23, first.readByte(0));
                first.close();
                equal(1L, GetLiveBufferCount());
                equal(23, second.readByte(0));
            } finally {
                first.close();
                if (second != null) second.close();
                owner.close();
            }
            equal(0L, GetLiveBufferCount());
        });

        run("ScopedBytes.ReleaseErrorsStillConsumeAccessExactlyOnce", () -> {
            long releases = GetBufferReleaseCount();
            try (IMarshalBytes owner = buffer((byte)5)) {
                owner.SetReleaseStatus(MARSHAL_STATUS.MARSHAL_STATUS_FAILED);
                ReadByteScope access = owner.MapRead().ppData;
                try (access) {
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, access.release());
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, access.release());
                    equal(0L, owner.ReaderCount());
                }
                equal(releases + 1, GetBufferReleaseCount());
                throwsType(IllegalStateException.class, () -> access.readByte(0));
                throwsType(IllegalStateException.class, () -> {
                    try (WriteByteScope write = owner.MapWrite().ppData) {
                        write.writeByte(0, 17);
                        throw new IllegalStateException("Client operation failed");
                    }
                });
                equal(false, owner.IsWriting());
                equal(releases + 2, GetBufferReleaseCount());
                equal((byte)17, owner.GetData()[0]);
            }
        });

        run("ScopedBytes.InvalidAcquisitionsRollBackNativeAccess", () -> {
            long releases = GetBufferReleaseCount();
            try (IMarshalBytes owner = buffer()) {
                // An acquired null pointer is valid only for an empty span.
                owner.SetReportedSize(1);
                throwsType(IllegalArgumentException.class, owner::MapRead);
                equal(0L, owner.ReaderCount());
                equal(2L, owner.RefCount());
                equal(releases + 1, GetBufferReleaseCount());
                owner.Resize(1);
                owner.SetReportedSize(-1); // UINT64_MAX cannot become a native range.
                throwsType(IllegalArgumentException.class, owner::MapWrite);
                equal(false, owner.IsWriting());
                equal(2L, owner.RefCount());
                equal(releases + 2, GetBufferReleaseCount());
                owner.SetReportedSize(0x80000000L);
                try (ReadByteScope read = owner.MapRead().ppData) {
                    equal(0x80000000L, read.byteLength());
                    throwsType(IllegalArgumentException.class, read::toByteArray);
                    // Do not read outside the fixture's actual one-byte storage.
                }
            }
            equal(0L, GetLiveBufferCount());
        });

        run("ScopedBytes.DirectReadByteVersusClose", () ->
            closeBeforeDirectOperation(access -> equal(23, access.readByte(0))));
        run("ScopedBytes.DirectCopyToVersusClose", () ->
            closeBeforeDirectOperation(access -> {
                byte[] copy = new byte[1];
                access.copyTo(copy, 0);
                equal((byte)23, copy[0]);
            }));
        run("ScopedBytes.DirectWriteByteVersusClose", () ->
            closeBeforeDirectOperation(access -> access.writeByte(0, 23)));
        run("ScopedBytes.DirectCopyFromVersusClose", () ->
            closeBeforeDirectOperation(access -> access.copyFrom(new byte[]{23}, 0)));

        run("ScopedBytes.ConcurrentCloseCannotInvalidateAnActiveCopy", () -> {
            byte[] data = new byte[64 * 1024];
            Arrays.fill(data, (byte)0xA5);
            IMarshalBytes owner = buffer(data);
            ReadByteScope access = owner.MapRead().ppData;
            owner.close();
            CountDownLatch copied = new CountDownLatch(1);
            AtomicReference<Throwable> error = new AtomicReference<>();
            Thread reader = new Thread(() -> {
                try {
                    for (;;) {
                        byte[] copy = access.toByteArray();
                        check(Arrays.equals(data, copy));
                        copied.countDown();
                    }
                } catch (IllegalStateException closed) {
                    // Close is serialized with the complete allocation and copy.
                } catch (Throwable failure) {
                    error.set(failure);
                    copied.countDown();
                }
            }, "byte-scope-reader");
            reader.setDaemon(true);
            try {
                reader.start();
                await(copied);
                access.close();
            } finally {
                access.close();
                try { reader.join(10000); }
                catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new AssertionError(interrupted);
                }
            }
            check(!reader.isAlive());
            if (error.get() != null) throw new AssertionError(error.get());
            equal(0L, GetLiveBufferCount());
        });
    }
}
