// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;
import static diligent.radient.tests.TestSupport.*;

// Mirrors animation_test_helpers.mjs; calls the public bindings directly.
final class AnimationTestHelpers {
    private AnimationTestHelpers() {}

    static IRadientAnimationClipAsset createClip(AssetFixture f, INTERFACE_ID schema,
            String property, short type, byte[] values) {
        RadientAnimationClipDesc desc = new RadientAnimationClipDesc();
        desc.Name = Fixtures.string("animation.name");
        desc.Duration = Fixtures.number("animation.duration");
        RadientAnimationTargetDesc target = new RadientAnimationTargetDesc();
        target.Schema = schema;
        target.Object = 0;
        target.Name = Fixtures.string("animation.targetName");
        desc.pTargets = new RadientAnimationTargetDesc[]{target};
        desc.TargetCount = 1;
        RadientAnimationSamplerDesc sampler = new RadientAnimationSamplerDesc();
        sampler.Value.Type = type;
        sampler.Value.ArraySize = 1;
        sampler.pTimes = Fixtures.floats("animation.times");
        sampler.KeyframeCount = sampler.pTimes.length;
        sampler.pValues = values;
        sampler.ValueDataSize = values.length;
        desc.pSamplers = new RadientAnimationSamplerDesc[]{sampler};
        desc.SamplerCount = 1;
        RadientAnimationChannelDesc channel = new RadientAnimationChannelDesc();
        channel.TargetIndex = 0;
        channel.SamplerIndex = 0;
        channel.Property = property;
        desc.pChannels = new RadientAnimationChannelDesc[]{channel};
        desc.ChannelCount = 1;
        var result = f.manager.CreateAnimationClip(desc);
        return f.value(result.ppClip, result.returnValue);
    }

    static IRadientAnimationBinding bindClip(AssetFixture f, IRadientAnimationClipAsset clip,
            NativeObject object, long element) {
        var destination = f.keep(object.QueryInterface(IRadientAnimationDestination.class));
        check(destination != null);
        RadientAnimationDestinationMappingDesc mapping = new RadientAnimationDestinationMappingDesc();
        mapping.ClipTargetIndex = 0;
        mapping.DestinationElement = element;
        RadientAnimationDestinationDesc desc = new RadientAnimationDestinationDesc();
        desc.pDestination = destination;
        desc.pMappings = new RadientAnimationDestinationMappingDesc[]{mapping};
        desc.MappingCount = 1;
        RadientAnimationBindingDesc info = new RadientAnimationBindingDesc();
        info.pDestinations = new RadientAnimationDestinationDesc[]{desc};
        info.DestinationCount = 1;
        var result = clip.CreateBinding(info);
        var binding = f.value(result.ppBinding, result.returnValue);
        destination.close();
        return binding;
    }

    static RadientAnimationEvaluateInfo evaluateAt(String key) {
        RadientAnimationEvaluateInfo result = new RadientAnimationEvaluateInfo();
        result.Time = Fixtures.number(key);
        return result;
    }

    static RadientAnimationRegistryState state(AssetFixture f, IRadientAnimationRegistry registry) {
        var state = registry.GetState();
        if (state.pEntries != null) {
            for (var entry : state.pEntries) {
                f.keep(entry.pClip);
                if (entry.ppBindings != null)
                    for (var binding : entry.ppBindings) f.keep(binding);
            }
        }
        return state;
    }
}
