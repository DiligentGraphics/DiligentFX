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
#include "ObjectBase.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace Diligent
{
namespace
{
enum class ResolverRoute
{
    Relative,
    Filesystem,
    Network,
    Unsupported
};

bool IsFilesystemRoot(std::string_view URI)
{
    return !URI.empty() && (URI.front() == '/' || URI.front() == '\\' || (URI.size() >= 2 && std::isalpha(static_cast<unsigned char>(URI[0])) && URI[1] == ':'));
}

ResolverRoute GetRoute(std::string_view URI)
{
    // Windows drives (including drive-relative paths) and UNC paths are paths
    // on every platform. Query/fragment colons do not introduce a URI scheme.
    if (IsFilesystemRoot(URI))
        return ResolverRoute::Filesystem;
    const auto End = URI.find_first_of(":/\\?#");
    if (End == std::string_view::npos || URI[End] != ':')
        return ResolverRoute::Relative;
    std::string Scheme{URI.substr(0, End)};
    for (char& Ch : Scheme)
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    if (Scheme == "http" || Scheme == "https")
        return ResolverRoute::Network;
    if (Scheme == "file")
        return ResolverRoute::Filesystem;
    return ResolverRoute::Unsupported;
}

std::string GetFilesystemPath(std::string_view URI)
{
    // Preserve the filesystem resolver's literal spelling and file:// handling;
    // only remove the explicit file scheme (case-insensitively) here.
    if (GetRoute(URI) == ResolverRoute::Filesystem && !IsFilesystemRoot(URI))
    {
        URI.remove_prefix(URI.find(':') + 1);
        if (URI.substr(0, 2) == "//")
            URI.remove_prefix(2);
    }
    return std::string{URI};
}

// Locations retain both the original backend and its opaque location. Opening
// them must not reclassify an identity normalized by a backend or redirect.
constexpr INTERFACE_ID IID_DefaultAssetLocation =
    {0x6b4f05dc, 0xc318, 0x4f68, {0xa7, 0xdd, 0x9a, 0xbc, 0x12, 0xd6, 0x3f, 0x45}};

class DefaultAssetLocation final : public ObjectBase<IRadientAssetLocation>
{
public:
    using TBase = ObjectBase<IRadientAssetLocation>;
    DefaultAssetLocation(IReferenceCounters* Counters, IRadientAssetResolver* Resolver, RefCntAutoPtr<IRadientAssetLocation> Location) :
        TBase{Counters}, m_Resolver{Resolver}, m_Location{std::move(Location)} {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientAssetLocation, IID_DefaultAssetLocation, TBase)
    const Char* DILIGENT_CALL_TYPE GetLocation() const override { return m_Location->GetLocation(); }
    RADIENT_STATUS                 Check() const { return m_Resolver->CheckAsset(m_Location); }
    RADIENT_STATUS                 Open(IRadientAssetData** Data) const { return m_Resolver->OpenAsset(m_Location, Data); }

private:
    const RefCntAutoPtr<IRadientAssetResolver> m_Resolver;
    const RefCntAutoPtr<IRadientAssetLocation> m_Location;
};

class DefaultAssetResolver final : public ObjectBase<IRadientAssetResolver>
{
public:
    using TBase = ObjectBase<IRadientAssetResolver>;
    explicit DefaultAssetResolver(IReferenceCounters* Counters) : TBase{Counters}
    {
        if (CreateRadientFilesystemAssetResolver({}, &m_Filesystem) != RADIENT_STATUS_OK)
            LOG_ERROR_AND_THROW("Failed to create default Radient filesystem resolver");
#if !PLATFORM_ANDROID
        // Android requires a caller-supplied JVM. Preserve local loading when
        // no resolver is supplied; networking requires an explicit resolver.
        if (CreateRadientNetworkAssetResolver({}, &m_Network) != RADIENT_STATUS_OK)
            LOG_ERROR_AND_THROW("Failed to create default Radient network resolver");
#endif
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetResolver, TBase)

    RADIENT_STATUS DILIGENT_CALL_TYPE ResolveAssetLocation(const RadientAssetResolveInfo& Info, IRadientAssetLocation** Location) override
    {
        if (Location == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *Location = nullptr;
        if (Info.URI == nullptr || Info.URI[0] == 0)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        try
        {
            const std::string_view URI{Info.URI};
            const std::string_view Base{Info.BaseURI != nullptr ? Info.BaseURI : ""};
            const auto             URIRoute  = GetRoute(URI);
            const auto             BaseRoute = GetRoute(Base);
            const auto             Route     = URIRoute == ResolverRoute::Relative ? BaseRoute : URIRoute;
            if (Route == ResolverRoute::Unsupported)
                return RADIENT_STATUS_UNSUPPORTED;
            IRadientAssetResolver* Resolver = Route == ResolverRoute::Network ? m_Network.RawPtr() : m_Filesystem.RawPtr();
            if (Resolver == nullptr)
                return RADIENT_STATUS_UNSUPPORTED;
            RefCntAutoPtr<IRadientAssetLocation> Resolved;
            RADIENT_STATUS                       Status;
            if (Route == ResolverRoute::Network)
                Status = Resolver->ResolveAssetLocation(Info, &Resolved);
            else
            {
                const std::string Path = GetFilesystemPath(URI);
                // An explicit local path must not inherit a network/custom base.
                const bool UseBase = !IsFilesystemRoot(Path) &&
                    (BaseRoute == ResolverRoute::Filesystem || BaseRoute == ResolverRoute::Relative);
                const std::string Directory = UseBase ? GetFilesystemPath(Base) : std::string{};
                Status                      = Resolver->ResolveAssetLocation({Path.c_str(), Directory.c_str()}, &Resolved);
            }
            if (Status != RADIENT_STATUS_OK)
                return Status;
            RefCntAutoPtr<IRadientAssetLocation> Result{MakeNewRCObj<DefaultAssetLocation>()(Resolver, std::move(Resolved))};
            *Location = Result.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (...)
        {
            return RADIENT_STATUS_FAILED;
        }
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(IRadientAssetLocation* Location) override
    {
        RefCntAutoPtr<DefaultAssetLocation> Routed{Location, IID_DefaultAssetLocation};
        return Routed ? Routed->Check() : RADIENT_STATUS_INVALID_ARGUMENT;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE OpenAsset(IRadientAssetLocation* Location, IRadientAssetData** Data) override
    {
        if (Data == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *Data = nullptr;
        RefCntAutoPtr<DefaultAssetLocation> Routed{Location, IID_DefaultAssetLocation};
        return Routed ? Routed->Open(Data) : RADIENT_STATUS_INVALID_ARGUMENT;
    }

private:
    RefCntAutoPtr<IRadientAssetResolver> m_Filesystem;
    RefCntAutoPtr<IRadientAssetResolver> m_Network;
};
} // namespace

RefCntAutoPtr<IRadientAssetResolver> CreateDefaultRadientAssetResolver()
{
    return RefCntAutoPtr<IRadientAssetResolver>{MakeNewRCObj<DefaultAssetResolver>()()};
}
} // namespace Diligent
