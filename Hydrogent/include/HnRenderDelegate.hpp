/*
 *  Copyright 2023 Diligent Graphics LLC
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
#include <string>
#include <atomic>

#include "pxr/imaging/hd/renderDelegate.h"

#include "RenderDevice.h"
#include "RefCntAutoPtr.hpp"
#include "HnTextureRegistry.hpp"

namespace Diligent
{

class PBR_Renderer;

namespace USD
{

class HnMaterial;
class HnMesh;

/// USD render delegate implementation in Hydrogent.
class HnRenderDelegate final : public pxr::HdRenderDelegate
{
public:
    struct CreateInfo
    {
        IRenderDevice*  pDevice        = nullptr;
        IDeviceContext* pContext       = nullptr;
        IBuffer*        pCameraAttribs = nullptr;
        IBuffer*        pLightAttribs  = nullptr;

        std::shared_ptr<PBR_Renderer> PBRRenderer;
    };
    static std::unique_ptr<HnRenderDelegate> Create(const CreateInfo& CI);

    HnRenderDelegate(const CreateInfo& CI);

    virtual ~HnRenderDelegate() override final;

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
    // \param index the render index to bind to the new renderpass.
    // \param collection the rprim collection to bind to the new renderpass.
    // \return A shared pointer to the new renderpass or empty on error.
    //
    virtual pxr::HdRenderPassSharedPtr CreateRenderPass(pxr::HdRenderIndex*           index,
                                                        pxr::HdRprimCollection const& collection) override final;


    //
    // Instancer Factory
    //

    // Request to create a new instancer.
    // \param id The unique identifier of this instancer.
    // \return A pointer to the new instancer or nullptr on error.
    virtual pxr::HdInstancer* CreateInstancer(pxr::HdSceneDelegate* delegate,
                                              pxr::SdfPath const&   id) override final;

    virtual void DestroyInstancer(pxr::HdInstancer* instancer) override final;

    ///////////////////////////////////////////////////
    //
    // Prim Factories
    //
    ///////////////////////////////////////////////////


    // Request to Allocate and Construct a new Rprim.
    // \param typeId the type identifier of the prim to allocate
    // \param rprimId a unique identifier for the prim
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdRprim* CreateRprim(pxr::TfToken const& typeId,
                                      pxr::SdfPath const& rprimId) override final;

    // Request to Destruct and deallocate the prim.
    virtual void DestroyRprim(pxr::HdRprim* rPrim) override final;

    // Request to Allocate and Construct a new Sprim.
    // \param typeId the type identifier of the prim to allocate
    // \param sprimId a unique identifier for the prim
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdSprim* CreateSprim(pxr::TfToken const& typeId,
                                      pxr::SdfPath const& sprimId) override final;

    // Request to Allocate and Construct an Sprim to use as a standin, if there
    // if an error with another another Sprim of the same type.  For example,
    // if another prim references a non-exisiting Sprim, the fallback could
    // be used.
    //
    // \param typeId the type identifier of the prim to allocate
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdSprim* CreateFallbackSprim(pxr::TfToken const& typeId) override final;

    // Request to Destruct and deallocate the prim.
    virtual void DestroySprim(pxr::HdSprim* sprim) override final;

    // Request to Allocate and Construct a new Bprim.
    // \param typeId the type identifier of the prim to allocate
    // \param sprimId a unique identifier for the prim
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdBprim* CreateBprim(pxr::TfToken const& typeId,
                                      pxr::SdfPath const& bprimId) override final;


    // Request to Allocate and Construct a Bprim to use as a standin, if there
    // if an error with another another Bprim of the same type.  For example,
    // if another prim references a non-exisiting Bprim, the fallback could
    // be used.
    //
    // \param typeId the type identifier of the prim to allocate
    // \return A pointer to the new prim or nullptr on error.
    virtual pxr::HdBprim* CreateFallbackBprim(pxr::TfToken const& typeId) override final;

    // Request to Destruct and deallocate the prim.
    virtual void DestroyBprim(pxr::HdBprim* bprim) override final;

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
    virtual void CommitResources(pxr::HdChangeTracker* tracker) override final;

    const auto& GetMeshes() const { return m_Meshes; }

    HnTextureRegistry& GetTextureRegistry() { return m_TextureRegistry; }

    const HnMaterial* GetMaterial(const char* Id) const;

    const char* GetMeshPrimId(Uint32 UID) const;

private:
    static const pxr::TfTokenVector SupportedRPrimTypes;
    static const pxr::TfTokenVector SupportedSPrimTypes;
    static const pxr::TfTokenVector SupportedBPrimTypes;

    RefCntAutoPtr<IRenderDevice>  m_pDevice;
    RefCntAutoPtr<IDeviceContext> m_pContext;
    RefCntAutoPtr<IBuffer>        m_CameraAttribsCB;
    RefCntAutoPtr<IBuffer>        m_LightAttribsCB;
    std::shared_ptr<PBR_Renderer> m_PBRRenderer;

    HnTextureRegistry m_TextureRegistry;

    std::atomic<Uint32> m_MeshUIDCounter{1};

    std::unordered_map<std::string, std::shared_ptr<HnMaterial>> m_Materials;
    std::unordered_map<std::string, std::shared_ptr<HnMesh>>     m_Meshes;
    std::unordered_map<Uint32, std::string>                      m_MeshUIDToPrimId;
};

} // namespace USD

} // namespace Diligent
