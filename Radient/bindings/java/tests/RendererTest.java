// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import java.util.function.ToIntFunction;
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;
import static diligent.radient.tests.TestSupport.*;

public final class RendererTest {
    // Same three checks as the JS effect table, with statically typed setters.
    private static <T> void effect(T settings, ToIntFunction<T> setter, Runnable checkSnapshot) {
        ok(setter.applyAsInt(settings));
        checkSnapshot.run();
        equal(RADIENT_STATUS_NO_CHANGE, setter.applyAsInt(settings));
    }

    public static void main(String[] args) {
        run("renderer and view descriptors retain interfaces and copy settings", () -> {
            try (RendererFixture f = new RendererFixture()) {
                equal(Fixtures.string("renderer.name"), f.renderer.GetDesc().Name);
                equal(Fixtures.integer("renderer.batchSize"), f.renderer.GetDesc().MultiDrawBatchSize);
                RadientRenderTargetDesc expectedTarget = new RadientRenderTargetDesc();
                expectedTarget.Name = Fixtures.string("renderer.targetName");
                descriptorEqual(expectedTarget, f.target.GetDesc());
                RadientExtent2D expectedSize = new RadientExtent2D();
                expectedSize.Width = 0;
                expectedSize.Height = 0;
                descriptorEqual(expectedSize, f.target.GetSize().Size);
                var before = f.snapshot();
                equal(Fixtures.string("renderer.sceneName"), before.pScene.GetDesc().Name);
                equal(Fixtures.string("renderer.targetName"), before.pRenderTarget.GetDesc().Name);
                ok(f.view.SetScene(null));
                ok(f.view.SetRenderTarget(null));
                equal(null, f.snapshot().pScene);
                equal(null, f.snapshot().pRenderTarget);
                equal(Fixtures.string("renderer.sceneName"), before.pScene.GetDesc().Name);
                ok(f.view.SetScene(f.scene));
                ok(f.view.SetRenderTarget(f.target));
                equal(RADIENT_STATUS_NO_CHANGE, f.view.SetScene(f.scene));
                long camera = Fixtures.integer("renderer.camera");
                ok(f.view.SetCamera(camera));
                equal(camera, f.snapshot().Camera);
                var color = Fixtures.vector4("renderer.clearColor");
                ok(f.view.SetClearColor(color));
                color.x = Fixtures.number("renderer.mutatedColorX");
                equal(Fixtures.vector4("renderer.clearColor").x, f.snapshot().ClearColor.x);
                short debug = RADIENT_DEBUG_VISUALIZATION.RADIENT_DEBUG_VISUALIZATION_BASE_COLOR;
                ok(f.view.SetDebugVisualization(debug));
                equal(debug, f.snapshot().DebugVisualization);
                ok(f.view.SetIBLEnabled(false));
                equal(false, f.snapshot().EnableIBL);
            }
        });

        run("all view effect descriptors marshal nested settings and native defaults", () -> {
            try (RendererFixture f = new RendererFixture()) {
                RadientEnvironmentDesc environment = new RadientEnvironmentDesc();
                environment.Intensity = Fixtures.number("renderer.environmentIntensity");
                environment.Yaw = Fixtures.number("renderer.environmentYaw");
                effect(environment, f.view::SetEnvironment,
                    () -> equal(Fixtures.number("renderer.environmentIntensity"), f.snapshot().Environment.Intensity));

                RadientSkyboxDesc skybox = new RadientSkyboxDesc();
                skybox.Exposure = Fixtures.number("renderer.skyboxExposure");
                effect(skybox, f.view::SetSkybox,
                    () -> equal(Fixtures.number("renderer.skyboxExposure"), f.snapshot().Skybox.Exposure));

                float[] agx = Fixtures.floats("renderer.agx"); // Saturation, slope, power, offset.
                equal(4, agx.length);
                RadientToneMappingDesc toneMapping = new RadientToneMappingDesc();
                toneMapping.AgX.Slope = agx[1]; // Leave the other input fields at native defaults.
                RadientToneMappingAgXDesc expectedAgX = new RadientToneMappingAgXDesc();
                expectedAgX.Saturation = agx[0];
                expectedAgX.Slope = agx[1];
                expectedAgX.Power = agx[2];
                expectedAgX.Offset = agx[3];
                effect(toneMapping, f.view::SetToneMapping,
                    () -> descriptorEqual(expectedAgX, f.snapshot().ToneMapping.AgX));

                RadientBloomDesc bloom = new RadientBloomDesc();
                bloom.Enabled = true;
                bloom.Intensity = Fixtures.number("renderer.bloomIntensity");
                effect(bloom, f.view::SetBloom,
                    () -> equal(Fixtures.number("renderer.bloomIntensity"), f.snapshot().Bloom.Intensity));

                RadientTemporalAntiAliasingDesc taa = new RadientTemporalAntiAliasingDesc();
                taa.Enabled = true;
                taa.TemporalStabilityFactor = Fixtures.number("renderer.taaStability");
                effect(taa, f.view::SetTemporalAntiAliasing,
                    () -> equal(Fixtures.number("renderer.taaStability"), f.snapshot().TemporalAntiAliasing.TemporalStabilityFactor));

                RadientSSAODesc ssao = new RadientSSAODesc();
                ssao.Enabled = true;
                ssao.EffectRadius = Fixtures.number("renderer.ssaoRadius");
                effect(ssao, f.view::SetSSAO,
                    () -> equal(Fixtures.number("renderer.ssaoRadius"), f.snapshot().SSAO.EffectRadius));

                RadientSSRDesc ssr = new RadientSSRDesc();
                ssr.Enabled = true;
                ssr.MaxTraversalIntersections = Fixtures.integer("renderer.ssrIntersections");
                effect(ssr, f.view::SetSSR,
                    () -> equal(Fixtures.integer("renderer.ssrIntersections"), f.snapshot().SSR.MaxTraversalIntersections));

                RadientDepthOfFieldDesc dof = new RadientDepthOfFieldDesc();
                dof.Enabled = true;
                dof.BokehKernelRingCount = Fixtures.integer("renderer.dofRings");
                effect(dof, f.view::SetDepthOfField,
                    () -> equal(Fixtures.integer("renderer.dofRings"), f.snapshot().DepthOfField.BokehKernelRingCount));
            }
        });

        run("frame calls marshal times and view references without a GPU", () -> {
            try (RendererFixture f = new RendererFixture()) {
                RadientFrameAttribs frame = new RadientFrameAttribs();
                frame.Time = Double.parseDouble(Fixtures.string("renderer.frameTime"));
                frame.DeltaTime = Double.parseDouble(Fixtures.string("renderer.frameDeltaTime"));
                check(f.renderer.BeginFrame(frame) >= 0);
                try {
                    // A CPU target has no attachment and consequently has zero size.
                    RadientRenderAttribs render = new RadientRenderAttribs();
                    render.pView = f.view;
                    equal(RADIENT_STATUS_INVALID_ARGUMENT, f.renderer.Render(render));
                    render.pView = null;
                    throwsType(IllegalArgumentException.class, () -> f.renderer.Render(render));
                } finally {
                    ok(f.renderer.EndFrame());
                }
                check(f.renderer.BeginFrame(new RadientFrameAttribs()) >= 0);
                ok(f.renderer.EndFrame());
            }
        });
    }
}
