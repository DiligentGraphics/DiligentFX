export function createClip(f, constants, schema, property, type, values, arraySize = 1) {
    const data = new Uint8Array(values.buffer, values.byteOffset, values.byteLength);
    const times = new Float32Array(f.data.numbers("animation.times"));
    return f.keep(f.value(f.manager.CreateAnimationClip({
        Name: f.data.string("animation.name"), Duration: f.data.number("animation.duration"),
        TargetCount: 1, pTargets: [{Schema: schema, Object: 0n, Name: f.data.string("animation.targetName")}],
        SamplerCount: 1, pSamplers: [{Value: {Type: type, ArraySize: arraySize},
            pTimes: times, KeyframeCount: times.length, pValues: data, ValueDataSize: BigInt(data.length)}],
        ChannelCount: 1, pChannels: [{TargetIndex: 0, SamplerIndex: 0, Property: property}],
    })));
}

export function bindClip(f, api, clip, object, element = 0n) {
    const destination = f.keep(object.QueryInterface(api.IRadientAnimationDestination));
    const binding = f.keep(f.value(clip.CreateBinding({DestinationCount: 1, pDestinations: [{
        pDestination: destination, MappingCount: 1,
        pMappings: [{ClipTargetIndex: 0, DestinationElement: element}],
    }]})));
    destination.dispose();
    return binding;
}
