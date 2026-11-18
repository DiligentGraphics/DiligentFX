// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class InterfaceInheritanceTest {
    private static IMarshalObject create(long size, boolean mutable) {
        MarshalDesc desc = new MarshalDesc();
        desc.Size = size;
        CreateMarshalObjectResult result = CreateMarshalObject(desc, MARSHAL_STATUS.MARSHAL_STATUS_PENDING, mutable);
        equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, result.returnValue);
        return result.ppObject;
    }

    // Early closes verify independent references; try-with-resources also covers assertion failures.
    @SuppressWarnings("try")
    public static void main(java.lang.String[] args) {
        run("InterfaceInheritance.CovariantDescriptorReturns", () -> {
            MarshalObjectAttribs baseDefaults = new MarshalObjectAttribs();
            equal(7L, baseDefaults.Value);
            equal("base default", baseDefaults.Name);
            MarshalObjectDesc defaults = new MarshalObjectDesc();
            MarshalObjectAttribs defaultsAsBase = defaults;
            equal(29L, defaultsAsBase.Value);
            equal("derived default", defaultsAsBase.Name);
            equal(0xFEDCBA9876543210L, defaults.Size);
            defaultsAsBase.Value = 103;
            equal(103L, defaults.Value);
            equal(103L, ReadOverrideBase(defaults));
            MarshalObjectDesc copied = EchoOverrideDesc(defaults);
            equal(103L, copied.Value);
            equal("derived default", copied.Name);
            equal(defaults.Size, copied.Size);
            check(copied != defaults);

            MarshalObjectDesc snapshot;
            try (IMarshalBaseOverrides base = CreateMarshalOverrides().ppObject;
                 IMarshalDerivedOverrides derived = base.QueryInterface(IMarshalDerivedOverrides.class)) {
                derived.SetValue(113);
                snapshot = derived.GetDesc();
                equal(113L, snapshot.Value);
                equal("derived default", snapshot.Name);
                equal(0xFEDCBA9876543210L, snapshot.Size);
                MarshalObjectAttribs baseSnapshot = base.GetDesc();
                equal(113L, baseSnapshot.Value);
                // The Java override remains covariant when called through the base class.
                IMarshalBaseOverrides upcast = derived;
                MarshalObjectAttribs virtualSnapshot = upcast.GetDesc();
                check(virtualSnapshot instanceof MarshalObjectDesc);
                equal(snapshot.Size, ((MarshalObjectDesc)virtualSnapshot).Size);
                derived.SetValue(127);
                snapshot.Value = 131;
                equal(127L, derived.GetDesc().Value);
                equal(113L, baseSnapshot.Value);
            }
            // Snapshots do not borrow the native object's storage.
            equal(131L, snapshot.Value);
            equal("derived default", snapshot.Name);
        });

        run("InterfaceInheritance.RedeclaredOutputMethodsReuseResultTypes", () -> {
            try (IMarshalBaseOverrides base = CreateMarshalOverrides().ppObject;
                 IMarshalDerivedOverrides derived = base.QueryInterface(IMarshalDerivedOverrides.class)) {
                derived.SetValue(137);
                IMarshalBaseOverrides_GetValueResult direct = derived.GetValue();
                IMarshalBaseOverrides upcast = derived;
                IMarshalBaseOverrides_GetValueResult virtual = upcast.GetValue();
                IMarshalBaseOverrides_GetValueResult queried = base.GetValue();
                equal(137L, direct.Value);
                equal(137L, virtual.Value);
                equal(137L, queried.Value);
                equal(direct.getClass(), queried.getClass());
                // Status and multiple outputs reuse the same result class too.
                IMarshalBaseOverrides_GetValuesResult values = derived.GetValues();
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, values.returnValue);
                equal(137L, values.Value);
                equal(0xFEDCBA9876543210L, values.Size);
                derived.SetValue(139);
                equal(139L, base.GetValue().Value);
                equal(137L, direct.Value);
            }
        });

        run("InterfaceInheritance.FactoryFailuresAndPendingOutputs", () -> {
            long destroyed = GetDestructionCount();
            MarshalDesc desc = new MarshalDesc();
            CreateMarshalObjectResult failure = CreateMarshalObject(desc, MARSHAL_STATUS.MARSHAL_STATUS_FAILED, false);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, failure.returnValue);
            equal(null, failure.ppObject);
            equal(0L, GetLiveObjectCount());
            CreateMarshalObjectResult pending = CreateMarshalObject(desc, MARSHAL_STATUS.MARSHAL_STATUS_PENDING, true);
            equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, pending.returnValue);
            try (IMarshalObject object = pending.ppObject) {
                equal(desc.Size, object.GetSize());
                equal(2L, object.GetRefCount()); // Wrapper plus the active JNI call.
                equal(0L, GetObjectSize(null));
                object.close();
                object.close();
                throwsType(IllegalStateException.class, () -> GetObjectSize(object));
            }
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 1, GetDestructionCount());
        });

        run("InterfaceInheritance.InheritedCallsAndIndependentOwnership", () -> {
            equal(0L, GetLiveObjectCount());
            long destroyed = GetDestructionCount();
            check(IMarshalObject.class.isAssignableFrom(IMarshalMutableObject.class));
            IMarshalObject original = create(19, true);
            try (original; IMarshalMutableObject mutable = original.QueryInterface(IMarshalMutableObject.class)) {
                check(mutable != original);
                // Two wrapper references plus the temporary reference held by this JNI call.
                equal(3L, mutable.GetRefCount());
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, mutable.SetSize(0xFEDCBA9876543210L));
                equal(0xFEDCBA9876543210L, original.GetSize());
                IMarshalObject base = mutable;
                equal(0xFEDCBA9876543210L, base.GetSize());
                equal(0xFEDCBA9876543210L, GetObjectSize(mutable));
                equal(0xFEDCBA9876543210L, ReadObjectReference(mutable));
                original.close();
                equal(2L, mutable.GetRefCount());
                throwsType(IllegalStateException.class, original::GetSize);
                throwsType(IllegalStateException.class, () -> original.QueryInterface(IMarshalObject.class));
                try (IMarshalObject queriedBase = mutable.QueryInterface(IMarshalObject.class);
                     IMarshalMutableObject queriedSelf = mutable.QueryInterface(IMarshalMutableObject.class)) {
                    check(queriedBase != mutable);
                    check(queriedSelf != mutable);
                    equal(4L, queriedBase.GetRefCount());
                    mutable.close();
                    queriedSelf.SetSize(23);
                    equal(23L, queriedBase.GetSize());
                }
            }
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 1, GetDestructionCount());
        });

        run("InterfaceInheritance.InterfaceReferenceArguments", () -> {
            try (IMarshalObject a = create(71, true); IMarshalObject b = create(3, true);
                 IMarshalMutableObject source = a.QueryInterface(IMarshalMutableObject.class);
                 IMarshalMutableObject destination = b.QueryInterface(IMarshalMutableObject.class)) {
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, destination.CopySizeFrom(source));
                equal(71L, b.GetSize());
                source.SetSize(89);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, source.CopySizeTo(destination));
                equal(89L, b.GetSize());
                MarshalDesc desc = new MarshalDesc();
                desc.Size = 97;
                equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, WriteObjectReference(destination, desc));
                equal(97L, ReadObjectReference(destination));
                equal(3L, source.GetRefCount());
                equal(3L, destination.GetRefCount());
            }
            equal(0L, GetLiveObjectCount());
        });

        run("InterfaceInheritance.BorrowedBaseResultsRetainDerivedInput", () -> {
            long destroyed = GetDestructionCount();
            IMarshalObject original = create(101, true);
            IMarshalMutableObject mutable = original.QueryInterface(IMarshalMutableObject.class);
            try (original; mutable;
                 IMarshalObject borrowed = BorrowObject(mutable, false);
                 IMarshalObject output = BorrowObjectOutput(mutable).ppObject) {
                equal(null, BorrowObject(mutable, true));
                equal(5L, borrowed.GetRefCount());
                original.close();
                mutable.close();
                equal(101L, borrowed.GetSize());
                equal(101L, output.GetSize());
                equal(3L, output.GetRefCount());
            }
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 1, GetDestructionCount());
        });

        run("InterfaceInheritance.UnsupportedQueriesDoNotRetainReferences", () -> {
            try (IMarshalObject object = create(31, false);
                 ITestCounter counter = CreateCounter(7, TEST_STATUS.TEST_STATUS_OK).ppCounter) {
                equal(null, object.QueryInterface(IMarshalMutableObject.class));
                // An interface without an IID can still be a query source.
                equal(null, counter.QueryInterface(IMarshalObject.class));
                throwsType(NullPointerException.class, () -> object.QueryInterface(null));
                throwsType(IllegalArgumentException.class, () -> object.QueryInterface(NativeObject.class));
                throwsType(IllegalArgumentException.class, () -> object.QueryInterface(ITestCounter.class));
                equal(2L, object.GetRefCount());
                equal(2L, counter.GetRefCount());
            }
            equal(0L, GetLiveObjectCount());
            equal(0L, GetLiveCounterCount());
        });

        run("InterfaceInheritance.QueriesPreserveDistinctInterfaceAddresses", () -> {
            long destroyed = GetDestructionCount();
            IMarshalObject original = create(41, true);
            // Tagged is a different native base subobject, not a base of Mutable.
            try (original; IMarshalTaggedObject tagged = original.QueryInterface(IMarshalTaggedObject.class)) {
                equal(41L ^ 0xFEDCBA9876543210L, tagged.GetTag());
                original.close();
                try (IMarshalMutableObject mutable = tagged.QueryInterface(IMarshalMutableObject.class);
                     IMarshalObject base = tagged.QueryInterface(IMarshalObject.class);
                     IMarshalTaggedObject anotherTag = mutable.QueryInterface(IMarshalTaggedObject.class)) {
                    equal(5L, base.GetRefCount());
                    tagged.close();
                    mutable.SetSize(43);
                    equal(43L, GetObjectSize(mutable));
                    equal(43L, base.GetSize());
                    equal(43L ^ 0xFEDCBA9876543210L, anotherTag.GetTag());
                    base.close();
                    mutable.close();
                    equal(1L, GetLiveObjectCount());
                    equal(43L ^ 0xFEDCBA9876543210L, anotherTag.GetTag());
                }
            }
            equal(0L, GetLiveObjectCount());
            equal(destroyed + 1, GetDestructionCount());
        });
    }
}
