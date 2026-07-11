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
#include "Assets/RadientFilesystemAssetResolver.hpp"

#include "FileSystem.hpp"
#include "TempDirectory.hpp"
#include "gtest/gtest.h"

#include <fstream>
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
