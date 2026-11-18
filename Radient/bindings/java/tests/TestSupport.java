// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.Arrays;
import java.util.Objects;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;

final class TestSupport {
    private TestSupport() {}

    static void run(String name, Runnable test) {
        System.out.println("[ RUN      ] " + name);
        test.run();
        System.out.println("[       OK ] " + name);
    }

    static void equal(Object expected, Object actual) {
        if (!Objects.deepEquals(expected, actual)) {
            throw new AssertionError("Expected " + Arrays.deepToString(new Object[]{expected})
                + ", got " + Arrays.deepToString(new Object[]{actual}));
        }
    }

    static void check(boolean condition) {
        if (!condition) throw new AssertionError("Condition was false");
    }

    static void ok(int status) { equal(RADIENT_STATUS_OK, status); }

    static void throwsType(Class<? extends Throwable> type, Runnable operation) {
        try { operation.run(); }
        catch (Throwable error) {
            if (type.isInstance(error)) return;
            throw new AssertionError("Expected " + type.getName(), error);
        }
        throw new AssertionError("Expected " + type.getName());
    }

    // All operations use the production engine with an internally owned worker.
    // Cleanup also handles failed assertions during fixture construction.
    static final class SceneFixture implements AutoCloseable {
        IRadientEngine engine;
        IRadientScene scene;
        IRadientSceneWriter writer;

        SceneFixture() {
            try {
                RadientEngineCreateInfo info = new RadientEngineCreateInfo();
                info.WorkerThreadCount = 1;
                var created = CreateRadientEngine(info);
                engine = created.ppEngine;
                ok(created.returnValue);
                check(engine != null);
                RadientSceneCreateInfo sceneInfo = new RadientSceneCreateInfo();
                sceneInfo.Desc.Name = "Scène 世界";
                var createdScene = engine.CreateScene(sceneInfo);
                scene = createdScene.ppScene;
                ok(createdScene.returnValue);
                check(scene != null);
                var createdWriter = engine.CreateSceneWriter(scene);
                writer = createdWriter.ppWriter;
                ok(createdWriter.returnValue);
                check(writer != null);
            } catch (RuntimeException | Error error) {
                close();
                throw error;
            }
        }

        long entity(RadientEntityDesc desc) {
            var result = writer.CreateEntity(desc);
            ok(result.returnValue);
            check(result.Entity != InvalidRadientEntityID);
            return result.Entity;
        }

        @Override public void close() {
            try { if (writer != null) writer.close(); }
            finally {
                try { if (scene != null) scene.close(); }
                finally { if (engine != null) engine.close(); }
            }
        }
    }
}
