// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

public final class ConcurrentCloseTest {
    public static void main(java.lang.String[] args) {
        run("Interfaces.ActiveCallSurvivesConcurrentClose", () -> {
            long destroyed = GetDestroyedCounterCount();
            ITestCounter counter = CreateCounter(0xFEDCBA9876543210L, TEST_STATUS.TEST_STATUS_OK).ppCounter;
            AtomicLong result = new AtomicLong();
            AtomicReference<Throwable> error = new AtomicReference<>();
            ResetCounterCallGate();
            Thread reader = new Thread(() -> {
                try {
                    result.set(ReadCounterBlocked(counter));
                } catch (Throwable failure) {
                    error.set(failure);
                }
            }, "binding-counter-reader");
            // A failed test must not leave a native wait keeping the JVM alive.
            reader.setDaemon(true);
            CountDownLatch closed = new CountDownLatch(1);
            Thread closer = new Thread(() -> {
                try {
                    counter.close();
                    counter.close();
                } catch (Throwable failure) {
                    error.set(failure);
                } finally {
                    closed.countDown();
                }
            }, "binding-counter-closer");
            closer.setDaemon(true);
            try {
                reader.start();
                check(WaitForCounterCall(10000));
                closer.start();
                try {
                    check(closed.await(10, TimeUnit.SECONDS));
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new AssertionError(interrupted);
                }
                throwsType(IllegalStateException.class, counter::GetValue);
                equal(1L, GetLiveCounterCount());
                equal(destroyed, GetDestroyedCounterCount());
            } finally {
                ReleaseCounterCall();
                try {
                    reader.join(10000);
                    closer.join(10000);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new AssertionError(interrupted);
                } finally {
                    if (!reader.isAlive() && !closer.isAlive()) counter.close();
                }
            }
            check(!reader.isAlive());
            check(!closer.isAlive());
            if (error.get() != null) throw new AssertionError("Native reader failed", error.get());
            equal(0xFEDCBA9876543210L, result.get());
            equal(0L, GetLiveCounterCount());
            equal(destroyed + 1, GetDestroyedCounterCount());
        });
    }
}
