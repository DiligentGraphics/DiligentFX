// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.TestSupport.*;

public final class NamesTest {
    public static void main(java.lang.String[] args) {
        run("Names.NativeStringAndJavaTextHaveDistinctJNITypes", () -> {
            BindingTests.String source = new BindingTests.String();
            equal(37, source.Value);
            source.Value = 91;
            source.Text = "native text \uD83D\uDE00";
            BindingTests.String copy = BindingTests.CopyNativeString(source).Result;
            equal(91, copy.Value);
            equal(source.Text, copy.Text);
            equal(source.Text, BindingTests.EchoText(copy.Text));
            equal(source.Text, BindingTests.EchoOptionalText(copy.Text));
        });
        run("Names.RuntimeClassesCannotBeShadowed", () -> {
            equal(11, new BindingTests.System().Value);
            equal(11, new BindingTests.Cleaner().Value);
            equal(11, new BindingTests.Runnable().Value);
            equal(11, new BindingTests.AutoCloseable().Value);
            equal(11, new BindingTests.Override().Value);
            // Interface cleanup still uses the Java runtime types above.
            try (BindingTests.ITestCounter object = BindingTests.CreateCounter(23, 0).ppCounter) {
                equal(23L, object.GetValue());
            }
            equal(0L, BindingTests.GetLiveCounterCount());
        });
    }
}
