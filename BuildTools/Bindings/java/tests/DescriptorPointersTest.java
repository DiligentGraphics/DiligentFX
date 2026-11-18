// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Arrays;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class DescriptorPointersTest {
    private static IMarshalObject object() {
        MarshalDesc desc = new MarshalDesc();
        desc.Size = 19;
        return CreateMarshalObject(desc, 0, false).ppObject;
    }

    private static MarshalNestedPointerDesc nested(IMarshalObject object) {
        MarshalNestedPointerDesc desc = new MarshalNestedPointerDesc();
        desc.pNested = new MarshalPointerDesc();
        desc.pNested.pData = new MarshalDesc();
        desc.pNested.pData.Size = 41;
        MarshalInterfaceDesc interfaces = new MarshalInterfaceDesc();
        interfaces.pObject = object;
        interfaces.ppObjects = new IMarshalObject[]{object, null};
        interfaces.Count = 2;
        desc.pInterfaces = new MarshalInterfaceArrayDesc();
        desc.pInterfaces.pItems = new MarshalInterfaceDesc[]{interfaces};
        desc.pInterfaces.Count = 1;
        desc.pArrays = new MarshalArrayRoundTrip();
        desc.pArrays.Name = "Aé水😀";
        desc.pArrays.pValues = new double[]{1.25, -7.5};
        desc.pArrays.ValueCount = 2;
        desc.pArrays.pItems = new MarshalOutputArrayItem[]{new MarshalOutputArrayItem()};
        desc.pArrays.pItems[0].Id = 53;
        desc.pArrays.pItems[0].Label = "nested item";
        desc.pArrays.pItems[0].pSamples = new float[]{3.5f, -2.25f};
        desc.pArrays.pItems[0].SampleCount = 2;
        desc.pArrays.ItemCount = 1;
        return desc;
    }

    private static void close(MarshalNestedPointerDesc desc) {
        if (desc == null || desc.pInterfaces == null || desc.pInterfaces.pItems == null) return;
        for (MarshalInterfaceDesc item : desc.pInterfaces.pItems) {
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
        run("DescriptorPointers.OptionalFieldsAndNativeDefaults", () -> {
            MarshalPointerDesc input = new MarshalPointerDesc();
            equal(null, input.pData);
            equal(null, EchoPointerDesc(input).pData);
            equal(null, EchoStructPointer(null));
            equal(0L, ReadStructPointer(null));
            MarshalDesc child = new MarshalDesc();
            equal(0xFEDCBA9876543210L, ReadRequiredPointer(child));
            throwsType(IllegalArgumentException.class, () -> ReadRequiredPointer(null));
            child.Size = 29;
            input.pData = child;
            MarshalPointerDesc snapshot = EchoPointerDesc(input);
            check(snapshot != input);
            check(snapshot.pData != child);
            child.Size = 31;
            equal(29L, snapshot.pData.Size);
            equal(1.25f, snapshot.pData.Scale);
            equal(31L, EchoStructPointer(child).Size);

            MarshalDefaultPointerDesc defaults = new MarshalDefaultPointerDesc();
            equal(31L, defaults.pData.Size);
            equal(-2, defaults.pData.Count);
            equal(2.0f, defaults.pData.Scale);
            defaults.pData.Size = 99;
            equal(99L, EchoDefaultPointerDesc(defaults).pData.Size);
            equal(31L, new MarshalDefaultPointerDesc().pData.Size);
            defaults.pData = null;
            equal(null, EchoDefaultPointerDesc(defaults).pData);
        });

        run("DescriptorPointers.ConstPointeeSnapshots", () -> {
            equal(7, new MarshalReadOnlyChild().Value);
            equal(null, new MarshalReadOnlyPointerDesc().pChild);
            equal(null, GetReadOnlyPointerDesc(true).pChild);
            MarshalReadOnlyPointerDesc first = GetReadOnlyPointerDesc(false);
            MarshalReadOnlyPointerDesc second = GetReadOnlyPointerDesc(false);
            equal(7, first.pChild.Value);
            check(first.pChild != second.pChild);
            first.pChild.Value = 99;
            equal(7, second.pChild.Value);
            equal(7, GetReadOnlyPointerDesc(false).pChild.Value);

            MarshalReadOnlyPointerGroup group = GetReadOnlyPointerGroup();
            equal(7, group.pPointer.pChild.Value);
            equal(2L, group.pArray.ChildCount);
            equal(11, group.pArray.pChildren[0].Value);
            equal(23, group.pArray.pChildren[1].Value);
            group.pArray.pChildren[0].Value = 99;
            equal(11, GetReadOnlyPointerGroup().pArray.pChildren[0].Value);
            equal(null, new MarshalReadOnlyPointerGroup().pArray);
        });

        run("DescriptorPointers.RequiredFieldsAreValidatedAtUse", () -> {
            MarshalRequiredPointerDesc required = new MarshalRequiredPointerDesc();
            equal(null, required.pData); // Incomplete defaults are allowed during construction.
            throwsType(IllegalArgumentException.class, () -> EchoRequiredPointerDesc(required));
            MarshalPointerDesc optional = new MarshalPointerDesc();
            throwsType(IllegalArgumentException.class, () -> GetRequiredPointerSnapshot(optional));
            required.pData = new MarshalDesc();
            required.pData.Size = 37;
            equal(37L, EchoRequiredPointerDesc(required).pData.Size);
            optional.pData = required.pData;
            equal(37L, GetRequiredPointerSnapshot(optional).pData.Size);
        });

        run("DescriptorPointers.DeepSnapshotsRetainNestedData", () -> {
            MarshalNestedPointerDesc snapshot = null;
            try (IMarshalObject object = object()) {
                MarshalNestedPointerDesc input = nested(object);
                equal(41.0, ReadNestedPointerValue(input, 0));
                equal(38.0, ReadNestedPointerValue(input, 1));
                equal(53.0, ReadNestedPointerValue(input, 2));
                equal(1.25, ReadNestedPointerValue(input, 3));
                equal(-1.0, ReadNestedPointerValue(input, 4));
                snapshot = EchoNestedPointerDesc(input, false);
                check(snapshot.pNested != input.pNested);
                check(snapshot.pNested.pData != input.pNested.pData);
                check(snapshot.pArrays != input.pArrays);
                check(snapshot.pArrays.pItems[0] != input.pArrays.pItems[0]);
                equal(4L, object.GetRefCount());
                input.pNested.pData.Size = 99;
                input.pArrays.Name = "changed";
                input.pArrays.pValues[0] = 99;
                input.pArrays.pItems[0].pSamples[0] = 99;
                object.close();
                equal(41L, snapshot.pNested.pData.Size);
                equal("Aé水😀", snapshot.pArrays.Name);
                check(Arrays.equals(new double[]{1.25, -7.5}, snapshot.pArrays.pValues));
                check(Arrays.equals(new float[]{3.5f, -2.25f}, snapshot.pArrays.pItems[0].pSamples));
                equal(19L, snapshot.pInterfaces.pItems[0].pObject.GetSize());
                equal(19L, snapshot.pInterfaces.pItems[0].ppObjects[0].GetSize());
                equal(null, snapshot.pInterfaces.pItems[0].ppObjects[1]);
                equal(1L, GetLiveObjectCount());
            } finally { close(snapshot); }
            equal(0L, GetLiveObjectCount());
        });

        run("DescriptorPointers.FailedConversionsReleaseNestedReferences", () -> {
            try (IMarshalObject object = object()) {
                MarshalNestedPointerDesc input = nested(object);
                input.pArrays.Name = "\uD800"; // Fails after converting the earlier interface pointee.
                throwsType(IllegalArgumentException.class, () -> EchoNestedPointerDesc(input, false));
                equal(2L, object.GetRefCount());
                input.pArrays.Name = "valid";
                input.pArrays.ItemCount = 2;
                throwsType(IllegalArgumentException.class, () -> ReadNestedPointerValue(input, 0));
                equal(2L, object.GetRefCount());
                input.pArrays.ItemCount = 1;
                for (int i = 0; i < 3; ++i) {
                    throwsType(IllegalArgumentException.class, () -> EchoNestedPointerDesc(input, true));
                    equal(2L, object.GetRefCount());
                    equal(1L, GetLiveObjectCount());
                }
            }
            equal(0L, GetLiveObjectCount());
        });

        run("DescriptorPointers.FieldsInsideDynamicArrays", () -> {
            MarshalNestedPointerDesc[] result = null;
            try (IMarshalObject object = object()) {
                MarshalNestedPointerDesc input = nested(object);
                result = EchoNestedPointerArray(new MarshalNestedPointerDesc[]{input, new MarshalNestedPointerDesc()}, 2);
                equal(2, result.length);
                check(result[0].pArrays != input.pArrays);
                equal(null, result[1].pArrays);
                equal(null, result[1].pNested);
                object.close();
                equal(19L, result[0].pInterfaces.pItems[0].pObject.GetSize());
            } finally {
                if (result != null) for (MarshalNestedPointerDesc item : result) close(item);
            }
            equal(0L, GetLiveObjectCount());
        });

        run("DescriptorPointers.LargePointeesUseHeapStorage", () -> {
            MarshalNestedPointerDesc input = new MarshalNestedPointerDesc();
            for (int field = 0; field < 5; ++field) equal(-1.0, ReadNestedPointerValue(input, field));
            equal(null, EchoNestedPointerDesc(input, false).pImage);
            input.pImage = new TestLargeFixedArray();
            short[] pixels = input.pImage.Pixels;
            pixels[0] = 17;
            pixels[pixels.length - 1] = 241;
            equal(241.0, ReadNestedPointerValue(input, 4));
            MarshalNestedPointerDesc snapshot = EchoNestedPointerDesc(input, false);
            check(snapshot.pImage != input.pImage);
            check(snapshot.pImage.Pixels != pixels);
            pixels[pixels.length - 1] = 0;
            equal((short)17, snapshot.pImage.Pixels[0]);
            equal((short)241, snapshot.pImage.Pixels[pixels.length - 1]);
        });
    }
}
