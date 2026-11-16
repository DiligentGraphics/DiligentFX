// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class InterfacesTest {
    public static void main(java.lang.String[] args) {
        run("Interfaces.OwnedReference", () -> {
            equal(0L, GetLiveCounterCount());
            CreateCounterResult result = CreateCounter(19, TEST_STATUS.TEST_STATUS_PENDING);
            equal(TEST_STATUS.TEST_STATUS_PENDING, result.returnValue);
            ITestCounter object = result.ppCounter;
            try (object) {
                equal(1L, GetLiveCounterCount());
                equal(19L, object.GetValue());
                object.SetValue(0xFEDCBA9876543210L);
                equal(0xFEDCBA9876543210L, ReadCounter(object));
                // One wrapper reference and one temporary reference for this JNI call.
                equal(2L, object.GetRefCount());
            }
            object.close();
            equal(0L, GetLiveCounterCount());
            throwsType(IllegalStateException.class, object::GetValue);
            throwsType(IllegalStateException.class, () -> ReadCounter(object));
        });
        run("Interfaces.BorrowedReference", () -> {
            ITestCounter original = CreateCounter(41, TEST_STATUS.TEST_STATUS_OK).ppCounter;
            try (ITestCounter borrowed = BorrowCounter(original)) {
                check(original != borrowed);
                equal(3L, borrowed.GetRefCount());
                original.close();
                equal(41L, borrowed.GetValue());
                equal(1L, GetLiveCounterCount());
            }
            equal(0L, GetLiveCounterCount());
        });
        run("Interfaces.BorrowedOutputAndReferenceInput", () -> {
            ITestCounter original = CreateCounter(71, TEST_STATUS.TEST_STATUS_OK).ppCounter;
            try (ITestCounter borrowed = BorrowCounterOutput(original).ppCounter) {
                original.close();
                equal(71L, ReadCounterReference(borrowed));
                equal(2L, borrowed.GetRefCount());
            }
            equal(0L, GetLiveCounterCount());
            throwsType(IllegalArgumentException.class, () -> ReadCounterReference(null));
        });
        run("Interfaces.FailedCreation", () -> {
            CreateCounterResult result = CreateCounter(19, TEST_STATUS.TEST_STATUS_FAILED);
            equal(TEST_STATUS.TEST_STATUS_FAILED, result.returnValue);
            equal(null, result.ppCounter);
            equal(0L, GetLiveCounterCount());
            throwsType(IllegalArgumentException.class, () -> ReadCounter(null));
        });
        run("Interfaces.FailedOutputConversionReleasesReference", () -> {
            for (int i = 0; i < 100; ++i) {
                throwsType(IllegalArgumentException.class, BindingTests::FailingCounterOutputs);
                equal(0L, GetLiveCounterCount());
            }
        });
    }
}
