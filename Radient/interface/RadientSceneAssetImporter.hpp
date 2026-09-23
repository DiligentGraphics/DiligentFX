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

/// \file
/// C++ interface and context for implementing scene asset importers.
/// Include this header explicitly; it is not included by Radient.h.

#include "RadientImportedDocument.hpp"
#include "RadientMeshImportServices.h"

namespace Diligent
{

/// Services and resolved source bytes supplied for one scene-asset import.
///
/// All pointers are non-null and remain valid throughout Import(). The context
/// does not own them. Registered importers do not retain the asset manager, mesh
/// services, or context after the call; the manager retains the importer, so
/// retaining these services would create a reference cycle. Source data or a
/// parser's backing storage can be retained independently by reference blobs
/// while asset processing still needs their bytes.
struct RadientSceneAssetImportContext
{
    /// Already opened root document. GetResolvedURI() is its canonical URI and
    /// the base URI for resolving referenced files. The importer does not need
    /// to reopen the root document.
    IRadientAssetData* pSourceData = nullptr;

    /// Resolver used to locate and open referenced files. Use the root document's
    /// resolved URI as RadientAssetResolveInfo::BaseURI for relative references.
    IRadientAssetResolver* pAssetResolver = nullptr;

    /// Asset manager for textures, materials, skeletons, skins, and animations.
    /// Assets returned in the document are created through these services.
    IRadientAssetManager* pAssetManager = nullptr;

    /// Services for independently shared vertex, index, and morph-target data
    /// and for assembling mesh views. Pending data handles can be used immediately
    /// to create views; the importer does not wait for child assets to finish.
    IRadientMeshImportServices* pMeshImportServices = nullptr;

    /// Manager's default material for primitives without an authored material.
    /// Mesh views retain the material when it is assigned to a primitive.
    IRadientMaterialAsset* pDefaultMaterial = nullptr;
};

// {523BBC9A-EDC7-49FD-AE69-5F3F1741CF7E}
static constexpr INTERFACE_ID IID_RadientSceneAssetImporter =
    {0x523bbc9a, 0xedc7, 0x49fd, {0xae, 0x69, 0x5f, 0x3f, 0x17, 0x41, 0xcf, 0x7e}};

/// Converts an external scene format into an owning ImportedDocument.
/// Register an implementation with IRadientAssetManager::RegisterSceneAssetImporter().
///
/// Importer methods can be called concurrently on the same object. Each Import
/// call has its own context and output document; per-document parser state is
/// local to that call or otherwise synchronized by the implementation. The
/// manager retains registered importers and selected importers remain fixed for
/// each accepted load request. This interface describes file-format conversion;
/// IRadientSceneImporter separately instantiates loaded assets into a scene.
struct IRadientSceneAssetImporter : IObject
{
    /// Non-null, nonempty, case-sensitive identifier, e.g. "gltf" or "obj".
    /// The identifier is immutable and its string remains valid for the object's
    /// lifetime. Registration copies the identifier and retains the importer.
    /// Identifiers are unique within an asset manager.
    virtual const Char* DILIGENT_CALL_TYPE GetIdentifier() const = 0;

    /// Recognizes an original request URI without opening the source. URI is
    /// non-null and nonempty. Automatic selection checks a fixed snapshot of
    /// registered importers in registration order and selects the first match.
    /// Multiple matches produce an informational message. Set
    /// RadientSceneLoadInfo::ImporterId to select an importer explicitly,
    /// bypassing this call. The URI is valid only during the call.
    ///
    /// Keep this URI predicate quick. It runs without the manager's registration
    /// lock held; later registrations do not change the selection's snapshot.
    virtual Bool DILIGENT_CALL_TYPE CanImport(const Char* URI) const = 0;

    /// Converts the source into Document as an asset-loading task. The task may
    /// run on a worker or a thread calling WaitForAssetLoad; it has no guaranteed
    /// thread affinity. No importer-registration lock is held during this call.
    /// Document is initially empty. Return RADIENT_STATUS_OK only after all
    /// metadata and asset references have been populated. Child assets may still
    /// be pending; scene dependency tracking handles their completion.
    ///
    /// A negative status fails the scene load; a partially filled document is
    /// discarded. Other nonnegative statuses, including PENDING, are invalid and
    /// fail the load with RADIENT_STATUS_FAILED. Exceptions are caught by the
    /// manager and also fail the load. A failed import does not try another
    /// importer. Successful documents are moved into the scene asset; neither the
    /// output object nor the context needs to remain alive afterward.
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Import(
        const RadientSceneAssetImportContext& Context,
        RadientImport::ImportedDocument&      Document) = 0;
};

} // namespace Diligent
