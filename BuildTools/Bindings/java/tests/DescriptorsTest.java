// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class DescriptorsTest {
    public static void main(java.lang.String[] args) {
        run("Descriptors.NativeDefaults", () -> check(CheckDefaultDesc(new TestDesc())));
        run("Descriptors.IncompleteRequiredDefaults", () -> {
            RequiredDesc leaf = new RequiredDesc();
            equal(null, leaf.Name);
            RequiredNestedDesc desc = new RequiredNestedDesc();
            equal(null, desc.Inner.Name);
            throwsType(IllegalArgumentException.class, () -> ReadRequiredName(desc));
            desc.Inner.Name = "assigned";
            equal("assigned", ReadRequiredName(desc));
            // Only initialization may expose incomplete defaults. Native results
            // must still satisfy the descriptor's required-field contract.
            throwsType(IllegalArgumentException.class, BindingTests::GetIncompleteDesc);
        });
        run("Descriptors.NestedRoundTrip", () -> {
            TestDesc source = new TestDesc();
            source.Size = 0x123456789ABCDEF0L;
            source.Count = 51;
            source.Enabled = false;
            source.Mode = TEST_MODE.TEST_MODE_FIRST;
            source.Position.X = -9.25f;
            source.Position.Y = 13.5;
            source.Name = "material \uD83D\uDE00";
            source.Label = "label";
            CopyDescResult copy = CopyDesc(source);
            equal(TEST_STATUS.TEST_STATUS_PENDING, copy.returnValue);
            equal(source.Size, copy.Result.Size);
            equal(source.Count, copy.Result.Count);
            equal(source.Enabled, copy.Result.Enabled);
            equal(source.Mode, copy.Result.Mode);
            equal(source.Position.X, copy.Result.Position.X);
            equal(source.Position.Y, copy.Result.Position.Y);
            equal(source.Name, copy.Result.Name);
            equal(source.Label, copy.Result.Label);
            check(source != copy.Result && source.Position != copy.Result.Position);
            source.Position.X = 5;
            equal(-9.25f, copy.Result.Position.X);
        });
        run("Descriptors.StructPointerOutput", () -> check(CheckDefaultDesc(CopyDescPointer(new TestDesc()).pResult)));
        run("Descriptors.MultipleOutputs", () -> {
            MakeOutputsResult result = MakeOutputs(23);
            equal(0xFEDCBA9800000017L, result.Value);
            equal(23.5f, result.Vector.X);
            equal(-3.25, result.Vector.Y);
            FailedOutputResult failure = FailedOutput();
            equal(TEST_STATUS.TEST_STATUS_FAILED, failure.returnValue);
            equal(0L, failure.Value);
        });
        run("Descriptors.NullablePointer", () -> {
            equal(null, EchoOptionalDesc(null));
            check(CheckDefaultDesc(EchoOptionalDesc(new TestDesc())));
            throwsType(IllegalArgumentException.class, () -> CheckDefaultDesc(null));
        });
        run("Descriptors.InvalidNestedFields", () -> {
            TestDesc desc = new TestDesc();
            desc.Position = null;
            throwsType(IllegalArgumentException.class, () -> CopyDesc(desc));
            desc.Position = new TestVector();
            desc.Name = null;
            throwsType(IllegalArgumentException.class, () -> CopyDesc(desc));
            desc.Name = "restored";
            equal("restored", CopyDesc(desc).Result.Name);
        });
    }
}
