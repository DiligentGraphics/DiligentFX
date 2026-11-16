/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once
#include "BasicTypes.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)
DILIGENT_BEGIN_NAMESPACE(JavaNames)
// clang-format off
// Native declarations may legitimately share names with Java runtime classes.
struct String
{
    const Char* Text DEFAULT_INITIALIZER(nullptr);
    Int32 Value DEFAULT_INITIALIZER(37);
};
typedef struct String String;
struct System
{
    Int32 Value DEFAULT_INITIALIZER(11);
};
typedef struct System System;
struct Cleaner
{
    Int32 Value DEFAULT_INITIALIZER(11);
};
typedef struct Cleaner Cleaner;
struct Runnable
{
    Int32 Value DEFAULT_INITIALIZER(11);
};
typedef struct Runnable Runnable;
struct AutoCloseable
{
    Int32 Value DEFAULT_INITIALIZER(11);
};
typedef struct AutoCloseable AutoCloseable;
struct Override
{
    Int32 Value DEFAULT_INITIALIZER(11);
};
typedef struct Override Override;

#include "DefineRefMacro.h"
void DILIGENT_GLOBAL_FUNCTION(CopyNativeString)(const String REF Source, String REF Result);
#include "UndefRefMacro.h"
// clang-format on
DILIGENT_END_NAMESPACE
DILIGENT_END_NAMESPACE
