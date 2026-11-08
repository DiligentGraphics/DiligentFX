export function createClip(f, constants, schema, property, type, values, arraySize = 1) {
    const data = new Uint8Array(values.buffer, values.byteOffset, values.byteLength);
    return f.keep(f.value(f.manager.CreateAnimationClip({
        Name: "Animation 世界", Duration: 1,
        TargetCount: 1, pTargets: [{Schema: schema, Object: 0n, Name: "Target"}],
        SamplerCount: 1, pSamplers: [{Value: {Type: type, ArraySize: arraySize},
            pTimes: new Float32Array([0, 1]), KeyframeCount: 2, pValues: data, ValueDataSize: BigInt(data.length)}],
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
