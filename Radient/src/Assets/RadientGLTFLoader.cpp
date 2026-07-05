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

#include "Assets/RadientGLTFLoader.hpp"

#include "Assets/RadientTextureAssetManager.hpp"
#include "Errors.hpp"
#include "GLTFDocument.hpp"

#include <memory>
#include <string>

namespace Diligent
{

namespace
{

std::string MakeEmbeddedGLTFTextureURI(const std::string& SourceURI, Uint32 TextureIndex)
{
    return SourceURI + "#texture:" + std::to_string(TextureIndex);
}

void ReleaseGLTFTextureSourceData(const void*, Uint64, void* pUserData)
{
    delete static_cast<std::shared_ptr<const GLTF::Document>*>(pUserData);
}

} // namespace

namespace RadientGLTFLoader
{

TextureAssetList LoadTextures(IThreadPool&                           ThreadPool,
                              RadientTextureAssetManager&            TextureManager,
                              const std::string&                     SourceURI,
                              const std::shared_ptr<GLTF::Document>& pDocument)
{
    VERIFY_EXPR(pDocument != nullptr);
    if (pDocument == nullptr)
        return {};

    const Uint32     TextureCount = pDocument->GetTextureCount();
    TextureAssetList Textures(TextureCount);

    for (Uint32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
    {
        GLTF::TextureSourceInfo Source;
        if (!pDocument->GetTextureSourceInfo(TextureIndex, Source))
        {
            LOG_ERROR_MESSAGE("Failed to resolve GLTF texture source ", TextureIndex, " in '", SourceURI, "'");
            continue;
        }

        const std::string TextureURI =
            !Source.URI.empty() ?
            Source.URI :
            MakeEmbeddedGLTFTextureURI(SourceURI, TextureIndex);

        RadientTextureLoadInfo LoadInfo;
        LoadInfo.URI      = TextureURI.c_str();
        LoadInfo.pData    = Source.pData;
        LoadInfo.DataSize = Source.DataSize;
        LoadInfo.IsSRGB   = False;

        std::unique_ptr<std::shared_ptr<const GLTF::Document>> pDocumentOwner;
        if (Source.pData != nullptr)
        {
            // Embedded texture bytes are owned by the temporary GLTF document.
            // The release callback keeps that document alive until the texture
            // worker has created its loader/cache key from the borrowed bytes.
            pDocumentOwner                = std::make_unique<std::shared_ptr<const GLTF::Document>>(pDocument);
            LoadInfo.ReleaseData          = ReleaseGLTFTextureSourceData;
            LoadInfo.pReleaseDataUserData = pDocumentOwner.get();
        }

        TextureManager.LoadTexture(ThreadPool, LoadInfo, Textures[TextureIndex].GetAddressOfEmpty());
        if (Textures[TextureIndex] == nullptr)
        {
            LOG_ERROR_MESSAGE("Failed to create Radient texture asset for GLTF texture ", TextureIndex, " in '", SourceURI, "'");
            continue;
        }

        pDocumentOwner.release();
    }

    return Textures;
}

} // namespace RadientGLTFLoader

} // namespace Diligent
