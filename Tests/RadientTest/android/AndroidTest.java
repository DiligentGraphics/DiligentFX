// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
package diligent.radient.tests;

import static diligent.radient.Radient.*;

public final class AndroidTest {
    private static native int run();

    public static void main(String[] args) {
        // Exercise generated JNI injection before testing the same resolver from
        // native workers, attached Java threads, and the shared HTTP fixtures.
        RadientNetworkAssetResolverCreateInfo info = new RadientNetworkAssetResolverCreateInfo();
        CreateRadientNetworkAssetResolverResult result = CreateRadientNetworkAssetResolver(info);
        if (result.returnValue != RADIENT_STATUS.RADIENT_STATUS_OK || result.ppResolver == null)
            throw new AssertionError("Java factory did not supply the Android JVM");
        try (IRadientAssetResolver resolver = result.ppResolver) {
            RadientEngineCreateInfo engineInfo = new RadientEngineCreateInfo();
            engineInfo.WorkerThreadCount = 2;
            engineInfo.Assets.pAssetResolver = resolver;
            CreateRadientEngineResult engine = CreateRadientEngine(engineInfo);
            if (engine.returnValue != RADIENT_STATUS.RADIENT_STATUS_OK || engine.ppEngine == null)
                throw new AssertionError("Engine rejected the network resolver");
            engine.ppEngine.close();
        }
        System.load(args[0]);
        int status = run();
        if (status == 0)
            System.out.println("Android CPU, resolver, and Java binding tests passed");
        // Preserve Google Test's exit status instead of Android's crash-handler status.
        System.exit(status);
    }
}
