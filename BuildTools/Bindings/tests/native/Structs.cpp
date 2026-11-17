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

#include "Structs.h"

namespace Diligent
{
Bool CheckDefaultDesc(const MarshalDesc& Desc)
{
    return Desc.Size == 0xFEDCBA9876543210ull && Desc.Count == -13 && Desc.Scale == 1.25f &&
        Desc.Weight == -2.5 && Desc.Enabled && Desc.Mode == MARSHAL_MODE_LAST;
}
Bool CheckCustomDesc(const MarshalDesc& Desc)
{
    return Desc.Size == 0x123456789ABCDEF0ull && Desc.Count == 19 && Desc.Scale == -3.5f &&
        Desc.Weight == 0.125 && !Desc.Enabled && Desc.Mode == MARSHAL_MODE_FIRST;
}
} // namespace Diligent
