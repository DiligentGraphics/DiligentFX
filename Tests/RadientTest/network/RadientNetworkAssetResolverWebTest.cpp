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

#include "RadientAssetResolver.h"
#include "RadientEngine.h"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "RadientAssetTestHelpers.hpp"
#include "RadientImportedDocument.hpp"
#include "ThreadPool.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"
#include <emscripten.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

EM_JS_DEPS(RadientNetworkTestDeps, "$stringToNewUTF8");
EM_JS(char*, RadientNetworkTestOrigin, (), { return stringToNewUTF8(location.origin + '/radient-network'); });

namespace
{
class RadientNetworkAssetResolverWebTest : public ::testing::Test
{
protected:
    void ImportDependencies(IRadientAssetResolver* SelectedResolver);

    void SetUp() override
    {
        char* Origin = RadientNetworkTestOrigin();
        Base         = Origin;
        std::free(Origin);
        RadientNetworkAssetResolverCreateInfo Info;
        Info.ConnectTimeoutMs = 2000;
        Info.RequestTimeoutMs = 5000;
        ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Resolver), RADIENT_STATUS_OK);
    }

    template <typename ActionType>
    void OnWorker(ActionType&& Action)
    {
        std::atomic<bool> Done{false};
        std::thread       Worker{[&] {
            Action();
            Done.store(true, std::memory_order_release);
        }};
        // Keep fetch promises and timeout callbacks running while the coordinator
        // and asset workers use ordinary blocking native waits.
        while (!Done.load(std::memory_order_acquire))
            TestingEnvironment::YieldExecution(1);
        Worker.join();
    }

    RADIENT_STATUS Open(const char* Path, RefCntAutoPtr<IRadientAssetData>& Data, IRadientAssetResolver* Custom = nullptr)
    {
        const auto URL = Base + Path;
        return OpenAsset(Custom ? Custom : Resolver.RawPtr(), {URL.c_str()}, Data.GetAddressOfEmpty());
    }

    struct PoolScope
    {
        RefCntAutoPtr<IThreadPool> Pool = CreateThreadPool(ThreadPoolCreateInfo{2});
        ~PoolScope()
        {
            Pool->WaitForAllTasks();
            Pool->StopThreads();
        }
    };

    std::string                          Base;
    RefCntAutoPtr<IRadientAssetResolver> Resolver;
};

TEST_F(RadientNetworkAssetResolverWebTest, ResolvesURLsAndRejectsRuntimeThreadWaits)
{
    RefCntAutoPtr<IRadientAssetLocation> Location;
    const auto                           Document = Base + "/model/scene.gltf?old=1";
    ASSERT_EQ(Resolver->ResolveAssetLocation({"../bytes#fragment", Document.c_str()}, &Location), RADIENT_STATUS_OK);
    EXPECT_EQ(Location->GetLocation(), Base + "/bytes");
    EXPECT_EQ(Resolver->CheckAsset(Location), RADIENT_STATUS_INVALID_OPERATION);
    RefCntAutoPtr<IRadientAssetData> Data;
    EXPECT_EQ(Resolver->OpenAsset(Location, &Data), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Data, nullptr);
    for (const auto& Item : {std::make_pair("?value=%23", "/model/scene.gltf?value=%23"),
                             std::make_pair("../a%2520b.bin", "/a%2520b.bin"),
                             std::make_pair("../a+b.bin", "/a+b.bin")})
    {
        Location.Release();
        ASSERT_EQ(Resolver->ResolveAssetLocation({Item.first, Document.c_str()}, &Location), RADIENT_STATUS_OK);
        EXPECT_EQ(Location->GetLocation(), Base + Item.second);
    }
    Location.Release();
    EXPECT_EQ(Resolver->ResolveAssetLocation({"file:///file"}, &Location), RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(Resolver->ResolveAssetLocation({"relative"}, &Location), RADIENT_STATUS_INVALID_ARGUMENT);
    RadientNetworkAssetResolverCreateInfo Invalid;
    Invalid.RequestTimeoutMs = 0;
    RefCntAutoPtr<IRadientAssetResolver> Other;
    EXPECT_EQ(CreateRadientNetworkAssetResolver(Invalid, &Other), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST_F(RadientNetworkAssetResolverWebTest, LoadsBytesAndMapsFailures)
{
    OnWorker([&] {
        const auto URL = Base + "/bytes";
        EXPECT_EQ(CheckAsset(Resolver, {URL.c_str()}), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetData> Data;
        ASSERT_EQ(Open("/bytes", Data), RADIENT_STATUS_OK);
        ASSERT_EQ(Data->GetSize(), 4u);
        EXPECT_EQ(std::memcmp(Data->GetData(), "A\0B\xff", 4), 0);
        EXPECT_STREQ(Data->GetResolvedURI(), URL.c_str());
        RefCntAutoPtr<IRadientAssetData> Empty;
        ASSERT_EQ(Open("/empty", Empty), RADIENT_STATUS_OK);
        EXPECT_EQ(Empty->GetSize(), 0u);
        for (const char* Path : {"/status/404", "/status/410"})
        {
            SCOPED_TRACE(Path);
            RefCntAutoPtr<IRadientAssetData> Missing;
            EXPECT_EQ(Open(Path, Missing), RADIENT_STATUS_NOT_FOUND);
            EXPECT_EQ(Missing, nullptr);
        }
        for (const char* Path : {"/status/500", "/status/206", "/truncated", "/loop", "/cors-denied"})
        {
            SCOPED_TRACE(Path);
            RefCntAutoPtr<IRadientAssetData> Failed;
            EXPECT_EQ(Open(Path, Failed), RADIENT_STATUS_FAILED);
            EXPECT_EQ(Failed, nullptr);
        }
        RefCntAutoPtr<IRadientAssetData> CrossOrigin;
        ASSERT_EQ(Open("/cors-ok", CrossOrigin), RADIENT_STATUS_OK);
        EXPECT_EQ(CrossOrigin->GetSize(), 4u);
        RefCntAutoPtr<IRadientAssetData> Compressed;
        ASSERT_EQ(Open("/gzip", Compressed), RADIENT_STATUS_OK);
        ASSERT_GT(Compressed->GetSize(), 2u);
        EXPECT_EQ(static_cast<const char*>(Compressed->GetData())[0], '{');
    });
}

TEST_F(RadientNetworkAssetResolverWebTest, TimesOutAndRecovers)
{
    OnWorker([&] {
        RadientNetworkAssetResolverCreateInfo Info;
        Info.ConnectTimeoutMs = 150;
        Info.RequestTimeoutMs = 300;
        RefCntAutoPtr<IRadientAssetResolver> Timed;
        ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Timed), RADIENT_STATUS_OK);
        for (const char* Path : {"/slow-headers", "/slow-body"})
        {
            SCOPED_TRACE(Path);
            const auto                       Start = std::chrono::steady_clock::now();
            RefCntAutoPtr<IRadientAssetData> Data;
            EXPECT_EQ(Open(Path, Data, Timed), RADIENT_STATUS_FAILED);
            EXPECT_EQ(Data, nullptr);
            EXPECT_LT(std::chrono::steady_clock::now() - Start, std::chrono::seconds{3});
            ASSERT_EQ(Open("/bytes", Data, Timed), RADIENT_STATUS_OK);
        }
        Info.MaxRedirects = 0;
        Timed.Release();
        ASSERT_EQ(CreateRadientNetworkAssetResolver(Info, &Timed), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetData> Redirect;
        EXPECT_EQ(Open("/entry.gltf", Redirect, Timed), RADIENT_STATUS_FAILED);
        EXPECT_EQ(Redirect, nullptr);
    });
}

TEST_F(RadientNetworkAssetResolverWebTest, SupportsConcurrentTransfersAndRetainedData)
{
    OnWorker([&] {
        std::vector<std::thread>                      Workers;
        std::vector<RefCntAutoPtr<IRadientAssetData>> Results(4);
        for (size_t i = 0; i < Results.size(); ++i)
            Workers.emplace_back([&, i] { EXPECT_EQ(Open("/bytes", Results[i]), RADIENT_STATUS_OK); });
        for (auto& Worker : Workers)
            Worker.join();
        Resolver.Release();
        for (const auto& Result : Results)
        {
            ASSERT_NE(Result, nullptr);
            ASSERT_EQ(Result->GetSize(), 4u);
            EXPECT_EQ(std::memcmp(Result->GetData(), "A\0B\xff", 4), 0);
        }
    });
}

void RadientNetworkAssetResolverWebTest::ImportDependencies(IRadientAssetResolver* SelectedResolver)
{
    OnWorker([&] {
        PoolScope               Work;
        RadientEngineCreateInfo Info;
        Info.pThreadPool           = Work.Pool;
        Info.Assets.pAssetResolver = SelectedResolver;
        RefCntAutoPtr<IRadientEngine> Engine;
        ASSERT_EQ(CreateRadientEngine(Info, &Engine), RADIENT_STATUS_OK);
        RefCntAutoPtr<IRadientAssetManager> Manager;
        ASSERT_EQ(Engine->GetAssetManager(&Manager), RADIENT_STATUS_OK);
        for (const auto& Item : {std::make_pair("/entry.gltf", "/model/scene.gltf"), std::make_pair("/entry.glb", "/model/123")})
        {
            SCOPED_TRACE(Item.first);
            const auto           URL = Base + Item.first;
            RadientSceneLoadInfo LoadInfo;
            LoadInfo.URI = URL.c_str();
            RefCntAutoPtr<IRadientSceneAsset> Scene;
            const auto                        Status = Manager->LoadScene(LoadInfo, &Scene);
            ASSERT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING);
            ASSERT_EQ(WaitForAssetLoad(Scene, *Work.Pool), RADIENT_STATUS_OK);
            Work.Pool->WaitForAllTasks();
            const auto* Imported = RadientAssetManagerImpl::GetImportedScene(Scene);
            ASSERT_NE(Imported, nullptr);
            ASSERT_EQ(Imported->Meshes.size(), 1u);
            ASSERT_EQ(Imported->Textures.size(), 1u);
            EXPECT_EQ(Imported->Meshes[0]->GetDesc().pGeometries[0].VertexCount, 3u);
            EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Imported->Textures[0]), RADIENT_STATUS_OK);
            EXPECT_EQ(Imported->Textures[0]->GetDesc().Width, 1u);
            EXPECT_EQ(Imported->Textures[0]->GetReference().BaseURI, Base + Item.second);
        }
        Manager->Stop(nullptr);
    });
}

TEST_F(RadientNetworkAssetResolverWebTest, ImportsGLTFAndGLBDependenciesAfterRedirect)
{
    ImportDependencies(Resolver);
}

TEST_F(RadientNetworkAssetResolverWebTest, DefaultEngineImportsNetworkDependencies)
{
    ImportDependencies(nullptr);
}

TEST_F(RadientNetworkAssetResolverWebTest, StopDuringRequestRemainsTimeoutBounded)
{
    OnWorker([&] {
        PoolScope                             Work;
        RadientNetworkAssetResolverCreateInfo ResolverInfo;
        ResolverInfo.ConnectTimeoutMs = 700;
        ResolverInfo.RequestTimeoutMs = 700;
        RefCntAutoPtr<IRadientAssetResolver> Timed;
        ASSERT_EQ(CreateRadientNetworkAssetResolver(ResolverInfo, &Timed), RADIENT_STATUS_OK);
        RadientAssetManagerImpl::CreateInfo Info;
        Info.pThreadPool           = Work.Pool;
        Info.Assets.pAssetResolver = Timed;
        auto Manager               = RadientAssetManagerImpl::Create(Info);
        ASSERT_NE(Manager, nullptr);
        const auto           URL = Base + "/pending.gltf";
        RadientSceneLoadInfo LoadInfo;
        LoadInfo.URI = URL.c_str();
        RefCntAutoPtr<IRadientSceneAsset> Scene;
        const auto                        Status = Manager->LoadScene(LoadInfo, &Scene);
        ASSERT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING);
        const auto     ActiveURL = Base + "/active";
        const auto     Start     = std::chrono::steady_clock::now();
        RADIENT_STATUS Active    = RADIENT_STATUS_NOT_FOUND;
        while (Active == RADIENT_STATUS_NOT_FOUND && std::chrono::steady_clock::now() - Start < std::chrono::seconds{2})
            Active = CheckAsset(Resolver, {ActiveURL.c_str()});
        ASSERT_EQ(Active, RADIENT_STATUS_OK);
        Manager->Stop(nullptr);
        Manager.Release();
        Timed.Release();
        Work.Pool->WaitForAllTasks();
        EXPECT_NE(GetRadientAssetLoadStatus(Scene), RADIENT_STATUS_PENDING);
        EXPECT_LT(std::chrono::steady_clock::now() - Start, std::chrono::seconds{3});
    });
}
} // namespace
