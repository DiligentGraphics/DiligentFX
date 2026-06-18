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

#include "gtest/gtest.h"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "RadientMeshPrimitives.h"
#include "RadientTestAssetHelpers.hpp"

#include "ObjectBase.hpp"
#include "ThreadPool.hpp"

#include <array>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

RefCntAutoPtr<RadientAssetManagerImpl> CreateAssetManager()
{
    RefCntAutoPtr<IThreadPool>          pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    RadientAssetManagerImpl::CreateInfo CreateInfo;
    CreateInfo.pThreadPool = pThreadPool;
    return RadientAssetManagerImpl::Create(CreateInfo);
}

void ExpectCreateMeshAccepted(RADIENT_STATUS Status)
{
    EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING);
}

void ExpectMeshLoadFinished(IRadientAssetManager* pAssetManager, IRadientMeshAsset* pMesh)
{
    const RADIENT_STATUS Status = pAssetManager->WaitForAssetLoad(pMesh);
    EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_INVALID_OPERATION);
}

void ExpectValidMeshAsset(IRadientMeshAsset* pMesh)
{
    ASSERT_NE(pMesh, nullptr);
    EXPECT_EQ(pMesh->GetType(), RADIENT_ASSET_TYPE_MESH);
    EXPECT_NE(pMesh->GetReference().URI, nullptr);
    EXPECT_NE(pMesh->GetReference().Version, 0u);
}

void ExpectColorEq(const RadientColorRGBA8& Actual, const RadientColorRGBA8& Expected)
{
    EXPECT_EQ(Actual.r, Expected.r);
    EXPECT_EQ(Actual.g, Expected.g);
    EXPECT_EQ(Actual.b, Expected.b);
    EXPECT_EQ(Actual.a, Expected.a);
}

class CaptureMeshAssetManager final : public ObjectBase<IRadientAssetManager>
{
public:
    using TBase = ObjectBase<IRadientAssetManager>;

    explicit CaptureMeshAssetManager(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetManager, TBase)

    virtual const RadientAssetManagerDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Desc;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMesh(const RadientMeshCreateInfo& MeshCI,
                                                         IRadientMeshAsset**          ppMesh) override final
    {
        if (ppMesh == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        *ppMesh         = nullptr;
        VertexCount     = MeshCI.VertexCount;
        HasVertexColors = MeshCI.pColors0 != nullptr;
        CapturedVertexColors.clear();
        if (MeshCI.pColors0 != nullptr)
            CapturedVertexColors.assign(MeshCI.pColors0, MeshCI.pColors0 + MeshCI.VertexCount);

        RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://captured-cube", ++m_NextMeshVersion);
        *ppMesh                                = pMesh.Detach();
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateMaterial(const RadientMaterialCreateInfo&,
                                                             IRadientMaterialAsset**) override final
    {
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE LoadTexture(const RadientTextureLoadInfo&,
                                                          IRadientTextureAsset**) override final
    {
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE LoadGLTF(const RadientGLTFLoadInfo&,
                                                       IRadientSceneAsset**) override final
    {
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE WaitForAssetLoad(IRadientAsset*) override final
    {
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    Uint32                         VertexCount     = 0;
    bool                           HasVertexColors = false;
    std::vector<RadientColorRGBA8> CapturedVertexColors;

private:
    RadientAssetManagerDesc m_Desc{};
    Uint64                  m_NextMeshVersion = 0;
};

} // namespace

TEST(RadientMeshPrimitivesTest, CreateCubeMesh)
{
    // Cube helper should generate a valid mesh asset through the generic asset
    // manager mesh creation path.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();

    RadientCubeMeshCreateInfo CubeCI{};
    CubeCI.Name         = "Test cube";
    CubeCI.Size         = 2.f;
    CubeCI.Subdivisions = 2;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    ExpectCreateMeshAccepted(CreateRadientCubeMesh(pAssetManager, CubeCI, &pMesh));
    ExpectValidMeshAsset(pMesh);
    ExpectMeshLoadFinished(pAssetManager, pMesh);
}

TEST(RadientMeshPrimitivesTest, CreateCubeMeshWithFaceColors)
{
    RefCntAutoPtr<CaptureMeshAssetManager> pAssetManager{MakeNewRCObj<CaptureMeshAssetManager>()()};

    const std::array<RadientColorRGBA8, 6> FaceColors{
        RadientColorRGBA8{255, 0, 0, 255},
        RadientColorRGBA8{0, 255, 0, 255},
        RadientColorRGBA8{0, 0, 255, 255},
        RadientColorRGBA8{255, 255, 0, 255},
        RadientColorRGBA8{255, 0, 255, 255},
        RadientColorRGBA8{0, 255, 255, 255},
    };

    RadientCubeMeshCreateInfo CubeCI{};
    CubeCI.Size        = 1.f;
    CubeCI.pFaceColors = FaceColors.data();

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(CreateRadientCubeMesh(pAssetManager, CubeCI, &pMesh), RADIENT_STATUS_OK);
    ExpectValidMeshAsset(pMesh);

    ASSERT_TRUE(pAssetManager->HasVertexColors);
    ASSERT_EQ(pAssetManager->VertexCount, 24u);
    ASSERT_EQ(pAssetManager->CapturedVertexColors.size(), pAssetManager->VertexCount);

    static constexpr Uint32 VerticesPerFace = 4;
    for (size_t Face = 0; Face < FaceColors.size(); ++Face)
    {
        for (size_t Vertex = 0; Vertex < VerticesPerFace; ++Vertex)
            ExpectColorEq(pAssetManager->CapturedVertexColors[Face * VerticesPerFace + Vertex], FaceColors[Face]);
    }
}

TEST(RadientMeshPrimitivesTest, CreateSphereMesh)
{
    // Sphere helper should generate indexed mesh data with positions, normals,
    // and UVs before creating the mesh asset.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();

    RadientSphereMeshCreateInfo SphereCI{};
    SphereCI.Name         = "Test sphere";
    SphereCI.Radius       = 1.5f;
    SphereCI.Subdivisions = 4;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    ExpectCreateMeshAccepted(CreateRadientSphereMesh(pAssetManager, SphereCI, &pMesh));
    ExpectValidMeshAsset(pMesh);
    ExpectMeshLoadFinished(pAssetManager, pMesh);
}

TEST(RadientMeshPrimitivesTest, RejectInvalidArguments)
{
    // Primitive helpers should reject missing output pointers, missing asset
    // managers, and invalid dimensions before creating any asset.
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();

    RadientCubeMeshCreateInfo CubeCI{};
    CubeCI.Size = 1.f;

    EXPECT_EQ(CreateRadientCubeMesh(pAssetManager, CubeCI, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_EQ(CreateRadientCubeMesh(nullptr, CubeCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);

    CubeCI.Size = 0.f;
    EXPECT_EQ(CreateRadientCubeMesh(pAssetManager, CubeCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);

    RadientSphereMeshCreateInfo SphereCI{};
    SphereCI.Radius = 0.f;
    EXPECT_EQ(CreateRadientSphereMesh(pAssetManager, SphereCI, &pMesh), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pMesh, nullptr);
}
