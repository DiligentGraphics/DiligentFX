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

#include "RadientAssetManagerImpl.hpp"
#include "RadientTypes.h"
#include "RefCntAutoPtr.hpp"

#include "GLTFLoader.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Diligent
{

/// Renderer-owned cache for uploaded Radient asset data.
///
/// CPU asset payloads are expected to be transient: they are copied into
/// upload work items and released once the upload has been scheduled.
class RadientRenderResourceCache
{
public:
    explicit RadientRenderResourceCache(RadientAssetManagerImpl* pAssetManager);
    ~RadientRenderResourceCache();

    RADIENT_STATUS Prepare(IRenderDevice* pDevice,
                           IDeviceContext* pContext);

    RADIENT_STATUS EnsureGLTFLoaded(const RadientAssetReference& Model,
                                    IRenderDevice*               pDevice,
                                    IDeviceContext*              pContext);

    const GLTF::Model* GetGLTFModel(const RadientAssetReference& Model) const;

    IGPUUploadManager*     GetUploadManager() const;
    GLTF::ResourceManager* GetResourceManager() const;

private:
    struct GLTFResource
    {
        std::string                  SourceURI;
        std::unique_ptr<GLTF::Model> pModel;
    };

    void Reset();
    void CreateResources(IRenderDevice* pDevice,
                         IDeviceContext* pContext);
    RADIENT_STATUS PrepareGLTFResource(GLTFResource& Resource,
                                       IRenderDevice* pDevice,
                                       IDeviceContext* pContext);

    RefCntAutoPtr<RadientAssetManagerImpl> m_pAssetManager;
    RefCntAutoPtr<IRenderDevice>           m_pDevice;
    RefCntAutoPtr<IGPUUploadManager>       m_pUploadManager;
    RefCntAutoPtr<GLTF::ResourceManager>   m_pResourceManager;

    std::unordered_map<std::string, GLTFResource> m_GLTFResources;
};

} // namespace Diligent
