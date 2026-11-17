// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.lang.ref.WeakReference;
import java.util.concurrent.TimeUnit;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

// CTest runs this in its own JVM. No other test's wrappers or counters can
// affect the assertions, and no fixed collection delay is assumed.
public final class CleanerTest {
    private static WeakReference<ITestCounter> createForgottenCounter() {
        ITestCounter counter = CreateCounter(73, TEST_STATUS.TEST_STATUS_OK).ppCounter;
        return new WeakReference<>(counter);
    }

    private static WeakReference<ITestCounter> createClosedCounter() {
        ITestCounter counter = CreateCounter(79, TEST_STATUS.TEST_STATUS_OK).ppCounter;
        counter.close();
        counter.close();
        return new WeakReference<>(counter);
    }

    public static void main(java.lang.String[] args) {
        run("Interfaces.CleanerReleasesForgottenWrappersExactlyOnce", () -> {
            long destroyed = GetDestroyedCounterCount();
            WeakReference<ITestCounter> closed = createClosedCounter();
            equal(destroyed + 1, GetDestroyedCounterCount());
            WeakReference<ITestCounter> forgotten = createForgottenCounter();
            long deadline = java.lang.System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
            while (closed.get() != null || forgotten.get() != null || GetDestroyedCounterCount() < destroyed + 2) {
                if (java.lang.System.nanoTime() >= deadline)
                    throw new AssertionError("Cleaner did not release the unreachable wrapper within 30 seconds");
                // Explicit GC is only a request. Wait for observable native
                // destruction, not one java.lang.System.gc() call or a fixed sleep.
                java.lang.System.gc();
                try {
                    Thread.sleep(20);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new AssertionError(interrupted);
                }
            }
            equal(destroyed + 2, GetDestroyedCounterCount());
            equal(0L, GetLiveCounterCount());
        });
    }
}
