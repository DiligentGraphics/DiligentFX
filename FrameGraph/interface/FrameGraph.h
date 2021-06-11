/*
 *  Copyright 2019-2021 Diligent Graphics LLC
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

#include "../../../DiligentCore/Primitives/interface/BasicTypes.h"
#include "../../../DiligentCore/Primitives/interface/Object.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypes.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/BufferView.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"

namespace Diligent
{

namespace FrameGraph
{

struct TextureReference
{
    TextureDesc     TexDesc;
    TextureViewDesc ViewDesc;
    //SampleDesc      SamDesc;
    RESOURCE_STATE State = RESOURCE_STATE_UNKNOWN;
};

struct BufferReference
{
    BufferDesc     BuffDesc;
    BufferViewDesc ViewDesc;
    RESOURCE_STATE State = RESOURCE_STATE_UNKNOWN;
};

struct IFrameGraph;

struct NodeDesc
{
    const char* Name = nullptr;

    const char* SubgraphName = nullptr;

    COMMAND_QUEUE_TYPE QueueType = COMMAND_QUEUE_TYPE_UNKNOWN;

    /// Additional dependencies
    Uint32 NumDepenencies = 0;

    /// The names of nodes that this node depends on
    const char* const Dependencies = nullptr;

    //ResourceDeclarations Inputs;
    //ResourceDeclarations Outputs;
};


//struct NodeResources
//{
//    Uint32         NumTextures = 0;
//    IDeviceObject* Textures    = nullptr;
//
//    Uint32         NumBuffers = 0;
//    IDeviceObject* Buffers    = nullptr;
//};
//
struct IScheduleContext
{
    virtual void AddInputTexture(const TextureReference& TexRef) = 0;

    virtual void AddInputBuffer(const BufferReference& BuffRef) = 0;

    virtual void AddOutputTexture(const TextureReference& TexRef) = 0;

    virtual void AddOutputBuffer(const BufferReference& BuffRef) = 0;
};

struct IExecuteContext
{
    virtual IDeviceContext* GetDeviceContext() = 0;

    virtual ITexture* GetInputTexture(const char* TexName) = 0;

    virtual ITextureView* GetInputTextureView(const char* ViewName) = 0;

    virtual IBuffer* GetInputBuffer(const char* BuffName) = 0;

    virtual IBufferView* GetInputBufferView(const char* BuffName) = 0;

    virtual ITexture* GetOutputTexture(const char* TexName) = 0;

    virtual ITextureView* GetOutputTextureView(const char* ViewName) = 0;

    virtual IBuffer* GetOutputBuffer(const char* BuffName) = 0;

    virtual IBufferView* GetOutputBufferView(const char* BuffName) = 0;

    virtual IResourceMapping* GetResourceMapping() = 0;
};

struct INode : public IObject
{
    //virtual const NodeDesc& GetDesc() = 0;

    virtual void Schedule(IScheduleContext* pCtx) = 0;

    virtual void Execute(IExecuteContext* pCtx) = 0;
};


struct IFrameGraph : public IObject
{
    virtual INode* GetNode(const char* Name) = 0;

    virtual void Clear(const char* SubgraphName = nullptr) = 0;

    virtual void AddNode(INode* pNode) = 0;
};


} // namespace FrameGraph

} // namespace Diligent
