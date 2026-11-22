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

#include "Assets/RadientAssetData.hpp"
#include "Assets/RadientAssetResolver.hpp"
#include "Assets/RadientFilesystemAssetResolver.hpp"

#include "FileSystem.hpp"
#include "TempDirectory.hpp"
#include "gtest/gtest.h"

#include <fstream>
#include <filesystem>
#include <string>

using namespace Diligent;
using namespace Diligent::Testing;

TEST(RadientAssetResolverTest, DetectsURISchemes)
{
    EXPECT_FALSE(RadientFilesystemAssetResolver::HasURIScheme(""));
    EXPECT_FALSE(RadientFilesystemAssetResolver::HasURIScheme("asset.bin"));
    EXPECT_FALSE(RadientFilesystemAssetResolver::HasURIScheme("assets/asset:name.bin"));
    EXPECT_FALSE(RadientFilesystemAssetResolver::HasURIScheme("C:/assets/asset.bin"));
    EXPECT_FALSE(RadientFilesystemAssetResolver::HasURIScheme(R"(C:\assets\asset.bin)"));

    EXPECT_TRUE(RadientFilesystemAssetResolver::HasURIScheme("file://assets/asset.bin"));
    EXPECT_TRUE(RadientFilesystemAssetResolver::HasURIScheme("memory://assets/asset.bin"));
    EXPECT_TRUE(RadientFilesystemAssetResolver::HasURIScheme("https://example.com/asset.bin"));
}

TEST(RadientAssetResolverTest, GetsBaseDirectory)
{
    EXPECT_EQ(RadientFilesystemAssetResolver::GetBaseDirectory(nullptr), "");
    EXPECT_EQ(RadientFilesystemAssetResolver::GetBaseDirectory(""), "");
    EXPECT_EQ(RadientFilesystemAssetResolver::GetBaseDirectory("scene.gltf"), "");
    EXPECT_EQ(RadientFilesystemAssetResolver::GetBaseDirectory("assets/scene.gltf"), "assets/");
    EXPECT_EQ(RadientFilesystemAssetResolver::GetBaseDirectory(R"(assets\scene.gltf)"), R"(assets\)");
    EXPECT_EQ(RadientFilesystemAssetResolver::GetBaseDirectory("memory://assets/scene.gltf"), "memory://assets/");
}

TEST(RadientAssetResolverTest, ResolvesFilesystemURI)
{
    EXPECT_EQ(RadientFilesystemAssetResolver::ResolveFilesystemURI(nullptr, nullptr), "");
    EXPECT_EQ(RadientFilesystemAssetResolver::ResolveFilesystemURI("", "assets/scene.gltf"), "");

    EXPECT_EQ(
        RadientFilesystemAssetResolver::ResolveFilesystemURI("textures/../albedo.png", "assets/scenes/scene.gltf"),
        FileSystem::SimplifyPath("assets/scenes/textures/../albedo.png"));

    EXPECT_EQ(
        RadientFilesystemAssetResolver::ResolveFilesystemURI("file://assets/textures/albedo.png", nullptr),
        FileSystem::SimplifyPath("assets/textures/albedo.png"));

    TempDirectory     TempDir{"RadientAssetResolverTest"};
    const std::string AbsolutePath = TempDir.Get() + "/asset.bin";
    EXPECT_EQ(
        RadientFilesystemAssetResolver::ResolveFilesystemURI(AbsolutePath.c_str(), "ignored/scene.gltf"),
        FileSystem::SimplifyPath(AbsolutePath.c_str()));
}

TEST(RadientAssetResolverTest, ResolvesFilesystemPathForRead)
{
    EXPECT_EQ(RadientFilesystemAssetResolver::ResolveFilesystemPathForRead(nullptr, nullptr), "");
    EXPECT_EQ(RadientFilesystemAssetResolver::ResolveFilesystemPathForRead("", "assets/scene.gltf"), "");
    EXPECT_EQ(
        RadientFilesystemAssetResolver::ResolveFilesystemPathForRead("textures/../albedo.png", "assets/scenes/scene.gltf"),
        "assets/scenes/textures/../albedo.png");
    EXPECT_EQ(
        RadientFilesystemAssetResolver::ResolveFilesystemPathForRead("file://assets/textures/albedo.png", nullptr),
        "assets/textures/albedo.png");
}

TEST(RadientAssetResolverTest, ChecksFilesystemAssetAvailability)
{
    TempDirectory     TempDir{"RadientAssetResolverTest"};
    const std::string AssetPath = TempDir.Get() + "/asset.bin";
    const std::string BaseURI   = TempDir.Get() + "/scene.gltf";

    {
        std::ofstream AssetFile{AssetPath, std::ios::binary};
        ASSERT_TRUE(AssetFile.is_open());
        AssetFile.put('\0');
    }

    RefCntAutoPtr<IRadientAssetResolver> pResolver = CreateDefaultRadientAssetResolver();
    ASSERT_NE(pResolver, nullptr);

    EXPECT_EQ(CheckAsset(nullptr, {AssetPath.c_str(), nullptr}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(OpenAsset(pResolver, {AssetPath.c_str(), nullptr}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pResolver->CheckAsset(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientAssetLocation> pInvalidLocation;
    EXPECT_EQ(pResolver->ResolveAssetLocation(
                  {nullptr, BaseURI.c_str()},
                  pInvalidLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pResolver->ResolveAssetLocation(
                  {"", BaseURI.c_str()},
                  pInvalidLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientAssetLocation> pLocation;
    ASSERT_EQ(pResolver->ResolveAssetLocation(
                  {AssetPath.c_str(), nullptr},
                  pLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pLocation, nullptr);
    ASSERT_NE(pLocation->GetLocation(), nullptr);
    EXPECT_EQ(pLocation->GetLocation(), FileSystem::SimplifyPath(AssetPath.c_str()));
    EXPECT_EQ(pResolver->CheckAsset(pLocation), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAssetLocation> pAliasLocation;
    ASSERT_EQ(pResolver->ResolveAssetLocation(
                  {"subdir/../asset.bin", BaseURI.c_str()},
                  pAliasLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pAliasLocation, nullptr);
    EXPECT_STREQ(pAliasLocation->GetLocation(), pLocation->GetLocation());
    EXPECT_EQ(pResolver->CheckAsset(pAliasLocation), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAssetData> pAssetData;
    ASSERT_EQ(pResolver->OpenAsset(pLocation, pAssetData.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetData, nullptr);
    ASSERT_NE(pAssetData->GetData(), nullptr);
    EXPECT_EQ(pAssetData->GetSize(), 1u);
    EXPECT_EQ(*static_cast<const Uint8*>(pAssetData->GetData()), 0u);
    EXPECT_STREQ(pAssetData->GetResolvedURI(), pLocation->GetLocation());

    EXPECT_EQ(CheckAsset(pResolver, {"asset.bin", BaseURI.c_str()}), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAssetData> pConvenienceData;
    ASSERT_EQ(OpenAsset(pResolver,
                        {"asset.bin", BaseURI.c_str()},
                        pConvenienceData.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pConvenienceData, nullptr);
    EXPECT_EQ(pConvenienceData->GetSize(), 1u);
    EXPECT_STREQ(pConvenienceData->GetResolvedURI(), pLocation->GetLocation());

    // Resolving a location is intentionally metadata-only; CheckAsset performs
    // the separate existence check without loading the contents.
    RefCntAutoPtr<IRadientAssetLocation> pMissingLocation;
    ASSERT_EQ(pResolver->ResolveAssetLocation(
                  {"missing.bin", BaseURI.c_str()},
                  pMissingLocation.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pMissingLocation, nullptr);
    EXPECT_EQ(pResolver->CheckAsset(pMissingLocation), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(CheckAsset(pResolver, {"missing.bin", BaseURI.c_str()}), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientAssetResolverTest, PublicFactoryUsesBaseDirectoryAndOwnsConfiguration)
{
    TempDirectory     TempDir{"RadientResolverFactoryTest"};
    std::string       Directory = TempDir.Get();
    const std::string Path      = Directory + "/asset.bin";
    {
        std::ofstream File{Path, std::ios::binary};
        File << "data";
    }
    RadientFilesystemAssetResolverCreateInfo CI;
    CI.BaseDirectory = Directory.c_str();
    EXPECT_EQ(CreateRadientFilesystemAssetResolver(CI, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<IRadientAssetResolver> Resolver;
    ASSERT_EQ(CreateRadientFilesystemAssetResolver(CI, &Resolver), RADIENT_STATUS_OK);
    Directory.assign("changed");
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(OpenAsset(Resolver, {"asset.bin"}, &Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Data->GetSize(), 4u);
    EXPECT_EQ((std::string{static_cast<const char*>(Data->GetData()), Data->GetSize()}), "data");
    for (const auto& Request : {RadientAssetResolveInfo{Path.c_str()},
                                RadientAssetResolveInfo{"asset.bin", Path.c_str()}})
    {
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation(Request, &Location), RADIENT_STATUS_OK);
        EXPECT_STREQ(Location->GetLocation(), Data->GetResolvedURI());
    }
    RefCntAutoPtr<IRadientAssetLocation> RelativeBase;
    ASSERT_EQ(Resolver->ResolveAssetLocation({"asset.bin", "other/model.gltf"}, &RelativeBase), RADIENT_STATUS_OK);
    EXPECT_EQ(RelativeBase->GetLocation(), FileSystem::SimplifyPath("other/asset.bin"));
    Resolver.Release();
    EXPECT_EQ(Data->GetSize(), 4u);
}

TEST(RadientAssetResolverTest, CapturesRelativeBaseDirectoryAtCreation)
{
    TempDirectory TempDir{"RadientResolverDirectoryTest"};
    const auto    Root = std::filesystem::absolute(TempDir.Get());
    ASSERT_TRUE(std::filesystem::create_directory(Root / "assets"));
    ASSERT_TRUE(std::filesystem::create_directory(Root / "other"));
    const auto AssetPath = Root / "assets" / "asset.bin";
    {
        std::ofstream File{AssetPath, std::ios::binary};
        ASSERT_TRUE(File.is_open());
        File << "original";
        ASSERT_TRUE(File.good());
    }

    // Restore before TempDir is destroyed, including when ASSERT_* returns early.
    struct WorkingDirectoryGuard
    {
        const std::filesystem::path Original = std::filesystem::current_path();
        ~WorkingDirectoryGuard()
        {
            std::error_code Error;
            std::filesystem::current_path(Original, Error);
            EXPECT_FALSE(Error) << Error.message();
        }
    } RestoreDirectory;

    // Both working directories are inside the temporary tree. No relative path
    // between the process's original drive and the temporary drive is needed.
    std::filesystem::current_path(Root);
    // current_path() may resolve directory aliases (e.g. /var -> /private/var
    // on macOS). Use the same creation-time working directory as the resolver.
    const auto                               ExpectedAssetPath = std::filesystem::current_path() / "assets" / "asset.bin";
    RadientFilesystemAssetResolverCreateInfo CI;
    CI.BaseDirectory = "assets";
    RefCntAutoPtr<IRadientAssetResolver> Resolver;
    ASSERT_EQ(CreateRadientFilesystemAssetResolver(CI, &Resolver), RADIENT_STATUS_OK);

    // A resolver retaining just "assets" would now look in other/assets.
    std::filesystem::current_path(Root / "other");
    EXPECT_EQ(CheckAsset(Resolver, {"asset.bin"}), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(OpenAsset(Resolver, {"asset.bin"}, &Data), RADIENT_STATUS_OK);
    ASSERT_NE(Data, nullptr);
    ASSERT_NE(Data->GetData(), nullptr);
    EXPECT_EQ((std::string{static_cast<const char*>(Data->GetData()), Data->GetSize()}), "original");
    EXPECT_EQ(Data->GetResolvedURI(), FileSystem::SimplifyPath(ExpectedAssetPath.string().c_str()));
}

TEST(RadientAssetResolverTest, PublicFactoryDefaultsAndRejectsNetworkBase)
{
    for (const char* Directory : {static_cast<const char*>(nullptr), ""})
    {
        RadientFilesystemAssetResolverCreateInfo CI;
        CI.BaseDirectory = Directory;
        RefCntAutoPtr<IRadientAssetResolver> Resolver;
        ASSERT_EQ(CreateRadientFilesystemAssetResolver(CI, &Resolver), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation({"asset.bin"}, &Location), RADIENT_STATUS_OK);
        EXPECT_STREQ(Location->GetLocation(), "asset.bin");
    }
    RadientFilesystemAssetResolverCreateInfo CI;
    CI.BaseDirectory = "https://example.com/assets";
    RefCntAutoPtr<IRadientAssetResolver> Resolver;
    EXPECT_EQ(CreateRadientFilesystemAssetResolver(CI, &Resolver), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver, nullptr);
}

TEST(RadientAssetResolverTest, PreservesLiteralDependencyPaths)
{
    TempDirectory     TempDir{"RadientEncodedReferenceTest"};
    const std::string Directory = TempDir.Get() + "/literal%20base";
    ASSERT_TRUE(std::filesystem::create_directory(Directory));
    const std::string Base = Directory + "/document.gltf";
    const std::string Path = Directory + "/image +%20.png";
    {
        std::ofstream File{Path, std::ios::binary};
        ASSERT_TRUE(File.is_open());
        File << "image";
    }
    auto Resolver = CreateDefaultRadientAssetResolver();
    EXPECT_EQ(CheckAsset(Resolver, {"image +%20.png", Base.c_str()}), RADIENT_STATUS_OK);
    EXPECT_EQ(CheckAsset(Resolver, {Path.c_str()}), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetLocation> Location;
    EXPECT_EQ(Resolver->ResolveAssetLocation({"invalid%00name", Base.c_str()}, &Location), RADIENT_STATUS_OK);
    EXPECT_NE(Location, nullptr);
}


TEST(RadientAssetResolverTest, PreservesPercentEscapesInDependencyPaths)
{
    TempDirectory     TempDir{"RadientURIPathTest"};
    const std::string Base     = TempDir.Get() + "/document.gltf";
    auto              Resolver = CreateDefaultRadientAssetResolver();
    // Windows does not permit ':' in filenames, but resolution remains testable.
    // Linux and Web also verify that the decoded file can actually be opened.
    for (const auto& Pair : {std::make_pair("image%3Ared.png", "image%3Ared.png"),
                             std::make_pair("image%23red.png#part", "image%23red.png#part"),
                             std::make_pair("image%2520red.png", "image%2520red.png")})
    {
        SCOPED_TRACE(Pair.first);
        const std::string                    Path = TempDir.Get() + "/" + Pair.second;
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation({Pair.first, Base.c_str()}, &Location), RADIENT_STATUS_OK);
        EXPECT_EQ(Location->GetLocation(), FileSystem::SimplifyPath(Path.c_str()));
#if PLATFORM_WIN32 || PLATFORM_UNIVERSAL_WINDOWS
        if (std::strchr(Pair.second, ':') != nullptr)
            continue;
#endif
        {
            std::ofstream File{Path, std::ios::binary};
            ASSERT_TRUE(File.is_open());
            File << "data";
        }
        RefCntAutoPtr<IRadientAssetData> Data;
        ASSERT_EQ(Resolver->OpenAsset(Location, &Data), RADIENT_STATUS_OK);
        EXPECT_EQ(Data->GetSize(), 4u);
    }
    RefCntAutoPtr<IRadientAssetLocation> Location;
    ASSERT_EQ(Resolver->ResolveAssetLocation({"https://example.com/image.png", Base.c_str()}, &Location), RADIENT_STATUS_OK);
    EXPECT_STREQ(Location->GetLocation(), "https://example.com/image.png");
}

TEST(RadientAssetDataTest, AdoptsAllocationWithoutCopyAndReleasesItOnce)
{
    static int Releases = 0;
    Releases            = 0;
    RadientAssetDataImpl::OwnedBuffer Bytes{std::malloc(4), [](void* Data) noexcept {
                                                ++Releases;
                                                std::free(Data);
                                            }};
    ASSERT_NE(Bytes, nullptr);
    auto* Original = static_cast<Uint8*>(Bytes.get());
    Original[0]    = 17;
    Original[3]    = 29;
    RefCntAutoPtr<IRadientAssetData> Data{MakeNewRCObj<RadientAssetDataImpl>()(
        std::move(Bytes), size_t{4}, "https://example.com/asset.bin")};
    EXPECT_EQ(Bytes, nullptr);
    EXPECT_EQ(Data->GetData(), Original);
    EXPECT_EQ(Data->GetSize(), 4u);
    EXPECT_STREQ(Data->GetResolvedURI(), "https://example.com/asset.bin");
    RefCntAutoPtr<IRadientAssetData> Retained{Data};
    Data.Release();
    EXPECT_EQ(Releases, 0);
    EXPECT_EQ(static_cast<const Uint8*>(Retained->GetData())[0], 17u);
    EXPECT_EQ(static_cast<const Uint8*>(Retained->GetData())[3], 29u);
    Retained.Release();
    EXPECT_EQ(Releases, 1);
}

TEST(RadientAssetDataTest, AcceptsEmptyOwnedBuffer)
{
    RefCntAutoPtr<IRadientAssetData> Data{MakeNewRCObj<RadientAssetDataImpl>()(
        RadientAssetDataImpl::OwnedBuffer{nullptr, std::free}, size_t{0}, "empty")};
    EXPECT_EQ(Data->GetData(), nullptr);
    EXPECT_EQ(Data->GetSize(), 0u);
    EXPECT_STREQ(Data->GetResolvedURI(), "empty");
}

TEST(RadientAssetDataTest, PreservesVectorOwnershipWithoutCopy)
{
    std::vector<Uint8>               Bytes{1, 2, 3};
    const auto*                      Original = Bytes.data();
    RefCntAutoPtr<IRadientAssetData> Data{MakeNewRCObj<RadientAssetDataImpl>()(std::move(Bytes), "vector")};
    EXPECT_EQ(Data->GetData(), Original);
    EXPECT_EQ(Data->GetSize(), 3u);
    EXPECT_EQ(static_cast<const Uint8*>(Data->GetData())[2], 3u);
}


TEST(RadientAssetResolverTest, DefaultRoutesFilesystemPaths)
{
    auto Resolver = CreateDefaultRadientAssetResolver();
    const struct
    {
        const char* URI;
        const char* Base;
        const char* Expected;
    } Cases[] = {
        {"asset.bin", nullptr, "asset.bin"},
        {"../asset%20+red.bin", "assets/scenes/model.gltf", "assets/asset%20+red.bin"},
        {"asset.bin", "file://assets/model.gltf", "assets/asset.bin"},
        {"asset.bin", "FILE://assets/model.gltf", "assets/asset.bin"},
        {"file://assets/asset.bin", nullptr, "assets/asset.bin"},
        {"file:///local/asset.bin", "assets/model.gltf", "/local/asset.bin"},
        {"FILE://C:/assets/asset.bin", "assets/model.gltf", "C:/assets/asset.bin"},
        {"FiLe:assets/asset.bin", "https://host/model.gltf", "assets/asset.bin"},
        {"/local/asset.bin", "https://host/model.gltf", "/local/asset.bin"},
        {"C:/assets/asset.bin", "https://host/model.gltf", "C:/assets/asset.bin"},
        {"c:asset.bin", "https://host/model.gltf", "c:asset.bin"},
        {R"(C:\assets\asset.bin)", "https://host/model.gltf", R"(C:\assets\asset.bin)"},
        {R"(\\server\share\asset.bin)", "https://host/model.gltf", R"(\\server\share\asset.bin)"},
        {"//server/share/asset.bin", "https://host/model.gltf", "//server/share/asset.bin"},
        {"/local/asset.bin", "package:scene", "/local/asset.bin"},
    };
    for (const auto& Item : Cases)
    {
        SCOPED_TRACE(Item.URI);
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation({Item.URI, Item.Base}, &Location), RADIENT_STATUS_OK);
        EXPECT_EQ(Location->GetLocation(), FileSystem::SimplifyPath(Item.Expected));
    }
}

TEST(RadientAssetResolverTest, DefaultRoutesURLsAndRelativeDependencies)
{
    auto Resolver = CreateDefaultRadientAssetResolver();
    const struct
    {
        const char* URI;
        const char* Base;
        const char* Expected;
    } Cases[] = {
        {"https://host/model.gltf", nullptr, "https://host/model.gltf"},
        {"HTTP://host/model.gltf", "file://local/model.gltf", "http://host/model.gltf"},
        {"https://host/model.gltf", "custom:scene", "https://host/model.gltf"},
        {"../mesh.bin", "https://host/assets/model.gltf", "https://host/mesh.bin"},
        {"image%23red.png", "http://host/assets/model.gltf", "http://host/assets/image%23red.png"},
        {"?variant=mesh:main", "https://host/assets/model.gltf", "https://host/assets/model.gltf?variant=mesh:main"},
        {"#scene:1", "https://host/assets/model.gltf", "https://host/assets/model.gltf"},
    };
    for (const auto& Item : Cases)
    {
        SCOPED_TRACE(Item.URI);
        RefCntAutoPtr<IRadientAssetLocation> Location;
        ASSERT_EQ(Resolver->ResolveAssetLocation({Item.URI, Item.Base}, &Location), RADIENT_STATUS_OK);
        EXPECT_STREQ(Location->GetLocation(), Item.Expected);
    }
    for (const auto& Item : {RadientAssetResolveInfo{"ftp://host/file", "https://host/model"},
                             RadientAssetResolveInfo{"package:asset", nullptr},
                             RadientAssetResolveInfo{"asset.bin", "package:scene"}})
    {
        RefCntAutoPtr<IRadientAssetLocation> Location;
        EXPECT_EQ(Resolver->ResolveAssetLocation(Item, &Location), RADIENT_STATUS_UNSUPPORTED);
        EXPECT_EQ(Location, nullptr);
    }
    RefCntAutoPtr<IRadientAssetLocation> Location;
    EXPECT_EQ(Resolver->ResolveAssetLocation({nullptr}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({""}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"file"}, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientAssetResolverTest, DefaultLocationRetainsBackendAndRejectsForeignLocations)
{
    TempDirectory Temp{"RadientDefaultResolver"};
    const auto    Path = Temp.Get() + "/asset.bin";
    {
        std::ofstream File{Path, std::ios::binary};
        File << "data";
    }
    auto                                 Resolver = CreateDefaultRadientAssetResolver();
    RefCntAutoPtr<IRadientAssetLocation> Location;
    ASSERT_EQ(Resolver->ResolveAssetLocation({Path.c_str()}, &Location), RADIENT_STATUS_OK);
    Resolver.Release();
    Resolver = CreateDefaultRadientAssetResolver();
    ASSERT_EQ(Resolver->CheckAsset(Location), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetData> Data;
    ASSERT_EQ(Resolver->OpenAsset(Location, &Data), RADIENT_STATUS_OK);
    EXPECT_EQ(Data->GetSize(), 4u);
    RefCntAutoPtr<IRadientAssetResolver> Filesystem;
    ASSERT_EQ(CreateRadientFilesystemAssetResolver({}, &Filesystem), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAssetLocation> Foreign;
    ASSERT_EQ(Filesystem->ResolveAssetLocation({Path.c_str()}, &Foreign), RADIENT_STATUS_OK);
    Data.Release();
    EXPECT_EQ(Resolver->OpenAsset(Foreign, &Data), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Data, nullptr);
    EXPECT_EQ(Resolver->CheckAsset(Foreign), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->CheckAsset(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->OpenAsset(nullptr, &Data), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Resolver->OpenAsset(Location, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientAssetResolverTest, SuppliedResolverBypassesDefaultRouting)
{
    RefCntAutoPtr<IRadientAssetResolver> Filesystem;
    ASSERT_EQ(CreateRadientFilesystemAssetResolver({}, &Filesystem), RADIENT_STATUS_OK);
    auto Selected = GetRadientAssetResolverOrDefault(Filesystem);
    EXPECT_EQ(Selected, Filesystem);
    RefCntAutoPtr<IRadientAssetLocation> Location;
    EXPECT_EQ(Selected->ResolveAssetLocation({"https://host/file"}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
}
