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

#include "Scalars.h"
#include "Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

struct MarshalBufferDesc
{
    Uint64 ByteCount   DEFAULT_INITIALIZER(0);
    const void* pBytes DEFAULT_INITIALIZER(nullptr);
};
typedef struct MarshalBufferDesc MarshalBufferDesc;
typedef struct IMarshalBytes     IMarshalBytes;

static DILIGENT_CONSTEXPR INTERFACE_ID IID_MarshalBytes =
    {0x91234567, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define DILIGENT_INTERFACE_NAME IMarshalBytes
#include "DefineInterfaceHelperMacros.h"
#define IMarshalBytesInclusiveMethods \
    IObjectInclusiveMethods;          \
    IMarshalBytesMethods MarshalBytes

// clang-format off
DILIGENT_BEGIN_INTERFACE(IMarshalBytes, IObject)
{
    VIRTUAL Uint64 METHOD(Size)(THIS) CONST PURE;
    VIRTUAL MARSHAL_STATUS METHOD(MapRead)(THIS_ const void** ppData) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(UnmapRead)(THIS) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(MapWrite)(THIS_ void** ppData) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(UnmapWrite)(THIS) PURE;
    VIRTUAL MARSHAL_STATUS METHOD(Resize)(THIS_ Uint64 Size) PURE;
    VIRTUAL Uint32 METHOD(ReaderCount)(THIS) CONST PURE;
    VIRTUAL Bool METHOD(IsWriting)(THIS) CONST PURE;
    VIRTUAL Uint32 METHOD(RefCount)(THIS) CONST PURE;
    VIRTUAL void METHOD(SetReportedSize)(THIS_ Uint64 Size) PURE;
    VIRTUAL void METHOD(SetReleaseStatus)(THIS_ MARSHAL_STATUS Status) PURE;
};
DILIGENT_END_INTERFACE
#include "UndefInterfaceHelperMacros.h"
#if DILIGENT_C_INTERFACE
#    define IMarshalBytes_Size(This) CALL_IFACE_METHOD(MarshalBytes, Size, This)
#    define IMarshalBytes_MapRead(This, ...) CALL_IFACE_METHOD(MarshalBytes, MapRead, This, __VA_ARGS__)
#    define IMarshalBytes_UnmapRead(This) CALL_IFACE_METHOD(MarshalBytes, UnmapRead, This)
#endif

#include "DefineRefMacro.h"
MARSHAL_STATUS DILIGENT_GLOBAL_FUNCTION(CreateMarshalBytes)(const MarshalBufferDesc REF Desc, IMarshalBytes** ppBuffer);
Uint32 DILIGENT_GLOBAL_FUNCTION(ChecksumBytes)(const void* pBytes, Uint64 ByteCount, Bool Invert);
Bool DILIGENT_GLOBAL_FUNCTION(CheckBytes)(Uint32 ByteCount, const Uint8* pBytes);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetLiveBufferCount)(void);
Uint32 DILIGENT_GLOBAL_FUNCTION(GetBufferReleaseCount)(void);
Uint64 DILIGENT_GLOBAL_FUNCTION(GetTestHeapSize)(void);
Bool DILIGENT_GLOBAL_FUNCTION(GrowTestHeap)(void);
#include "UndefRefMacro.h"
// clang-format on

DILIGENT_END_NAMESPACE
