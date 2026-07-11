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

#include "Assets/RadientAssetResolver.hpp"

#include "FileSystem.hpp"
#include "FileWrapper.hpp"
#include "ObjectBase.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

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

// Detects non-filesystem URI schemes without treating Windows drive letters as schemes.
bool HasURIScheme(const std::string& URI)
{
    const size_t ColonPos = URI.find(':');
    if (ColonPos == std::string::npos)
        return false;

    const size_t SlashPos = URI.find_first_of("/\\");
    if (SlashPos != std::string::npos && SlashPos < ColonPos)
        return false;

    // Treat Windows drive-letter paths as filesystem paths, not URI schemes.
    return !(ColonPos == 1 && std::isalpha(static_cast<unsigned char>(URI[0])));
}

// Returns the directory portion of BaseURI, including its trailing separator.
std::string GetBaseDirectory(const char* BaseURI)
{
    if (BaseURI == nullptr || *BaseURI == 0)
        return {};

    std::string  Base{BaseURI};
    const size_t SlashPos = Base.find_last_of("/\\");
    if (SlashPos == std::string::npos)
        return {};

    return Base.substr(0, SlashPos + 1);
}

// Resolves relative paths and normalizes the result for use as the canonical asset URI.
std::string ResolveFilesystemURI(const char* URI, const char* BaseURI)
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

// Resolves the filesystem path without normalization for use as a fallback read path.
std::string ResolveFilesystemPathForRead(const char* URI, const char* BaseURI)
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

class RadientFilesystemAssetResolver final : public ObjectBase<IRadientAssetResolver>
{
public:
    using TBase = ObjectBase<IRadientAssetResolver>;

    explicit RadientFilesystemAssetResolver(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetResolver, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(const RadientAssetResolveInfo& ResolveInfo) override final
    {
        if (ResolveInfo.URI == nullptr || *ResolveInfo.URI == 0)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const std::string ResolvedURI = ResolveFilesystemURI(ResolveInfo.URI, ResolveInfo.BaseURI);
        if (ResolvedURI.empty() || HasURIScheme(ResolvedURI))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (FileSystem::FileExists(ResolvedURI.c_str()))
            return RADIENT_STATUS_OK;

        // Retry the original path spelling when normalization changed it.
        const std::string ReadPath = ResolveFilesystemPathForRead(ResolveInfo.URI, ResolveInfo.BaseURI);
        if (!ReadPath.empty() &&
            ReadPath != ResolvedURI &&
            FileSystem::FileExists(ReadPath.c_str()))
        {
            return RADIENT_STATUS_OK;
        }

        return RADIENT_STATUS_NOT_FOUND;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ResolveAsset(const RadientAssetResolveInfo& ResolveInfo,
                                                           IRadientAssetData**            ppData) override final
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppData = nullptr;

        if (ResolveInfo.URI == nullptr || *ResolveInfo.URI == 0)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const std::string ResolvedURI = ResolveFilesystemURI(ResolveInfo.URI, ResolveInfo.BaseURI);
        if (ResolvedURI.empty() || HasURIScheme(ResolvedURI))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        std::vector<Uint8> Data;
        if (!FileWrapper::ReadWholeFile(ResolvedURI.c_str(), Data, true))
        {
            // Retry the original path spelling, but retain ResolvedURI as the asset identity.
            const std::string ReadPath = ResolveFilesystemPathForRead(ResolveInfo.URI, ResolveInfo.BaseURI);
            if (ReadPath.empty() ||
                ReadPath == ResolvedURI ||
                !FileWrapper::ReadWholeFile(ReadPath.c_str(), Data, true))
            {
                return RADIENT_STATUS_NOT_FOUND;
            }
        }

        // The data object keeps the loaded bytes alive for all consumers.
        RefCntAutoPtr<RadientAssetDataImpl> pAssetData{
            MakeNewRCObj<RadientAssetDataImpl>()(std::move(Data), ResolvedURI)};
        pAssetData->QueryInterface(IID_RadientAssetData, ppData);
        return *ppData != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
    }
};

} // namespace

// Creates the built-in resolver for filesystem paths and file:// URIs.
RefCntAutoPtr<IRadientAssetResolver> CreateDefaultRadientAssetResolver()
{
    return RefCntAutoPtr<RadientFilesystemAssetResolver>{
        MakeNewRCObj<RadientFilesystemAssetResolver>()()};
}

// Preserve a user resolver when supplied, otherwise provide the built-in resolver.
RefCntAutoPtr<IRadientAssetResolver> GetRadientAssetResolverOrDefault(IRadientAssetResolver* pResolver)
{
    if (pResolver != nullptr)
        return RefCntAutoPtr<IRadientAssetResolver>{pResolver};

    return CreateDefaultRadientAssetResolver();
}

} // namespace Diligent
