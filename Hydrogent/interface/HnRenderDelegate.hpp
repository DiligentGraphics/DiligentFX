/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <atomic>
#include <mutex>
#include <array>

#include "pxr/imaging/hd/renderDelegate.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentTools/TextureLoader/interface/TextureLoader.h"
#include "../../PBR/interface/USD_Renderer.hpp"

#include "entt/entity/registry.hpp"

#include "HnTypes.hpp"

namespace Diligent
{

namespace GLTF
{
class ResourceManager;
}

namespace USD
{

class HnMaterial;
class HnMesh;
class HnLight;
class HnRenderParam;
class HnShadowMapManager;
class HnTextureRegistry;
class HnGeometryPool;

/// Memory usage statistics of the render delegate.
struct HnRenderDelegateMemoryStats
{
    /// Index pool usage statistics.
    struct IndexPoolUsage
    {
        /// The total committed memory size, in bytes.
        Uint64 CommittedSize = 0;

        /// The total memory size used by all allocations, in bytes.
        Uint64 UsedSize = 0;

        /// The number of allcations.
        Uint32 AllocationCount = 0;

        /// The amount of index data pending upload, in bytes.
        Uint64 PendingDataSize = 0;
    };
    /// Index pool usage statistics.
    IndexPoolUsage IndexPool;

    /// Vertex pool usage statistics.
    struct VertexPoolUsage
    {
        /// The total committed memory size, in bytes.
        Uint64 CommittedSize = 0;

        /// The total memory size used by all allocations, in bytes.
        Uint64 UsedSize = 0;

        /// The number of allcations.
        Uint32 AllocationCount = 0;

        /// The number of vertices allocated from the pool.
        Uint64 AllocatedVertexCount = 0;

        /// The amount vertex data pending upload, in bytes.
        Uint64 PendingDataSize = 0;
    };
    /// Vertex pool usage statistics.
    VertexPoolUsage VertexPool;

    /// The number of RPrims currently loading.
    Uint32 NumRPrimsLoading = 0;

    /// Texture atlas usage statistics.
    struct TextureAtlasUsage
    {
        /// The total committed memory size, in bytes.
        Uint64 CommittedSize = 0;

        /// The number of allcations.
        Uint32 AllocationCount = 0;

        /// The total number of texels in the atlas.
        Uint64 TotalTexels = 0;

        /// The total number of texels in all allocations.
        Uint64 AllocatedTexels = 0;
    };
    TextureAtlasUsage Atlas;

    /// Texture registry usage statistics.
    struct TextureRegistryUsage
    {
        /// The number of textures currently loading.
        Uint32 NumTexturesLoading = 0;

        /// The total size of texture data currently loading, in bytes.
        Uint64 LoadingTexDataSize = 0;

        /// The size of textures loaded into the atlas, in bytes.
        Uint64 AtlasDataSize = 0;

        /// The size of separate textures, in bytes.
        Uint64 SeparateTexDataSize = 0;
    };
    TextureRegistryUsage TextureRegistry;
};

/// Rendering statistics of the render delegate.
struct HnRenderDelegateRenderingStats
{
    /// Indicates whether the loading animation is currently active.
    bool LoadingAnimationActive = false;
};

/// USD render delegate implementation in Hydrogent.
class HnRenderDelegate final : public pxr::HdRenderDelegate
{
public:
    struct CreateInfo
    {
        IRenderDevice*     pDevice           = nullptr;
        IDeviceContext*    pContext          = nullptr;
        IRenderStateCache* pRenderStateCache = nullptr;

        /// Thread pool used for asynchronous tasks (e.g. texture loading).
        /// If null, the tasks will be executed synchronously.
        IThreadPool* pThreadPool = nullptr;

        bool UseVertexPool = false;
        bool UseIndexPool  = false;
        bool EnableShadows = false;

        TEXTURE_LOAD_COMPRESS_MODE TextureCompressMode = TEXTURE_LOAD_COMPRESS_MODE_NONE;

        /// Whether to allow hot shader reload.
        ///
        /// \remarks    When hot shader reload is enabled, the renderer will need
        ///             to keep copies of generated shaders in the shader source factory.
        ///             This adds some overhead and should only be used in development mode.
        bool AllowHotShaderReload = false;

        /// Whether to use asynchronous shader and pipeline state compilation.
        ///
        /// \remarks    When set to true, the renderer will compile shaders asynchronously
        ///             in a separate thread. Until the final shader is ready, the
        ///             renderer will use a simple fallback shader.
        bool AsyncShaderCompilation = false;

        /// Whether to enable asynchronous texture loading.
        ///
        /// \remarks    If no thread pool is provided (pThreadPool is null), the flag
        ///             has no effect and texture loading will be synchronous.
        bool AsyncTextureLoading = false;

        /// Whether to pack float3 vertex normals into a 32-bit uint.
        bool PackVertexNormals = false;

        /// Whether to pack float3 vertex positions into two 32-bit uints.
        bool PackVertexPositions = false;

        /// Whether to pack float3 vertex colors into a 32-bit uint.
        bool PackVertexColors = false;

        /// When shadows are enabled, the size of the PCF kernel.
        /// Allowed values are 2, 3, 5, 7.
        Uint32 PCFKernelSize = 3;

        HN_MATERIAL_TEXTURES_BINDING_MODE TextureBindingMode = HN_MATERIAL_TEXTURES_BINDING_MODE_LEGACY;

        /// When TextureBindingMode is HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS,
        /// the texture atlas dimension.
        /// Must be a power of two between 512 and 16384.
        ///
        /// If zero, the renderer will automatically determine the atlas dimension.
        Uint32 TextureAtlasDim = 0;

        /// When TextureBindingMode is HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS,
        /// the maximum number of atlases that can be used. This corresponds to
        /// the maximum number of different material texture formats that can be
        /// used by the render delegate.
        ///
        /// When TextureBindingMode is HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC,
        /// the maximum number of material textures.
        ///
        /// If zero, the renderer will automatically determine the array size.
        Uint32 TexturesArraySize = 0;

        /// The size of the multi-draw batch. If zero, multi-draw batching is disabled.
        ///
        /// \remarks    Multi-draw batching requires the NativeMultiDraw device feature.
        ///             If the feature is not supported, the value is ignored.
        ///
        ///             The multi-draw batch size defines the size of the primitive
        ///             attributes array size in the shader.
        ///             Default value (16) is a good trade-off between the number of
        ///             draw calls that can be batched and the overhead associated with
        ///             the size of the primitive attributes array.
        Uint32 MultiDrawBatchSize = 16;

        /// The maximum number of lights that can be used by the render delegate.
        Uint32 MaxLightCount = 16;

        /// The maximum number of shadow-casting lights that can be used by the render delegate.
        Uint32 MaxShadowCastingLightCount = 8;

        /// Meters per logical unit.
        float MetersPerUnit = 1.0f;

        /// The maximum number of joints.
        ///
        /// If set to 0, skinning will be disabled.
        Uint32 MaxJointCount = 128;

        /// When asynchronous texture loading is enabled, the maximum amount of texture data
        /// that can be loaded at the same time.
        ///
        /// \remarks    The budget is used to limit the amount of texture data that can be loaded
        ///             at the same time. The budget is shared between all threads loading textures.
        ///			    If the budget is exceeded, no more textures will be loaded until some
        ///             of the previous textures are loaded and the budget is freed.
        ///
        ///             If set to 0, the budget is unlimited.
        Uint64 TextureLoadBudget = 0;

        /// The maximum amount of geometry data that can be loaded at the same time.
        ///
        /// \remarks    The budget is used to limit the amount of geometry data that can be loaded
        ///             at the same time. The budget is shared between all threads loading geometry.
        /// 		    If the budget is exceeded, no more geometry data will be loaded until some
        ///             of the previous geometry data is loaded and the budget is freed.
        ///             Meshes that exceed the budget will be synced again next frame.
        ///
        /// If set to 0, the budget is unlimited.
        Uint64 GeometryLoadBudget = 0;

        /// The number of OIT layers. If set to 0, OIT is disabled.
        Uint32 OITLayerCount = 0;
    };
    static std::unique_ptr<HnRenderDelegate> Create(const CreateInfo& CI);

    HnRenderDelegate(const CreateInfo& CI);

    virtual ~HnRenderDelegate() override final;

    // Returns an opaque handle to a render param, that in turn is
    // passed to each prim created by the render delegate during sync
    // processing.  This avoids the need to store a global state pointer
    // in each prim.
    virtual pxr::HdRenderParam* GetRenderParam() const override final;

    // Returns a list of typeId's of all supported Rprims by this render
    // delegate.
    virtual const pxr::TfTokenVector& GetSupportedRprimTypes() const override final;

    // Returns a list of typeId's of all supported Sprims by this render
    // delegate.
    virtual const pxr::TfTokenVector& GetSupportedSprimTypes() const override final;

    // Returns a list of typeId's of all supported Bprims by this render
    // delegate.
    virtual const pxr::TfTokenVector& GetSupportedBprimTypes() const override final;

    // Returns a shared ptr to the resource registry of the current render
    // delegate.
    virtual pxr::HdResourceRegistrySharedPtr GetResourceRegistry() const override final;


    //
    // Renderpass Factory
    //

    // Request to create a new renderpass.
    // \param Index      - the render index to bind to the new renderpass.
    // \param Collection - the rprim collection to bind to the new renderpass.
    // \return A shared pointer to the new renderpass or empty on error.
    //
    virtual pxr::HdRenderPassSharedPtr CreateRenderPass(pxr::HdRenderIndex*           Index,
                                                        const pxr::HdRprimCollection& Collection) override final;


    //
    // Instancer Factory
    //

    // Request to create a new instancer.
    // \param  Id - The unique identifier of this instancer.
    // \return A pointer to the new instancer or nullptr on error.
    virtual pxr::HdInstancer* CreateInstancer(pxr::HdSceneDelegate* Delegate,
                                              const pxr::SdfPath&   Id) override final;

    virtual void DestroyInstancer(pxr::HdInstancer* instancer) override final;

    ///////////////////////////////////////////////////
    //
    // Prim Factories
    //
    ///////////////////////////////////////////////////


    // Request to Allocate and Construct a new Rprim.
    // \param TypeId  - the type identifier of the prim to allocate
    // \param RPrimId - a unique identifier for the prim
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdRprim* CreateRprim(const pxr::TfToken& TypeId,
                                      const pxr::SdfPath& RPrimId) override final;

    // Request to Destruct and deallocate the prim.
    virtual void DestroyRprim(pxr::HdRprim* rPrim) override final;

    // Request to Allocate and Construct a new Sprim.
    // \param TypeId  - the type identifier of the prim to allocate
    // \param SPrimId - a unique identifier for the prim
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdSprim* CreateSprim(const pxr::TfToken& TypeId,
                                      const pxr::SdfPath& SPrimId) override final;

    // Request to Allocate and Construct an Sprim to use as a standin, if there
    // if an error with another another Sprim of the same type.  For example,
    // if another prim references a non-exisiting Sprim, the fallback could
    // be used.
    //
    // \param TypeId  - the type identifier of the prim to allocate
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdSprim* CreateFallbackSprim(const pxr::TfToken& TypeId) override final;

    // Request to Destruct and deallocate the prim.
    virtual void DestroySprim(pxr::HdSprim* sprim) override final;

    // Request to Allocate and Construct a new Bprim.
    // \param TypeId  - the type identifier of the prim to allocate
    // \param BPrimId - a unique identifier for the prim
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdBprim* CreateBprim(const pxr::TfToken& TypeId,
                                      const pxr::SdfPath& BPrimId) override final;


    // Request to Allocate and Construct a Bprim to use as a standin, if there
    // if an error with another another Bprim of the same type.  For example,
    // if another prim references a non-exisiting Bprim, the fallback could
    // be used.
    //
    // \param TypeId - the type identifier of the prim to allocate
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdBprim* CreateFallbackBprim(const pxr::TfToken& TypeId) override final;

    // Request to Destruct and deallocate the prim.
    virtual void DestroyBprim(pxr::HdBprim* BPrim) override final;

    //
    // Sync, Execute & Dispatch Hooks
    //

    // Notification point from the Engine to the delegate.
    // This notification occurs after all Sync's have completed and
    // before task execution.
    //
    // This notification gives the Render Delegate a chance to
    // update and move memory that the render may need.
    //
    // For example, the render delegate might fill primvar buffers or texture
    // memory.
    virtual void CommitResources(pxr::HdChangeTracker* Tracker) override final;

    /// Whether or not multithreaded sync is enabled for the specified prim type.
    virtual bool IsParallelSyncEnabled(pxr::TfToken primType) const override final;

    HnTextureRegistry&  GetTextureRegistry() { return *m_TextureRegistry; }
    HnGeometryPool&     GetGeometryPool() { return *m_GeometryPool; }
    HnShadowMapManager* GetShadowMapManager() const { return m_ShadowMapManager.get(); }

    const pxr::SdfPath* GetRPrimId(Uint32 UID) const;

    std::shared_ptr<USD_Renderer> GetUSDRenderer() const { return m_USDRenderer; }

    entt::registry& GetEcsRegistry() { return m_EcsRegistry; }

    GLTF::ResourceManager& GetResourceManager() const { return *m_ResourceMgr; }

    IRenderDevice*     GetDevice() const { return m_pDevice; }
    IDeviceContext*    GetDeviceContext() const { return m_pContext; }
    IRenderStateCache* GetRenderStateCache() const { return m_pRenderStateCache; }
    IBuffer*           GetFrameAttribsCB() const { return m_FrameAttribsCB; }
    IBuffer*           GetPrimitiveAttribsCB() const { return m_PrimitiveAttribsCB; }

    enum class FrameAttribsSRBType : Uint32
    {
        Opaque,
        Transparent,
        Count
    };
    IShaderResourceBinding* GetFrameAttribsSRB(FrameAttribsSRBType Type) const
    {
        return m_FrameAttribsSRBs[static_cast<size_t>(Type)];
    }

    IShaderResourceBinding* GetShadowPassFrameAttribsSRB(Uint32 LightId) const;
    Uint32                  GetShadowPassFrameAttribsOffset(Uint32 LightId) const;

    const auto& GetLights() const { return m_Lights; }

    HnRenderDelegateMemoryStats    GetMemoryStats() const;
    HnRenderDelegateRenderingStats GetRenderingStats() const;

    void SetViewMode(HN_VIEW_MODE ViewMode);
    void SetGeometryMode(HN_GEOMETRY_MODE GeometryMode);
    void SetSelectedRPrimId(const pxr::SdfPath& RPrimID);
    void SetUseShadows(bool UseShadows);

    IObject* GetMaterialSRBCache() const { return m_MaterialSRBCache; }

    HnMaterial* GetFallbackMaterial() const { return m_FallbackMaterial; }

    bool AllowPrimitiveRestart() const;

private:
    RefCntAutoPtr<IShaderResourceBinding>& _GetFrameAttribsSRB(FrameAttribsSRBType Type)
    {
        return m_FrameAttribsSRBs[static_cast<size_t>(Type)];
    }

private:
    static const pxr::TfTokenVector SupportedRPrimTypes;
    static const pxr::TfTokenVector SupportedSPrimTypes;
    static const pxr::TfTokenVector SupportedBPrimTypes;

    RefCntAutoPtr<IRenderDevice>     m_pDevice;
    RefCntAutoPtr<IDeviceContext>    m_pContext;
    RefCntAutoPtr<IRenderStateCache> m_pRenderStateCache;

    RefCntAutoPtr<GLTF::ResourceManager> m_ResourceMgr;
    RefCntAutoPtr<IBuffer>               m_PrimitiveAttribsCB;
    RefCntAutoPtr<IObject>               m_MaterialSRBCache;
    std::shared_ptr<USD_Renderer>        m_USDRenderer;

    entt::registry m_EcsRegistry;

    // Frame attributes for the main pass and all shadow passes.
    //
    // ||                   Main Pass                  ||        Shadow Pass 1       ||  ...  ||       Shadow Pass N        ||
    // || Camera|PrevCamera|Renderer|Lights|ShadowMaps || Camera|PrevCamera|Renderer ||  ...  || Camera|PrevCamera|Renderer ||
    //
    RefCntAutoPtr<IBuffer> m_FrameAttribsCB;

    std::array<RefCntAutoPtr<IShaderResourceBinding>, static_cast<size_t>(FrameAttribsSRBType::Count)> m_FrameAttribsSRBs;

    struct ShadowPassFrameAttribs
    {
        RefCntAutoPtr<IShaderResourceBinding> SRB;
        IShaderResourceVariable*              FrameAttribsVar = nullptr;
    };
    ShadowPassFrameAttribs m_ShadowPassFrameAttribs;

    RefCntAutoPtr<ITextureView> m_pDummyShadowSRV;
    USD_Renderer::OITResources  m_DummyOITResources;

    Uint32 m_MainPassFrameAttribsAlignedSize   = 0;
    Uint32 m_ShadowPassFrameAttribsAlignedSize = 0;

    std::shared_ptr<HnTextureRegistry>  m_TextureRegistry;
    std::unique_ptr<HnGeometryPool>     m_GeometryPool;
    std::unique_ptr<HnRenderParam>      m_RenderParam;
    std::unique_ptr<HnShadowMapManager> m_ShadowMapManager;

    std::atomic<Uint32>                      m_RPrimNextUID{1};
    mutable std::mutex                       m_RPrimUIDToSdfPathMtx;
    std::unordered_map<Uint32, pxr::SdfPath> m_RPrimUIDToSdfPath;

    std::mutex                  m_MeshesMtx;
    std::unordered_set<HnMesh*> m_Meshes;

    std::mutex                      m_MaterialsMtx;
    std::unordered_set<HnMaterial*> m_Materials;
    HnMaterial*                     m_FallbackMaterial = nullptr;

    std::mutex m_LightsMtx;
    // Light type -> light prim
    std::unordered_multimap<pxr::TfToken, HnLight*, pxr::TfToken::HashFunctor> m_Lights;

    Uint32 m_MeshResourcesVersion     = ~0u;
    Uint32 m_MaterialResourcesVersion = ~0u;
    Uint32 m_ShadowAtlasVersion       = ~0u;
    Uint32 m_LightResourcesVersion    = ~0u;
    Uint32 m_OITResourcesVersion      = ~0u;

    // Amount of index data pending upload before the last call to m_GeometryPool->Commit
    Uint64 m_LastPendingIndexDataSize = 0;
    // Amount of vertex data pending upload before the last call to m_GeometryPool->Commit
    Uint64 m_LastPendingVertexDataSize = 0;
};

} // namespace USD

} // namespace Diligent
