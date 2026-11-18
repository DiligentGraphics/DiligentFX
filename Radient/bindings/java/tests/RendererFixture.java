// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.tests.TestSupport.*;

// Matches renderer_tests.mjs: a real CPU-only engine and attachment-free target.
final class RendererFixture implements AutoCloseable {
    private final Resources resources = new Resources();
    final IRadientEngine engine;
    final IRadientRenderer renderer;
    final IRadientRenderTarget target;
    final IRadientScene scene;
    final IRadientView view;

    RendererFixture() {
        try {
            RadientEngineCreateInfo info = new RadientEngineCreateInfo();
            info.WorkerThreadCount = 1;
            var createdEngine = CreateRadientEngine(info);
            engine = value(createdEngine.ppEngine, createdEngine.returnValue);
            RadientRendererDesc rendererDesc = new RadientRendererDesc();
            rendererDesc.Name = Fixtures.string("renderer.name");
            rendererDesc.MultiDrawBatchSize = Fixtures.integer("renderer.batchSize");
            var createdRenderer = engine.CreateRenderer(rendererDesc);
            renderer = value(createdRenderer.ppRenderer, createdRenderer.returnValue);
            RadientRenderTargetDesc targetDesc = new RadientRenderTargetDesc();
            targetDesc.Name = Fixtures.string("renderer.targetName");
            var createdTarget = renderer.CreateRenderTarget(targetDesc);
            target = value(createdTarget.ppTarget, createdTarget.returnValue);
            RadientSceneCreateInfo sceneInfo = new RadientSceneCreateInfo();
            sceneInfo.Desc.Name = Fixtures.string("renderer.sceneName");
            var createdScene = engine.CreateScene(sceneInfo);
            scene = value(createdScene.ppScene, createdScene.returnValue);
            RadientViewDesc viewDesc = new RadientViewDesc();
            viewDesc.Name = Fixtures.string("renderer.viewName");
            viewDesc.pScene = scene;
            viewDesc.pRenderTarget = target;
            var createdView = renderer.CreateView(viewDesc);
            view = value(createdView.ppView, createdView.returnValue);
        } catch (RuntimeException | Error error) {
            close();
            throw error;
        }
    }

    private <T extends AutoCloseable> T value(T object, int status) {
        resources.keep(object);
        ok(status);
        check(object != null);
        return object;
    }

    RadientViewDesc snapshot() {
        var desc = view.GetDesc();
        resources.keep(desc.pScene);
        resources.keep(desc.pRenderTarget);
        resources.keep(desc.Environment.pEnvironmentMap);
        resources.keep(desc.Skybox.pTexture);
        return desc;
    }

    @Override public void close() { resources.close(); }
}
