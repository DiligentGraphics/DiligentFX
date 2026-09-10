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
#include "TempDirectory.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Diligent
{

namespace Testing
{

template <typename ValueType>
ValueType ReadValue(const std::vector<Uint8>& Buffer, size_t Offset)
{
    ValueType Value{};
    EXPECT_LE(Offset + sizeof(ValueType), Buffer.size());
    if (Offset + sizeof(ValueType) <= Buffer.size())
        std::memcpy(&Value, Buffer.data() + Offset, sizeof(ValueType));
    return Value;
}

template <typename ValueType, size_t Size>
std::vector<Uint8> MakeBytes(const std::array<ValueType, Size>& Values)
{
    std::vector<Uint8> Bytes(sizeof(ValueType) * Values.size());
    std::memcpy(Bytes.data(), Values.data(), Bytes.size());
    return Bytes;
}

template <typename ValueType, size_t Size>
size_t AppendBytes(std::vector<Uint8>& Buffer, const std::array<ValueType, Size>& Values)
{
    const size_t Offset = Buffer.size();
    Buffer.resize(Offset + sizeof(ValueType) * Values.size());
    std::memcpy(Buffer.data() + Offset, Values.data(), sizeof(ValueType) * Values.size());
    return Offset;
}

inline std::string WriteTextFile(const TempDirectory& TempDir,
                                 const char*          FileName,
                                 const char*          Contents)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << Contents;

    return Path;
}

inline std::string WriteTextFile(const TempDirectory& TempDir,
                                 const char*          FileName,
                                 const std::string&   Contents)
{
    return WriteTextFile(TempDir, FileName, Contents.c_str());
}

inline std::string WriteGLTFFile(const TempDirectory& TempDir,
                                 const char*          FileName,
                                 const char*          Contents)
{
    return WriteTextFile(TempDir, FileName, Contents);
}

inline std::string WriteBinaryFile(const TempDirectory&      TempDir,
                                   const char*               FileName,
                                   const std::vector<Uint8>& Data)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    if (!Data.empty())
    {
        File.write(reinterpret_cast<const char*>(Data.data()),
                   static_cast<std::streamsize>(Data.size()));
    }

    return Path;
}

} // namespace Testing

} // namespace Diligent
