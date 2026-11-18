// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

// Corresponds to javascript/tests/struct_result_tests.mjs.
public final class StructResultsTest {
    private static final java.lang.String TEXT = "Aé水😀";

    private static IMarshalSnapshots create() {
        CreateMarshalSnapshotsResult result = CreateMarshalSnapshots();
        equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, result.returnValue);
        check(result.ppObject != null);
        return result.ppObject;
    }

    private static void expected(MarshalSnapshotDesc desc) {
        equal(41L, desc.Tag);
        equal(TEXT, desc.Name);
        check(CheckDefaultDesc(desc.Values));
        equal(0xfedcba98L, desc.Child.Sequence);
        equal("native label", desc.Child.Label);
        equal(1.5f, desc.Child.Offset.x);
        equal(-2.5f, desc.Child.Offset.y);
        equal(3.25f, desc.Child.Offset.z);
    }

    public static void main(java.lang.String[] args) {
        run("StructResults.IndependentNativeSnapshots", () -> {
            check(CheckDefaultDesc(GetStaticDescriptor()));
            try (IMarshalSnapshots source = create()) {
                MarshalSnapshotDesc first = source.GetDesc();
                MarshalSnapshotDesc second = source.GetDesc();
                expected(first);
                expected(second);
                check(first != second && first.Values != second.Values);
                check(first.Child != second.Child && first.Child.Offset != second.Child.Offset);
                first.Values.Count = 99;
                first.Child.Offset.y = 100;
                first.Child.Label = "edited";
                first.Name = "edited";
                first.Tag = 0;
                expected(second);
                expected(source.GetDesc());
            }
            equal(0L, GetLiveSnapshotCount());
        });
        run("StructResults.SnapshotsSurviveMutationAndDestruction", () -> {
            MarshalSnapshotDesc first;
            MarshalSnapshotDesc second;
            java.lang.String name = "changed " + TEXT.repeat(400);
            try (IMarshalSnapshots source = create()) {
                first = source.GetDesc();
                source.Update(42, name, "new label");
                second = source.GetDesc();
            }
            equal(0L, GetLiveSnapshotCount());
            expected(first);
            equal(name, second.Name);
            equal(42, second.Values.Count);
            equal("new label", second.Child.Label);
            equal(42.f, second.Child.Offset.x);
        });
        run("StructResults.NullEmptyAndInvalidStrings", () -> {
            try (IMarshalSnapshots source = create()) {
                source.Update(0, "", null);
                MarshalSnapshotDesc absent = source.GetDesc();
                source.Update(1, "", "");
                MarshalSnapshotDesc empty = source.GetDesc();
                equal("", absent.Name);
                equal(null, absent.Child.Label);
                equal("", empty.Child.Label);
                source.Update(0, null, null);
                throwsType(IllegalArgumentException.class, source::GetDesc);
                source.Update(0, TEXT, "recovered");
                equal(TEXT, source.GetDesc().Name);
            }
            equal(0L, GetLiveSnapshotCount());
        });
        run("StructResults.InputStorageLivesThroughOutputConversion", () -> {
            MarshalStringDesc input = new MarshalStringDesc();
            equal("native default", EchoStringDescriptor(input, false).Name);
            input.Name = TEXT.repeat(400);
            input.Label = "";
            input.Enabled = false;
            MarshalStringDesc result = EchoStringDescriptor(input, false);
            input.Name = "changed";
            for (int i = 0; i < 20; ++i) {
                throwsType(IllegalArgumentException.class, () -> EchoStringDescriptor(input, true));
                equal("changed", EchoStringDescriptor(input, false).Name);
            }
            equal(TEXT.repeat(400), result.Name);
            equal("", result.Label);
            equal(false, result.Enabled);
        });
    }
}
