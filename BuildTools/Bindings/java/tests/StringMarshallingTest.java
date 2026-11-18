// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

// Corresponds to javascript/tests/test_strings.mjs using the same native fixture.
public final class StringMarshallingTest {
    private static final java.lang.String TEXT = "Aé水😀";

    private static IMarshalStrings create(java.lang.String text) {
        CreateMarshalStringsResult result = CreateMarshalStrings(text);
        equal(MARSHAL_STATUS.MARSHAL_STATUS_OK, result.returnValue);
        check(result.ppObject != null);
        return result.ppObject;
    }

    public static void main(java.lang.String[] args) {
        run("StringMarshalling.NativeUTF8Bytes", () -> {
            check(CheckStringBytes(TEXT));
            check(!CheckStringBytes("different"));
            equal(TEXT, GetStaticString());
            for (java.lang.String text : new java.lang.String[]{"", "ASCII", TEXT, "e\u0301", "\ufeffcontent", "x".repeat(40000)})
                equal(text, EchoString(text, false));
            check(CheckCharacterBytes(3, new byte[]{65, 0, 90}));
            check(!CheckCharacterBytes(3, new byte[]{65, 1, 90}));
            throwsType(IllegalArgumentException.class, () -> CheckCharacterBytes(3, new byte[2]));
        });
        run("StringMarshalling.NullAndEmptyOutputs", () -> {
            for (java.lang.String text : new java.lang.String[]{null, "", TEXT}) {
                equal(text, EchoNullableString(text));
                try (IMarshalStrings source = create(text)) {
                    equal(text, source.GetText());
                    IMarshalStrings_ReadTextResult reference = source.ReadText(false);
                    equal(MARSHAL_STATUS.MARSHAL_STATUS_PENDING, reference.returnValue);
                    equal(text, reference.Text);
                    IMarshalStrings_ReadTextPointerResult pointer = source.ReadTextPointer(false);
                    equal(reference.returnValue, pointer.returnValue);
                    equal(text, pointer.ppText);
                }
                equal(0L, GetLiveStringCount());
            }
        });
        run("StringMarshalling.DescriptorDefaultsAndCopies", () -> {
            MarshalStringDesc desc = new MarshalStringDesc();
            equal("native default", GetStringDescName(desc));
            equal(null, GetStringDescLabel(desc));
            for (java.lang.String text : new java.lang.String[]{"", TEXT.repeat(400)}) {
                desc.Name = text;
                desc.Label = text;
                java.lang.String name = GetStringDescName(desc);
                java.lang.String label = GetStringDescLabel(desc);
                desc.Name = "changed";
                desc.Label = null;
                equal(text, name);
                equal(text, label);
            }
            desc.Name = null;
            throwsType(IllegalArgumentException.class, () -> GetStringDescName(desc));
        });
        run("StringMarshalling.SnapshotsOutliveNativeOwner", () -> {
            java.lang.String direct;
            IMarshalStrings_ReadTextResult reference;
            IMarshalStrings_ReadTextPointerResult pointer;
            try (IMarshalStrings source = create(TEXT)) {
                direct = source.GetText();
                reference = source.ReadText(false);
                pointer = source.ReadTextPointer(false);
                source.SetText("replacement".repeat(100));
                equal("replacement".repeat(100), source.GetText());
            }
            equal(0L, GetLiveStringCount());
            equal(TEXT, direct);
            equal(TEXT, reference.Text);
            equal(TEXT, pointer.ppText);
        });
        run("StringMarshalling.FailedOutputsAndRecovery", () -> {
            try (IMarshalStrings source = create(TEXT)) {
                IMarshalStrings_ReadTextResult reference = source.ReadText(true);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, reference.returnValue);
                equal(null, reference.Text);
                IMarshalStrings_ReadTextPointerResult pointer = source.ReadTextPointer(true);
                equal(MARSHAL_STATUS.MARSHAL_STATUS_FAILED, pointer.returnValue);
                equal(null, pointer.ppText);
                source.SetText(null);
                equal(null, source.GetText());
                source.SetText("");
                equal("", source.GetText());
            }
            equal(0L, GetLiveStringCount());
        });
        run("StringMarshalling.InvalidTextDoesNotReachNativeCode", () -> {
            long calls = GetStringCallCount();
            throwsType(IllegalArgumentException.class, () -> EchoString(null, false));
            for (java.lang.String text : new java.lang.String[]{"a\0b", "a\ud800b\udc00"}) {
                // Java rejects malformed UTF-16; JavaScript uses TextEncoder replacement.
                throwsType(IllegalArgumentException.class, () -> EchoString(text, false));
                throwsType(IllegalArgumentException.class, () -> EchoNullableString(text));
                MarshalStringDesc desc = new MarshalStringDesc();
                desc.Name = text;
                throwsType(IllegalArgumentException.class, () -> GetStringDescName(desc));
            }
            equal(calls, GetStringCallCount());
            throwsType(IllegalArgumentException.class, BindingTests::GetInvalidUTF8);
            equal(TEXT, EchoString(TEXT, false));
        });
    }
}
