// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.Objects;

final class TestSupport {
    private TestSupport() {}

    static void run(String name, Runnable test) {
        System.out.println("[ RUN      ] " + name);
        test.run();
        System.out.println("[       OK ] " + name);
    }

    static void equal(Object expected, Object actual) {
        if (!Objects.equals(expected, actual)) {
            throw new AssertionError("Expected " + expected + ", got " + actual);
        }
    }

    static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError("Condition was false");
        }
    }

    static void throwsType(Class<? extends Throwable> type, Runnable operation) {
        try {
            operation.run();
        } catch (Throwable error) {
            if (type.isInstance(error)) {
                return;
            }
            throw new AssertionError("Expected " + type.getName(), error);
        }
        throw new AssertionError("Expected " + type.getName());
    }
}
