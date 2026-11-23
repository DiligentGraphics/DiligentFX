// Copyright 2026 Diligent Graphics LLC
// Licensed under the Apache License, Version 2.0. See LICENSE for details.

#pragma once

#include <jni.h>
#include "RadientAssetResolver.h"

namespace Diligent
{
namespace RadientBindings
{
inline RADIENT_STATUS CreateNetworkAssetResolver(JNIEnv*                                      Env,
                                                 const RadientNetworkAssetResolverCreateInfo& CreateInfo,
                                                 IRadientAssetResolver**                      Resolver)
{
#if PLATFORM_ANDROID
    RadientNetworkAssetResolverCreateInfo AndroidInfo = CreateInfo;
    if (Env->GetJavaVM(&AndroidInfo.pJavaVM) != JNI_OK)
    {
        if (Resolver != nullptr)
            *Resolver = nullptr;
        return RADIENT_STATUS_FAILED;
    }
    return CreateRadientNetworkAssetResolver(AndroidInfo, Resolver);
#else
    (void)Env;
    return CreateRadientNetworkAssetResolver(CreateInfo, Resolver);
#endif
}
} // namespace RadientBindings
} // namespace Diligent
