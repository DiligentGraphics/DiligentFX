// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
#include <jni.h>
#include "gtest/gtest.h"
#include "TestingEnvironment.hpp"

namespace
{
JavaVM* TestVM = nullptr;
}

JavaVM* GetNetworkTestJavaVM()
{
    return TestVM;
}

extern "C" JNIEXPORT jint JNICALL Java_diligent_radient_tests_AndroidNetworkTest_run(JNIEnv* Env, jclass)
{
    if (Env->GetJavaVM(&TestVM) != JNI_OK)
        return -1;
    int   Count  = 1;
    char  Name[] = "RadientNetworkAndroidTest";
    char* Args[] = {Name, nullptr};
    ::testing::InitGoogleTest(&Count, Args);
    ::testing::AddGlobalTestEnvironment(new Diligent::Testing::TestingEnvironment{});
    return RUN_ALL_TESTS();
}
