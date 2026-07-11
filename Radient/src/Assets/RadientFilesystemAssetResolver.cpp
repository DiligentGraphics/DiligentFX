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

#include "Assets/RadientFilesystemAssetResolver.hpp"

#include "FileSystem.hpp"
#include "FileWrapper.hpp"
#include "RefCntAutoPtr.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

// {C2AADFDA-8741-404A-85C2-C21E20C5D727}
static constexpr INTERFACE_ID IID_RadientFilesystemAssetLocation =
    {0xc2aadfda, 0x8741, 0x404a, {0x85, 0xc2, 0xc2, 0x1e, 0x20, 0xc5, 0xd7, 0x27}};

class RadientFilesystemAssetLocation final : public ObjectBase<IRadientAssetLocation>
{
public:
    using TBase = ObjectBase<IRadientAssetLocation>;

    RadientFilesystemAssetLocation(IReferenceCounters* pRefCounters,
                                   std::string         Location,
                                   std::string         ReadPath) :
        TBase{pRefCounters},
        m_Location{std::move(Location)},
        m_ReadPath{std::move(ReadPath)}
    {
    }

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientAssetLocation, IID_RadientFilesystemAssetLocation, TBase)

    virtual const Char* DILIGENT_CALL_TYPE GetLocation() const override final
    {
        return m_Location.c_str();
    }

    const std::string& GetReadPath() const
    {
        return m_ReadPath;
    }

private:
    std::string m_Location;
    std::string m_ReadPath;
};

// Owns resolved bytes together with the canonical URI that identifies them.
class RadientAssetDataImpl final : public ObjectBase<IRadientAssetData>
{
public:
    using TBase = ObjectBase<IRadientAssetData>;

    RadientAssetDataImpl(IReferenceCounters* pRefCounters,
                         std::vector<Uint8>  Data,
                         std::string         ResolvedURI) :
        TBase{pRefCounters},
        m_Data{std::move(Data)},
        m_ResolvedURI{std::move(ResolvedURI)}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetData, TBase)

    virtual const void* DILIGENT_CALL_TYPE GetData() const override final
    {
        return !m_Data.empty() ? m_Data.data() : nullptr;
    }

    virtual size_t DILIGENT_CALL_TYPE GetSize() const override final
    {
        return m_Data.size();
    }

    virtual const Char* DILIGENT_CALL_TYPE GetResolvedURI() const override final
    {
        return m_ResolvedURI.c_str();
    }

private:
    std::vector<Uint8> m_Data;
    std::string        m_ResolvedURI;
};

} // namespace

RadientFilesystemAssetResolver::RadientFilesystemAssetResolver(IReferenceCounters* pRefCounters) :
    TBase{pRefCounters}
{
}

bool RadientFilesystemAssetResolver::HasURIScheme(const std::string& URI)
{
    const size_t ColonPos = URI.find(':');
    if (ColonPos == std::string::npos)
        return false;

    const size_t SlashPos = URI.find_first_of("/\\");
    if (SlashPos != std::string::npos && SlashPos < ColonPos)
        return false;

    return !(ColonPos == 1 && std::isalpha(static_cast<unsigned char>(URI[0])));
}

std::string RadientFilesystemAssetResolver::GetBaseDirectory(const char* BaseURI)
{
    if (BaseURI == nullptr || *BaseURI == 0)
        return {};

    std::string  Base{BaseURI};
    const size_t SlashPos = Base.find_last_of("/\\");
    if (SlashPos == std::string::npos)
        return {};

    return Base.substr(0, SlashPos + 1);
}

std::string RadientFilesystemAssetResolver::ResolveFilesystemURI(const char* URI, const char* BaseURI)
{
    std::string Path{URI != nullptr ? URI : ""};
    if (Path.empty())
        return {};

    constexpr const char* FileScheme = "file://";
    if (Path.compare(0, std::char_traits<char>::length(FileScheme), FileScheme) == 0)
        Path.erase(0, std::char_traits<char>::length(FileScheme));

    if (!FileSystem::IsPathAbsolute(Path.c_str()) && !HasURIScheme(Path))
        Path = GetBaseDirectory(BaseURI) + Path;

    return FileSystem::SimplifyPath(Path.c_str());
}

std::string RadientFilesystemAssetResolver::ResolveFilesystemPathForRead(const char* URI, const char* BaseURI)
{
    std::string Path{URI != nullptr ? URI : ""};
    if (Path.empty())
        return {};

    constexpr const char* FileScheme = "file://";
    if (Path.compare(0, std::char_traits<char>::length(FileScheme), FileScheme) == 0)
        Path.erase(0, std::char_traits<char>::length(FileScheme));

    if (!FileSystem::IsPathAbsolute(Path.c_str()) && !HasURIScheme(Path))
        Path = GetBaseDirectory(BaseURI) + Path;

    return Path;
}

RADIENT_STATUS RadientFilesystemAssetResolver::CheckAsset(IRadientAssetLocation* pLocation)
{
    if (pLocation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<RadientFilesystemAssetLocation> pFilesystemLocation{pLocation, IID_RadientFilesystemAssetLocation};
    if (pFilesystemLocation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const char* Location = pFilesystemLocation->GetLocation();
    if (Location == nullptr || Location[0] == '\0')
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (FileSystem::FileExists(Location))
        return RADIENT_STATUS_OK;

    // Retry the original path spelling when normalization changed it.
    const std::string& ReadPath = pFilesystemLocation->GetReadPath();
    if (!ReadPath.empty() &&
        ReadPath != Location &&
        FileSystem::FileExists(ReadPath.c_str()))
    {
        return RADIENT_STATUS_OK;
    }

    return RADIENT_STATUS_NOT_FOUND;
}

RADIENT_STATUS RadientFilesystemAssetResolver::ResolveAssetLocation(const RadientAssetResolveInfo& ResolveInfo,
                                                                    IRadientAssetLocation**        ppLocation)
{
    if (ppLocation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppLocation = nullptr;

    if (ResolveInfo.URI == nullptr || *ResolveInfo.URI == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const std::string ResolvedURI = ResolveFilesystemURI(ResolveInfo.URI, ResolveInfo.BaseURI);
    if (ResolvedURI.empty() || HasURIScheme(ResolvedURI))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<RadientFilesystemAssetLocation> pLocation{
        MakeNewRCObj<RadientFilesystemAssetLocation>()(
            ResolvedURI,
            ResolveFilesystemPathForRead(ResolveInfo.URI, ResolveInfo.BaseURI))};
    pLocation->QueryInterface(IID_RadientAssetLocation, ppLocation);
    return *ppLocation != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS RadientFilesystemAssetResolver::OpenAsset(IRadientAssetLocation* pLocation,
                                                         IRadientAssetData**    ppData)
{
    if (ppData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppData = nullptr;

    if (pLocation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<RadientFilesystemAssetLocation> pFilesystemLocation{pLocation, IID_RadientFilesystemAssetLocation};
    if (pFilesystemLocation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const char* Location = pFilesystemLocation->GetLocation();
    if (Location == nullptr || Location[0] == '\0')
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::vector<Uint8> Data;
    if (!FileWrapper::ReadWholeFile(Location, Data, true))
    {
        // Retry the original path spelling, but retain Location as the asset identity.
        const std::string& ReadPath = pFilesystemLocation->GetReadPath();
        if (ReadPath.empty() ||
            ReadPath == Location ||
            !FileWrapper::ReadWholeFile(ReadPath.c_str(), Data, true))
        {
            return RADIENT_STATUS_NOT_FOUND;
        }
    }

    // The data object keeps the loaded bytes alive for all consumers.
    RefCntAutoPtr<RadientAssetDataImpl> pAssetData{
        MakeNewRCObj<RadientAssetDataImpl>()(std::move(Data), Location)};
    pAssetData->QueryInterface(IID_RadientAssetData, ppData);
    return *ppData != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
}

} // namespace Diligent
