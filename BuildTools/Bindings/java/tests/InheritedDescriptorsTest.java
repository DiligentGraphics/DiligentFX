// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.bindings.tests;

import static diligent.bindings.tests.BindingTests.*;
import static diligent.bindings.tests.TestSupport.*;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

// Corresponds to javascript/tests/inheriteddescriptors_tests.mjs.
public final class InheritedDescriptorsTest {
    // Hiding is legal Java and does not override the generated descriptor fields.
    // Test both fields declared directly on the generated parent and its base.
    private static final class HiddenFields extends MarshalDerivedDesc {
        public long Tag = 99;
        public long Count = 1;
        public float Scale = 99;
        public long[] pValues = {99};
    }

    private static void concurrentFirstUse() {
        // Load the JNI library before releasing the workers, without constructing
        // descriptors: their shared metadata must still initialize concurrently.
        GetArrayCallCount();
        int threadCount = 8;
        ExecutorService executor = Executors.newFixedThreadPool(threadCount);
        CountDownLatch ready = new CountDownLatch(threadCount);
        CountDownLatch start = new CountDownLatch(1);
        List<Future<?>> workers = new ArrayList<>();
        try {
            for (int thread = 0; thread < threadCount; ++thread) {
                final int id = thread;
                workers.add(executor.submit(() -> {
                    ready.countDown();
                    start.await();
                    for (int iteration = 0; iteration < 100; ++iteration) {
                        // Derived and leaf metadata share the same base IDs. Some
                        // inputs also hide those fields in an application subclass.
                        MarshalDerivedDesc input = id % 2 == 0 ? new HiddenFields() : new MarshalLeafDesc();
                        equal(0xfedcba9876543210L, input.Tag);
                        equal(2.5f, input.Scale);
                        input.Tag = id;
                        input.Scale = iteration;
                        input.Count = 2;
                        input.pValues = new long[]{id, iteration};
                        MarshalDerivedDesc copy = input instanceof MarshalLeafDesc
                            ? EchoInherited((MarshalLeafDesc)input) : EchoDerived(input);
                        equal((long)id, copy.Tag);
                        equal((float)iteration, copy.Scale);
                        equal(2L, copy.Count);
                        check(Arrays.equals(input.pValues, copy.pValues));
                        check(input.pValues != copy.pValues);
                    }
                    return null;
                }));
            }
            check(ready.await(10, TimeUnit.SECONDS));
            start.countDown();
            for (Future<?> worker : workers) worker.get(10, TimeUnit.SECONDS);
        } catch (InterruptedException error) {
            java.lang.Thread.currentThread().interrupt();
            throw new AssertionError(error);
        } catch (Exception error) {
            throw new AssertionError(error);
        } finally {
            start.countDown();
            executor.shutdownNow();
        }
    }

    public static void main(java.lang.String[] args) {
        run("InheritedDescriptors.ConcurrentFirstUse", InheritedDescriptorsTest::concurrentFirstUse);
        run("InheritedDescriptors.SubclassDefaultsIgnoreHiddenFields", () -> {
            HiddenFields hidden = new HiddenFields();
            MarshalDerivedDesc desc = hidden;
            equal(0xfedcba9876543210L, desc.Tag);
            equal(0L, desc.Count);
            equal(2.5f, desc.Scale);
            equal(null, desc.pValues);
            equal(99L, hidden.Tag);
            equal(1L, hidden.Count);
            equal(99.f, hidden.Scale);
            check(Arrays.equals(new long[]{99}, hidden.pValues));
        });
        run("InheritedDescriptors.SubclassInputsIgnoreHiddenFields", () -> {
            HiddenFields hidden = new HiddenFields();
            MarshalDerivedDesc desc = hidden;
            desc.Tag = 123;
            desc.Count = 2;
            desc.Scale = 7;
            desc.pValues = new long[]{5, 6};
            MarshalDerivedDesc copy = EchoDerived(desc);
            equal(123L, copy.Tag);
            equal(2L, copy.Count);
            equal(7.f, copy.Scale);
            check(Arrays.equals(new long[]{5, 6}, copy.pValues));
            check(copy.pValues != desc.pValues);
            desc.pValues[0] = 100;
            equal(5L, copy.pValues[0]);
            equal(99L, hidden.Tag);
            equal(1L, hidden.Count);
            equal(99.f, hidden.Scale);
            check(Arrays.equals(new long[]{99}, hidden.pValues));
        });
        run("InheritedDescriptors.DefaultsAcrossTwoBaseClasses", () -> {
            MarshalLeafDesc input = new MarshalLeafDesc();
            MarshalDerivedDesc derived = input;
            MarshalBaseDesc base = derived;
            equal(0xfedcba9876543210L, base.Tag);
            equal(0L, base.Count);
            equal(2.5f, derived.Scale);
            equal(true, input.Enabled);
            MarshalLeafDesc copy = EchoInherited(input);
            equal(base.Tag, copy.Tag);
            equal(0L, copy.Count);
            equal(derived.Scale, copy.Scale);
            equal(input.Enabled, copy.Enabled);
            equal(null, copy.pValues);
        });
        run("InheritedDescriptors.CountFieldsDriveDerivedArrays", () -> {
            MarshalLeafDesc input = new MarshalLeafDesc();
            input.Tag = 11;
            input.Scale = 7;
            input.Enabled = false;
            input.Count = 2;
            input.pValues = new long[]{2, 3};
            equal(16L, SumInherited(input));
            MarshalLeafDesc copy = EchoInherited(input);
            equal(11L, copy.Tag);
            equal(2L, copy.Count);
            equal(7.f, copy.Scale);
            equal(false, copy.Enabled);
            check(Arrays.equals(new long[]{2, 3}, copy.pValues));
            input.pValues[0] = 100;
            equal(2L, copy.pValues[0]);
            input.Count = 3;
            throwsType(IllegalArgumentException.class, () -> SumInherited(input));
            input.Count = 0;
            input.pValues = new long[0];
            equal(0, EchoInherited(input).pValues.length);
        });
    }
}
