// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class InterfaceFieldsTest {
    private static IMarshalObject create(long size) {
        MarshalDesc desc = new MarshalDesc();
        desc.Size = size;
        return CreateMarshalObject(desc, MARSHAL_STATUS.MARSHAL_STATUS_OK, true).ppObject;
    }

    private static MarshalInterfaceDesc descriptor(IMarshalObject object, IMarshalObject... elements) {
        MarshalInterfaceDesc desc = new MarshalInterfaceDesc();
        desc.pObject = object;
        desc.ppObjects = elements;
        desc.Count = elements.length;
        return desc;
    }

    private static void close(IMarshalObject[] objects) {
        if (objects != null) for (IMarshalObject object : objects) if (object != null) object.close();
    }

    private static void close(MarshalInterfaceDesc desc) {
        if (desc != null) {
            if (desc.pObject != null) desc.pObject.close();
            close(desc.ppObjects);
        }
    }

    private static void close(MarshalInterfaceGroup group) {
        if (group != null) {
            close(group.Primary);
            for (MarshalInterfaceDesc entry : group.Entries) close(entry);
        }
    }

    // Close sources early to check that snapshots and active JNI calls own independent refs.
    @SuppressWarnings("try")
    public static void main(java.lang.String[] args) {
        run("InterfaceFields.SnapshotsRetainFieldsAndArrayElements", () -> {
            long destroyed = GetDestructionCount();
            MarshalInterfaceDesc snapshot = null;
            try (IMarshalObject a = create(7); IMarshalObject b = create(11);
                 IMarshalMutableObject mutable = b.QueryInterface(IMarshalMutableObject.class)) {
                MarshalInterfaceDesc input = descriptor(a, a, null, mutable);
                equal(25L, SumInterfaces(input));
                equal(11L, SumInterfaceArray(new IMarshalMutableObject[]{mutable}, 1));
                equal(2L, a.GetRefCount());
                equal(3L, b.GetRefCount());
                snapshot = EchoInterfaces(input);
                check(snapshot != input);
                check(snapshot.pObject != a);
                check(snapshot.ppObjects[0] != snapshot.pObject);
                equal(null, snapshot.ppObjects[1]);
                equal(4L, a.GetRefCount());
                equal(4L, b.GetRefCount());
                a.close(); b.close(); mutable.close();
                equal(7L, snapshot.pObject.GetSize());
                equal(7L, snapshot.ppObjects[0].GetSize());
                equal(11L, snapshot.ppObjects[2].GetSize());
                equal(2L, GetLiveObjectCount());
            } finally { close(snapshot); }
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 2, GetDestructionCount());
        });

        run("InterfaceFields.BorrowedArrayReturnsAndEmptyStorage", () -> {
            IMarshalObject[] result = null;
            try (IMarshalObject a = create(17); IMarshalObject b = create(19)) {
                IMarshalObject[] input = {a, null, a, b};
                equal(17L, SumInterfaceArray(input, 1));
                result = EchoInterfaceArray(input, 3);
                equal(3, result.length);
                check(result[0] != result[2]);
                equal(null, result[1]);
                equal(4L, a.GetRefCount());
                equal(2L, b.GetRefCount());
                a.close();
                equal(17L, result[2].GetSize());
                equal(true, InterfaceArrayHasStorage(null, 0));
                equal(true, InterfaceArrayHasStorage(new IMarshalObject[0], 0));
                equal(0, EchoInterfaceArray(null, 0).length);
                MarshalInterfaceDesc empty = new MarshalInterfaceDesc();
                MarshalInterfaceDesc absent = EchoInterfaces(empty);
                equal(null, absent.pObject);
                equal(null, absent.ppObjects);
                empty.ppObjects = new IMarshalObject[0];
                equal(0, EchoInterfaces(empty).ppObjects.length);
            } finally { close(result); }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceFields.CallerFilledArraysRetainBorrowedOutputs", () -> {
            IMarshalObject[] result = null;
            try (IMarshalObject object = create(21)) {
                CopyInterfaceArrayResult copied = CopyInterfaceArray(new IMarshalObject[]{object, null, object}, 3, 2);
                result = copied.ppOutput;
                equal(2L, copied.Written);
                equal(2, result.length);
                equal(null, result[1]);
                equal(3L, object.GetRefCount());
                equal(0, CopyInterfaceArray(null, 0, 4).ppOutput.length);
                object.close();
                equal(21L, result[0].GetSize());
            } finally { close(result); }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceFields.InvalidInputsReleaseEarlierReferences", () -> {
            try (IMarshalObject object = create(23); IMarshalObject closed = create(29)) {
                closed.close();
                MarshalInterfaceDesc desc = descriptor(object, object, closed);
                throwsType(IllegalStateException.class, () -> SumInterfaces(desc));
                equal(2L, object.GetRefCount());
                throwsType(IllegalStateException.class, () -> SumInterfaceArray(new IMarshalObject[]{object, closed}, 2));
                equal(2L, object.GetRefCount());
                desc.ppObjects = new IMarshalObject[]{object};
                throwsType(IllegalArgumentException.class, () -> SumInterfaces(desc));
                desc.Count = 1;
                desc.Label = "\uD800";
                throwsType(IllegalArgumentException.class, () -> SumInterfaces(desc));
                desc.Label = null;
                throwsType(IllegalArgumentException.class, () -> SumInterfaces(desc));
                throwsType(IllegalArgumentException.class, () -> SumInterfaceArray(null, 1));
                throwsType(IllegalArgumentException.class, () -> SumInterfaceArray(new IMarshalObject[]{object}, 0xFFFFFFFFL));
                equal(2L, object.GetRefCount());
                equal(1L, GetLiveObjectCount());
            }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceFields.RequiredFieldsAndCountAfterPointer", () -> {
            MarshalRequiredInterfaceDesc desc = new MarshalRequiredInterfaceDesc();
            equal(null, desc.pObject);
            equal(null, desc.ppObjects);
            throwsType(IllegalArgumentException.class, () -> EchoRequiredInterfaces(desc));
            try (IMarshalObject object = create(31)) {
                desc.pObject = object;
                desc.ppObjects = new IMarshalObject[]{object, null};
                desc.Count = 2;
                throwsType(IllegalArgumentException.class, () -> EchoRequiredInterfaces(desc));
                equal(2L, object.GetRefCount());
                desc.Count = 1;
                MarshalRequiredInterfaceDesc result = EchoRequiredInterfaces(desc);
                try {
                    equal(1, result.ppObjects.length);
                    equal(31L, result.ppObjects[0].GetSize());
                    equal(31L, result.pObject.GetSize());
                } finally { result.pObject.close(); close(result.ppObjects); }
                desc.Count = 0;
                desc.ppObjects = null;
                result = EchoRequiredInterfaces(desc);
                try { equal(0, result.ppObjects.length); }
                finally { result.pObject.close(); close(result.ppObjects); }
                equal(2L, object.GetRefCount());
            }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceFields.FailedSnapshotsRollBackAllReferences", () -> {
            long destroyed = GetDestructionCount();
            try (IMarshalObject a = create(37); IMarshalObject b = create(41)) {
                MarshalInterfaceDesc desc = descriptor(a, a, null, b);
                for (int i = 0; i < 3; ++i) {
                    throwsType(IllegalArgumentException.class, () -> GetInvalidInterfaceSnapshot(desc));
                    throwsType(IllegalArgumentException.class, () -> GetRequiredInterfaceSnapshot(desc));
                    throwsType(IllegalArgumentException.class, () -> GetRequiredInterfaceArray(desc.ppObjects, desc.Count));
                    equal(2L, a.GetRefCount());
                    equal(2L, b.GetRefCount());
                    equal(2L, GetLiveObjectCount());
                    equal(destroyed, GetDestructionCount());
                }
            }
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 2, GetDestructionCount());
        });

        run("InterfaceFields.NestedAndFixedArraySnapshotsShareRollback", () -> {
            MarshalInterfaceGroup snapshot = null;
            try (IMarshalObject object = create(43)) {
                MarshalInterfaceGroup group = new MarshalInterfaceGroup();
                group.Primary = descriptor(object, object);
                group.Entries[0] = descriptor(object, object, null);
                group.Entries[1] = descriptor(null, object);
                throwsType(IllegalArgumentException.class, () -> EchoInterfaceGroup(group, true));
                equal(2L, object.GetRefCount());
                snapshot = EchoInterfaceGroup(group, false);
                equal(7L, object.GetRefCount());
                object.close();
                equal(43L, snapshot.Primary.pObject.GetSize());
                equal(43L, snapshot.Entries[1].ppObjects[0].GetSize());
                equal(null, snapshot.Entries[0].ppObjects[1]);
                equal("interface group", snapshot.Label);
            } finally { close(snapshot); }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceFields.LargeSnapshotsBoundTemporaryJNIReferences", () -> {
            IMarshalObject[] result = null;
            try (IMarshalObject object = create(47)) {
                IMarshalObject[] input = new IMarshalObject[1024];
                java.util.Arrays.fill(input, object);
                result = EchoInterfaceArray(input, input.length);
                equal(1024, result.length);
                equal(1026L, object.GetRefCount());
                object.close();
                equal(47L, result[1023].GetSize());
            } finally { close(result); }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceFields.ActiveCallSurvivesConcurrentClose", () -> {
            long destroyed = GetDestructionCount();
            IMarshalObject a = create(53);
            IMarshalObject b = create(59);
            MarshalInterfaceDesc desc = descriptor(a, a, null, b);
            AtomicLong result = new AtomicLong();
            AtomicReference<Throwable> error = new AtomicReference<>();
            ResetCallGate();
            Thread reader = new Thread(() -> {
                try { result.set(SumInterfacesBlocked(desc)); }
                catch (Throwable failure) { error.set(failure); }
            }, "binding-interface-fields-reader");
            reader.setDaemon(true);
            try {
                reader.start();
                check(WaitForCall(10000));
                a.close(); b.close();
                equal(2L, GetLiveObjectCount());
                equal(destroyed, GetDestructionCount());
            } finally {
                ReleaseCall();
                try { reader.join(10000); }
                catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new AssertionError(interrupted);
                } finally {
                    if (!reader.isAlive()) { a.close(); b.close(); }
                }
            }
            check(!reader.isAlive());
            if (error.get() != null) throw new AssertionError("Native call failed", error.get());
            equal(165L, result.get());
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 2, GetDestructionCount());
        });
    }
}
