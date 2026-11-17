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

#include "Structs.h"
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off
struct MarshalComputedDesc
{
    Uint64 Count DEFAULT_INITIALIZER(0);
};
typedef struct MarshalComputedDesc MarshalComputedDesc;

struct MarshalComputedMatrix
{
    Float32 Data[16] DEFAULT_INITIALIZER({});
};
typedef struct MarshalComputedMatrix MarshalComputedMatrix;
typedef struct IMarshalComputedOutputs IMarshalComputedOutputs;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalComputedOutputs =
    {0xa8912456, 0x1934, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalComputedOutputs
#include "DefineInterfaceHelperMacros.h"
#define IMarshalComputedOutputsInclusiveMethods \
    IObjectInclusiveMethods;                    \
    IMarshalComputedOutputsMethods MarshalComputedOutputs

DILIGENT_BEGIN_INTERFACE(IMarshalComputedOutputs, IObject)
{
    VIRTUAL const MarshalComputedDesc REF METHOD(GetDesc)(THIS) CONST PURE;
    VIRTUAL Uint64 METHOD(GetValueCount)(THIS) CONST PURE;
    VIRTUAL void METHOD(SetCount)(THIS_ Uint64 Count) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(FillMatrices)(THIS_
        IMarshalComputedOutputs* pSource, MarshalComputedMatrix* pMatrices,
        Bool Fail DEFAULT_VALUE(False), Bool Grow DEFAULT_VALUE(False)) PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE
#    define IMarshalComputedOutputs_GetDesc(This) CALL_IFACE_METHOD(MarshalComputedOutputs, GetDesc, This)
#    define IMarshalComputedOutputs_GetValueCount(This) CALL_IFACE_METHOD(MarshalComputedOutputs, GetValueCount, This)
#    define IMarshalComputedOutputs_SetCount(This, ...) CALL_IFACE_METHOD(MarshalComputedOutputs, SetCount, This, __VA_ARGS__)
#    define IMarshalComputedOutputs_FillMatrices(This, ...) CALL_IFACE_METHOD(MarshalComputedOutputs, FillMatrices, This, __VA_ARGS__)
#endif

#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(CreateComputedOutputs)(Uint64 Count, IMarshalComputedOutputs** ppObject);
void DILIGENT_GLOBAL_FUNCTION(FillComputedValues)(Uint32* pValues, const IMarshalComputedOutputs REF Source, const MarshalDesc REF Options);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveComputedOutputs)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
