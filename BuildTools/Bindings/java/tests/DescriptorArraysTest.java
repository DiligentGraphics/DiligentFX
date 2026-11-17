// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Arrays;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class DescriptorArraysTest {
    private static MarshalArrayItem item(int id, java.lang.String name) {
        MarshalArrayItem value = new MarshalArrayItem();
        value.Id = id;
        value.Name = name;
        value.pValues = new float[]{1.25f, -7.5f};
        return value;
    }

    private static IMarshalObject object(long size) {
        MarshalDesc desc = new MarshalDesc();
        desc.Size = size;
        return CreateMarshalObject(desc, 0, false).ppObject;
    }

    private static MarshalInterfaceDesc interfaces(IMarshalObject object) {
        MarshalInterfaceDesc desc = new MarshalInterfaceDesc();
        desc.pObject = object;
        desc.ppObjects = new IMarshalObject[]{object, null};
        desc.Count = 2;
        return desc;
    }

    private static void close(MarshalInterfaceDesc[] items) {
        if (items == null) return;
        for (MarshalInterfaceDesc item : items) {
            if (item.pObject != null) item.pObject.close();
            if (item.ppObjects != null) {
                for (IMarshalObject object : item.ppObjects) {
                    if (object != null) object.close();
                }
            }
        }
    }

    @SuppressWarnings("try")
    public static void main(java.lang.String[] args) {
        run("DescriptorArrays.ContiguousInputsAndNestedFields", () -> {
            MarshalArrayItem a = item(11, "Aé水😀");
            MarshalArrayItem b = item(-29, "second");
            b.Transform.Scale.y = 3.5f;
            b.Transform.Position.y = -9.0f;
            MarshalArrayItem[] input = {a, b, null};
            equal(11.0, ReadArrayItem(input, 2, 0, 0));
            equal(-29.0, ReadArrayItem(input, 2, 1, 0));
            equal(3.5, ReadArrayItem(input, 2, 1, 2));
            equal(-9.0, ReadArrayItem(input, 2, 1, 3));
            equal(1.25, ReadArrayItem(input, 2, 0, 5));
            equal(-7.5, ReadArrayItem(input, 2, 1, 6));
            equal("Aé水😀", GetArrayItemName(input, 2, 0));
            equal("second", GetArrayItemName(input, 2, 1));
            MarshalArrayDesc desc = new MarshalArrayDesc();
            desc.pItems = input;
            desc.pSamples = new int[]{0, 32768, 65535};
            equal(-29.0, ReadArrayDesc(desc, 1, 0));
            equal(262141L, SumArraySamples(desc));
            MarshalArrayDesc first = new MarshalArrayDesc();
            first.ItemCount = 1;
            first.pItems = new MarshalArrayItem[]{a};
            equal(-29, ReadArrayGroup(new MarshalArrayDesc[]{first, desc}, 2, 1, 1));
            equal(11, a.Id);
            equal(3.5f, b.Transform.Scale.y);
        });

        run("DescriptorArrays.InputFailuresDoNotInvokeNativeCode", () -> {
            MarshalArrayItem good = item(7, "valid");
            MarshalArrayItem bad = item(9, "\uD800");
            long calls = GetArrayCallCount();
            throwsType(IllegalArgumentException.class, () -> ReadArrayItem(new MarshalArrayItem[]{good, bad}, 2, 0, 0));
            throwsType(IllegalArgumentException.class, () -> ReadArrayItem(new MarshalArrayItem[]{good, null}, 2, 0, 0));
            throwsType(IllegalArgumentException.class, () -> ReadArrayItem(new MarshalArrayItem[]{good}, 2, 0, 0));
            throwsType(IllegalArgumentException.class, () -> ReadArrayItem(null, 1, 0, 0));
            throwsType(IllegalArgumentException.class, () -> ReadArrayItem(new MarshalArrayItem[0], 0x80000000L, 0, 0));
            MarshalArrayDesc desc = new MarshalArrayDesc();
            desc.pItems = new MarshalArrayItem[]{good};
            throwsType(IllegalArgumentException.class, () -> ReadArrayDesc(desc, 0, 0));
            desc.ItemCount = 1;
            desc.pSamples = new int[]{0, 1, 65536};
            throwsType(IllegalArgumentException.class, () -> SumArraySamples(desc));
            desc.pSamples = new int[0];
            desc.SampleCount = -1;
            throwsType(IllegalArgumentException.class, () -> SumArraySamples(desc));
            equal(calls, GetArrayCallCount());
            equal(-1000.0, ReadArrayItem(null, 0, 0, 0));
        });

        run("DescriptorArrays.NativeDefaultsRemainConstructible", () -> {
            MarshalArrayItem item = new MarshalArrayItem();
            equal(-7, item.Id);
            equal("item default", item.Name);
            equal(2L, item.ValueCount);
            equal(null, item.pValues);
            equal(1.0f, item.Transform.Scale.x);
            MarshalOutputArrayDesc output = new MarshalOutputArrayDesc();
            equal(3L, output.Count);
            equal(null, output.pInt8); // Required nonempty native fields are initially incomplete.
            equal(null, output.pGroups);
        });

        run("DescriptorArrays.BorrowedOutputsAreIndependentSnapshots", () -> {
            MarshalOutputArrayDesc saved;
            MarshalOutputArrayItem[] items;
            try (IMarshalArraySnapshots source = CreateMarshalArraySnapshots().ppObject) {
                equal(1L, GetLiveArraySnapshotCount());
                saved = source.GetDesc();
                check(Arrays.equals(new byte[]{-128, 17, 127}, saved.pInt8));
                check(Arrays.equals(new short[]{0, 128, 255}, saved.pUint8));
                check(Arrays.equals(new short[]{-32768, 17, 32767}, saved.pInt16));
                check(Arrays.equals(new int[]{0, 32768, 65535}, saved.pUint16));
                check(Arrays.equals(new int[]{Integer.MIN_VALUE, 17, Integer.MAX_VALUE}, saved.pInt32));
                check(Arrays.equals(new long[]{0, 0x80000000L, 0xFFFFFFFFL}, saved.pUint32));
                check(Arrays.equals(new long[]{-0x7000000000000001L, 17, 0x7123456789ABCDEFL}, saved.pInt64));
                check(Arrays.equals(new long[]{0, Long.MIN_VALUE, 0xFEDCBA9876543210L}, saved.pUint64));
                check(Arrays.equals(new float[]{-1.5f, 0.25f, 123.5f}, saved.pFloat32));
                check(Arrays.equals(new double[]{-1.25, 0.1, 123456.789}, saved.pFloat64));
                check(Arrays.equals(new boolean[]{true, false, true}, saved.pBool));
                check(Arrays.equals(new short[]{2, 241, 2}, saved.pMARSHAL_MODE));
                check(Arrays.equals(new byte[]{65, 0, -1}, saved.pChar));
                equal(2, saved.pGroups.length);
                equal(2, saved.pGroups[0].pItems.length);
                equal(1, saved.pGroups[1].pItems.length);
                items = source.GetItems(2);
                check(items != saved.pGroups[0].pItems);
                equal(41L, items[0].Id);
                equal("Aé水😀", items[0].Label);
                equal(-2.5f, items[0].Transform.Position.y);
                check(Arrays.equals(new float[]{0.25f, 123.5f}, items[0].pSamples));
                equal(null, items[1].Label);
                equal(0xFEDCBA98L, items[1].Id);
                source.Update();
                equal(99L, source.GetItems(1)[0].Id);
                equal(41L, items[0].Id);
                equal(0.25f, items[0].pSamples[0]);
                items[0].Id = 3;
                equal(99L, source.GetItems(1)[0].Id);
                equal(41L, saved.pGroups[0].pItems[0].Id);
                equal(0, source.GetItems(0).length);
            }
            equal(0L, GetLiveArraySnapshotCount());
            equal("Aé水😀", items[0].Label);
            equal(0xFEDCBA98L, saved.pGroups[1].pItems[0].Id);
        });

        run("DescriptorArrays.RoundTripsAndEmptySpans", () -> {
            MarshalArrayRoundTrip input = new MarshalArrayRoundTrip();
            equal(null, EchoArrayDescriptor(input, false).pItems);
            input.pItems = new MarshalOutputArrayItem[0];
            input.pValues = new double[0];
            MarshalArrayRoundTrip empty = EchoArrayDescriptor(input, false);
            equal(0, empty.pItems.length);
            equal(0, empty.pValues.length);
            MarshalOutputArrayItem item = new MarshalOutputArrayItem();
            item.Id = 123;
            item.Label = "copied label";
            item.pSamples = new float[]{3.0f, -2.0f};
            item.SampleCount = 2;
            input.pItems = new MarshalOutputArrayItem[]{item};
            input.ItemCount = 1;
            input.pValues = new double[]{-1.25, 7.5};
            input.ValueCount = 2;
            MarshalArrayRoundTrip result = EchoArrayDescriptor(input, false);
            check(result.pItems != input.pItems);
            check(result.pItems[0] != item);
            check(result.pItems[0].pSamples != item.pSamples);
            item.pSamples[0] = 99;
            equal(3.0f, result.pItems[0].pSamples[0]);
            equal("copied label", result.pItems[0].Label);
            check(Arrays.equals(new double[]{-1.25, 7.5}, result.pValues));
            throwsType(IllegalArgumentException.class, () -> EchoArrayDescriptor(input, true));
            throwsType(IllegalArgumentException.class, () -> GetInvalidOutputArray(1)); // Count exceeds jsize; no large allocation.
        });

        run("DescriptorArrays.CallerFilledBuffersAndLargeInputs", () -> {
            equal(0, FillRecords(0).pRecords.length);
            MarshalOutputRecord[] filled = FillRecords(4096).pRecords;
            equal(4096, filled.length);
            for (int i = 0; i < filled.length; ++i) {
                equal(100L + i, filled[i].Value);
                equal("default", filled[i].Label);
            }
            // The native values plus conversion storage exceed a default JVM stack.
            MarshalArrayItem repeated = item(47, "large array");
            MarshalArrayItem[] many = new MarshalArrayItem[16384];
            Arrays.fill(many, repeated);
            equal(47.0, ReadArrayItem(many, many.length, many.length - 1, 0));
            equal("large array", GetArrayItemName(many, many.length, many.length - 1));
        });

        run("DescriptorArrays.InterfaceReferencesSurviveConversionAndRollback", () -> {
            MarshalInterfaceArrayDesc snapshot = null;
            MarshalInterfaceDesc[] filled = null;
            try (IMarshalObject object = object(19)) {
                MarshalInterfaceDesc first = interfaces(object);
                MarshalInterfaceDesc second = interfaces(object);
                MarshalInterfaceArrayDesc desc = new MarshalInterfaceArrayDesc();
                desc.pItems = new MarshalInterfaceDesc[]{first, second};
                desc.Count = 2;
                equal(76L, SumInterfaceDescriptors(desc.pItems, 2));
                second.Label = "\uD800";
                throwsType(IllegalArgumentException.class, () -> SumInterfaceDescriptors(desc.pItems, 2));
                equal(2L, object.GetRefCount());
                second.Label = "second";
                throwsType(IllegalArgumentException.class, () -> EchoInterfaceDescriptors(desc, true));
                throwsType(IllegalArgumentException.class, () -> FillInterfaceDescriptors(first, 1, true));
                equal(2L, object.GetRefCount());
                snapshot = EchoInterfaceDescriptors(desc, false);
                equal(6L, object.GetRefCount());
                FillInterfaceDescriptorsResult result = FillInterfaceDescriptors(first, 5, false);
                filled = result.pItems;
                equal(2L, result.Written);
                equal(2, filled.length);
                equal(10L, object.GetRefCount());
                equal(0, FillInterfaceDescriptors(first, 0, false).pItems.length);
                object.close();
                equal(19L, snapshot.pItems[1].ppObjects[0].GetSize());
                equal(19L, filled[1].pObject.GetSize());
                equal(1L, GetLiveObjectCount());
            } finally {
                if (snapshot != null) close(snapshot.pItems);
                close(filled);
            }
            equal(0L, GetLiveObjectCount());
        });
    }
}
