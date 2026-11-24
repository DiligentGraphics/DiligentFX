// Copyright 2026 Diligent Graphics LLC. Licensed under the Apache License, Version 2.0.
#include <jni.h>
#include <cstdlib>
#include "AndroidFileSystem.hpp"

int RunRadientTests(int argc, char** argv);

namespace
{
JavaVM* TestVM = nullptr;
}

JavaVM* GetNetworkTestJavaVM()
{
    return TestVM;
}

extern "C" JNIEXPORT jint JNICALL Java_diligent_radient_tests_AndroidTest_run(JNIEnv* Env, jclass)
{
    if (Env->GetJavaVM(&TestVM) != JNI_OK)
        return -1;
    // The runner starts in the deployed asset directory. Use ordinary files,
    // without requiring an Activity or an Android asset manager.
    Diligent::AndroidFileSystem::Init(nullptr, ".", std::getenv("TMPDIR"));
    int   Count  = 1;
    char  Name[] = "RadientTest";
    char* Args[] = {Name, nullptr};
    return RunRadientTests(Count, Args);
}
