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
/// Defines mesh-data creation services for scene asset importer implementations.
/// Include this header explicitly; it is not included by Radient.h.

#include "RadientAssets.h"
#include "RadientMorphTargets.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientMeshImportServices   IRadientMeshImportServices;
typedef struct IRadientMeshVertexData       IRadientMeshVertexData;
typedef struct IRadientMeshIndexData        IRadientMeshIndexData;
typedef struct IRadientMeshMorphTargetData  IRadientMeshMorphTargetData;

/// Shared data forming one drawable geometry in a mesh view.
///
/// All handles used by a mesh view belong to the service's associated asset manager.
/// They may still be loading when CreateMeshView is called; view processing waits
/// for their source processing. A view retains the handles it uses, so the caller
/// can release its own references after the call. Reusing handles shares their
/// renderer-selected storage without repacking the data for each mesh view.
struct RadientMeshGeometryData
{
    /// Required vertex-data handle. Its vertex count defines the geometry's
    /// vertex domain, including the indices and optional morph-target deltas.
    IRadientMeshVertexData* pVertexData DEFAULT_INITIALIZER(nullptr);

    /// Required index-data handle. Every index used by the geometry refers to a
    /// vertex in pVertexData; primitive ranges select elements of this index data.
    IRadientMeshIndexData* pIndexData DEFAULT_INITIALIZER(nullptr);

    /// Optional morph-target data, or nullptr for a geometry without morphing.
    /// Its vertex count matches pVertexData. All morphing geometries in one view
    /// have matching target counts, target names, default weights, and ordered
    /// attribute descriptions. Delta values and vertex counts may differ between
    /// geometries. Geometries without morph targets may appear in the same view.
    IRadientMeshMorphTargetData* pMorphTargetData DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientMeshGeometryData RadientMeshGeometryData;


/// Creates a mesh asset by selecting primitives from reusable geometry data.
///
/// CreateMeshView copies the primitive descriptions, names, and geometry mapping
/// and retains the referenced materials and geometry handles before returning.
/// The caller can then modify or release the descriptor arrays, strings, and its
/// asset references. Only geometry entries referenced by primitives are used;
/// unused entries are ignored. The resulting mesh description contains used
/// geometries in first-use order, so its geometry indices may differ from these
/// input indices. No vertex or index repacking is required to create a view.
///
/// Primitive range and morph-target compatibility checks occur after referenced
/// geometry data finishes source processing. Consequently, a view accepted with
/// RADIENT_STATUS_PENDING may later fail validation. WaitForAssetLoad reports
/// source-processing and dependency errors; success does not imply GPU readiness.
struct RadientMeshViewCreateInfo
{
    /// Array of PrimitiveCount primitive descriptions. Must be non-null. Each
    /// FirstIndex and IndexCount selects a nonempty range in the index-data handle
    /// of the primitive's selected geometry. Materials are optional.
    const RadientMeshPrimitiveCreateInfo* pPrimitives DEFAULT_INITIALIZER(nullptr);

    /// Number of elements in pPrimitives and, when provided, pGeometryIndices.
    /// Must be nonzero.
    Uint32 PrimitiveCount DEFAULT_INITIALIZER(0);

    /// Optional array mapping each primitive to an entry in pGeometryData.
    /// Every value is less than GeometryCount. If null, every primitive uses
    /// geometry zero. Multiple primitives may reference the same geometry.
    const Uint32* pGeometryIndices DEFAULT_INITIALIZER(nullptr);

    /// Array of GeometryCount geometry descriptions. Must be non-null. Handles
    /// in used entries belong to the associated asset manager; unused entries
    /// may be empty.
    const RadientMeshGeometryData* pGeometryData DEFAULT_INITIALIZER(nullptr);

    /// Number of entries in pGeometryData. Must be nonzero.
    Uint32 GeometryCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientMeshViewCreateInfo RadientMeshViewCreateInfo;


// {33B53B79-66B9-44AE-82D6-C47FE30634A3}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMeshVertexData =
    {0x33b53b79, 0x66b9, 0x44ae, {0x82, 0xd6, 0xc4, 0x7f, 0xe3, 0x06, 0x34, 0xa3}};

// {EB134756-0BAC-4BB9-8584-D55F836E7E7F}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMeshIndexData =
    {0xeb134756, 0x0bac, 0x4bb9, {0x85, 0x84, 0xd5, 0x5f, 0x83, 0x6e, 0x7e, 0x7f}};

// {DC6645D7-7BFF-4964-83AF-181C252F3BED}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMeshMorphTargetData =
    {0xdc6645d7, 0x7bff, 0x4964, {0x83, 0xaf, 0x18, 0x1c, 0x25, 0x2f, 0x3b, 0xed}};


// These interfaces add no methods to IRadientAsset. Their C vtables contain only
// the inherited methods, avoiding empty method structs in the C interface.
#if DILIGENT_C_INTERFACE

typedef struct IRadientMeshVertexDataVtbl
{
    IRadientAssetInclusiveMethods;
} IRadientMeshVertexDataVtbl;

struct IRadientMeshVertexData
{
    IRadientMeshVertexDataVtbl* pVtbl;
};

typedef struct IRadientMeshIndexDataVtbl
{
    IRadientAssetInclusiveMethods;
} IRadientMeshIndexDataVtbl;

struct IRadientMeshIndexData
{
    IRadientMeshIndexDataVtbl* pVtbl;
};

typedef struct IRadientMeshMorphTargetDataVtbl
{
    IRadientAssetInclusiveMethods;
} IRadientMeshMorphTargetDataVtbl;

struct IRadientMeshMorphTargetData
{
    IRadientMeshMorphTargetDataVtbl* pVtbl;
};

#else

/// Immutable, reusable vertex data created by IRadientMeshImportServices.
///
/// GetType returns RADIENT_ASSET_TYPE_MESH_VERTEX_DATA. This handle can be passed
/// to CreateMeshView and the associated asset manager's WaitForAssetLoad. Retaining
/// the handle retains the processed data, not a CPU copy of the source vertices.
/// WaitForAssetLoad success does not guarantee GPU resource or upload completion.
DILIGENT_BEGIN_INTERFACE(IRadientMeshVertexData, IRadientAsset)
{
};

/// Immutable, reusable index data created by IRadientMeshImportServices.
///
/// GetType returns RADIENT_ASSET_TYPE_MESH_INDEX_DATA. This handle can be passed to
/// CreateMeshView and the associated asset manager's WaitForAssetLoad. Retaining the
/// handle retains the processed data, not a CPU copy of the source indices.
/// WaitForAssetLoad success does not guarantee GPU resource or upload completion.
DILIGENT_BEGIN_INTERFACE(IRadientMeshIndexData, IRadientAsset)
{
};

/// Immutable, reusable morph-target data created by IRadientMeshImportServices.
///
/// GetType returns RADIENT_ASSET_TYPE_MESH_MORPH_TARGET_DATA. This handle can be
/// passed to CreateMeshView and the associated asset manager's WaitForAssetLoad.
/// Target descriptions remain available through mesh views that use the data;
/// retaining the handle does not keep a persistent CPU copy of the delta values.
/// WaitForAssetLoad success does not guarantee GPU resource or upload completion.
DILIGENT_BEGIN_INTERFACE(IRadientMeshMorphTargetData, IRadientAsset)
{
};

#endif


// {E054A803-CCEC-41FE-992E-1AE33CAA1880}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMeshImportServices =
    {0xe054a803, 0xccec, 0x41fe, {0x99, 0x2e, 0x1a, 0xe3, 0x3c, 0xaa, 0x18, 0x80}};


#define DILIGENT_INTERFACE_NAME IRadientMeshImportServices
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMeshImportServicesInclusiveMethods \
    IObjectInclusiveMethods;                       \
    IRadientMeshImportServicesMethods RadientMeshImportServices

// clang-format off

/// Mesh-data creation services supplied to scene asset importer implementations.
///
/// Each service is associated with an asset manager. It creates immutable data
/// handles that importers can share independently across mesh geometries and
/// assembles those geometries into mesh assets. The associated asset manager's
/// WaitForAssetLoad accepts both the data handles and the resulting mesh assets.
/// The service retains its associated asset manager. These methods can be
/// called concurrently; stopping the manager prevents new creation requests.
DILIGENT_BEGIN_INTERFACE(IRadientMeshImportServices, IObject)
{
    /// Creates reusable, immutable vertex data from a source layout and blobs.
    ///
    /// Layout arrays and strings are copied and source blobs are retained with
    /// shared read access before returning. The renderer chooses the stored
    /// layout. Data can be used by CreateMeshView while loading is pending.
    ///
    /// \param [in] VertexData - Source data; see RadientMeshVertexData.
    /// \param [out] ppVertexData - Address of a null pointer that receives a strong
    ///                            reference when an asset is created. Synchronous
    ///                            validation failures leave the pointer null. A
    ///                            scheduling failure may return a failed asset;
    ///                            release any non-null output.
    ///
    /// \return RADIENT_STATUS_OK if CPU processing is complete, or
    ///         RADIENT_STATUS_PENDING if it continues asynchronously. Neither
    ///         guarantees GPU readiness. WaitForAssetLoad reports later source
    ///         errors. Invalid descriptions return RADIENT_STATUS_INVALID_ARGUMENT;
    ///         active blob writers or a stopped manager return
    ///         RADIENT_STATUS_INVALID_OPERATION.
    VIRTUAL RADIENT_STATUS METHOD(CreateMeshVertexData)(THIS_
                                                        const RadientMeshVertexData REF VertexData,
                                                        IRadientMeshVertexData**        ppVertexData) PURE;

    /// Creates reusable, immutable index data from a tightly packed source blob.
    ///
    /// The blob is retained with shared read access before returning. Source
    /// UINT8, UINT16, and UINT32 encodings are accepted; the renderer selects the
    /// stored encoding. Data can be used by CreateMeshView while loading is pending.
    ///
    /// \param [in] IndexData - Source data; see RadientMeshIndexData.
    /// \param [out] ppIndexData - Address of a null pointer that receives a strong
    ///                           reference when an asset is created. Synchronous
    ///                           validation failures leave the pointer null. A
    ///                           scheduling failure may return a failed asset;
    ///                           release any non-null output.
    ///
    /// \return RADIENT_STATUS_OK if CPU processing is complete, or
    ///         RADIENT_STATUS_PENDING if it continues asynchronously. Neither
    ///         guarantees GPU readiness. WaitForAssetLoad reports later source
    ///         errors. Invalid descriptions return RADIENT_STATUS_INVALID_ARGUMENT;
    ///         an active blob writer or a stopped manager returns
    ///         RADIENT_STATUS_INVALID_OPERATION.
    VIRTUAL RADIENT_STATUS METHOD(CreateMeshIndexData)(THIS_
                                                       const RadientMeshIndexData REF IndexData,
                                                       IRadientMeshIndexData**        ppIndexData) PURE;

    /// Creates reusable, immutable morph-target data for a vertex domain.
    ///
    /// All descriptions, strings, and delta values are copied before returning.
    /// Data can be used by CreateMeshView while loading is pending.
    ///
    /// \param [in] MorphTargetData - Source targets; see RadientMeshMorphTargetData.
    ///                              MorphTargetCount must be nonzero.
    /// \param [in] VertexCount - Number of vertex deltas per attribute. Must be
    ///                          nonzero and match each geometry that uses the data.
    /// \param [out] ppMorphTargetData - Address of a null pointer that receives a
    ///                                 strong reference when an asset is created.
    ///                                 Synchronous validation failures leave it null.
    ///                                 A scheduling failure may return a failed
    ///                                 asset; release any non-null output.
    ///
    /// \return RADIENT_STATUS_OK if CPU processing is complete, or
    ///         RADIENT_STATUS_PENDING if it continues asynchronously. Neither
    ///         guarantees GPU readiness. WaitForAssetLoad reports later source
    ///         errors. Invalid descriptions return RADIENT_STATUS_INVALID_ARGUMENT;
    ///         a stopped manager returns RADIENT_STATUS_INVALID_OPERATION.
    VIRTUAL RADIENT_STATUS METHOD(CreateMeshMorphTargetData)(THIS_
                                                             const RadientMeshMorphTargetData REF MorphTargetData,
                                                             Uint32                               VertexCount,
                                                             IRadientMeshMorphTargetData**        ppMorphTargetData) PURE;

    /// Creates a mesh view over geometry data belonging to the associated asset manager.
    ///
    /// The primitive descriptions, names, and geometry mapping are copied, and
    /// used geometry handles and materials are retained, before returning. The
    /// geometry data may still be loading. The view shares its existing storage;
    /// creating another view does not repack or reupload its vertex or index data.
    ///
    /// \param [in] ViewCI - Primitive ranges and geometry; see RadientMeshViewCreateInfo.
    /// \param [out] ppMesh - Address of a null pointer that receives a strong
    ///                       reference when an asset is created. Synchronous
    ///                       validation failures leave the pointer null. A scheduling
    ///                       failure may return a failed asset; release any
    ///                       non-null output.
    ///
    /// \return RADIENT_STATUS_OK if CPU processing is complete, or
    ///         RADIENT_STATUS_PENDING if it continues asynchronously. Neither
    ///         guarantees GPU readiness. Invalid descriptors or geometry handles
    ///         return RADIENT_STATUS_INVALID_ARGUMENT; a stopped manager returns
    ///         RADIENT_STATUS_INVALID_OPERATION. Primitive ranges and morph-target
    ///         compatibility are checked after geometry processing; use
    ///         WaitForAssetLoad to observe deferred validation failures.
    VIRTUAL RADIENT_STATUS METHOD(CreateMeshView)(THIS_
                                                  const RadientMeshViewCreateInfo REF ViewCI,
                                                  IRadientMeshAsset**                ppMesh) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientMeshImportServices_CreateMeshVertexData(This, ...)      CALL_IFACE_METHOD(RadientMeshImportServices, CreateMeshVertexData,      This, __VA_ARGS__)
#    define IRadientMeshImportServices_CreateMeshIndexData(This, ...)       CALL_IFACE_METHOD(RadientMeshImportServices, CreateMeshIndexData,       This, __VA_ARGS__)
#    define IRadientMeshImportServices_CreateMeshMorphTargetData(This, ...) CALL_IFACE_METHOD(RadientMeshImportServices, CreateMeshMorphTargetData, This, __VA_ARGS__)
#    define IRadientMeshImportServices_CreateMeshView(This, ...)            CALL_IFACE_METHOD(RadientMeshImportServices, CreateMeshView,            This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
