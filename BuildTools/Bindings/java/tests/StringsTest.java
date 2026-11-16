// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class StringsTest {
    public static void main(java.lang.String[] args) {
        run("Strings.StandardUTF8", () -> {
            java.lang.String value = "A\u00E9\uD83D\uDE00";
            check(CheckUTF8(value));
            equal(7L, TextByteCount(value));
            equal(value, EchoText(value));
            equal("", EchoText(""));
            equal("\u6F22\u5B57", EchoText("\u6F22\u5B57"));
        });
        run("Strings.Nullability", () -> {
            equal(null, EchoOptionalText(null));
            equal("", EchoOptionalText(""));
            throwsType(IllegalArgumentException.class, () -> EchoText(null));
        });
        run("Strings.InvalidEncodings", () -> {
            throwsType(IllegalArgumentException.class, () -> EchoText("a\0b"));
            throwsType(IllegalArgumentException.class, () -> EchoText("\uD800"));
            throwsType(IllegalArgumentException.class, () -> EchoText("\uDC00"));
            throwsType(IllegalArgumentException.class, BindingTests::GetInvalidText);
        });
        run("Strings.RepeatedConversionCleanup", () -> {
            java.lang.String value = "\u00E9\uD83D\uDE00".repeat(1024);
            for (int i = 0; i < 200; ++i) {
                equal(value, EchoText(value));
            }
        });
    }
}
