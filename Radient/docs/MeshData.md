# Mesh import services

`IRadientMeshImportServices` provides importer implementations with independent
vertex, index, and morph-target data creation. Its `CreateMeshView` method
combines these handles into a mesh asset. This supports imported meshes that
share vertices but use different indices or materials, and meshes containing
multiple geometries.

Include the C-compatible `RadientMeshImportServices.h` explicitly to use this
interface. It is not included by `Radient.h`. Applications can create meshes
through the regular `IRadientAssetManager::CreateMesh` method.

The examples below assume the importer has received `pMeshImportServices`.
There is currently no public acquisition API for this service.

## Create geometry data

Describe source vertices with the existing vertex-layout API and data blobs.
The renderer chooses the stored layout. Index data accepts tightly packed
`UINT8`, `UINT16`, or `UINT32` elements.

```cpp
RadientVertexLayoutDescX Layout;
Layout.AddBuffer()
      .AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);

IRadientDataBlob* VertexBuffers[] = {pPositionBlob};
RadientMeshVertexDataCreateInfo VertexCI;
VertexCI.VertexLayout    = Layout;
VertexCI.ppVertexBuffers = VertexBuffers;
VertexCI.VertexCount     = VertexCount;

RefCntAutoPtr<IRadientMeshVertexData> pVertexData;
RADIENT_STATUS Status = pMeshImportServices->CreateMeshVertexData(VertexCI, &pVertexData);
if (RADIENT_FAILED(Status))
    return Status;

RadientMeshIndexDataCreateInfo IndexCI;
IndexCI.pIndexBuffer = pIndexBlob;
IndexCI.IndexCount   = IndexCount;
IndexCI.IndexType    = RADIENT_INDEX_TYPE_UINT16;

RefCntAutoPtr<IRadientMeshIndexData> pIndexData;
Status = pMeshImportServices->CreateMeshIndexData(IndexCI, &pIndexData);
if (RADIENT_FAILED(Status))
    return Status;
```

Both calls copy descriptor metadata and acquire shared read access to their
source blobs before returning. End any blob write scope before calling them.
The caller can release its descriptors and blob references after the calls;
Radient retains the blobs until source processing finishes. Reference-mode
blobs still require their external storage to remain valid for the blob's
lifetime.

Optional `CreateMeshMorphTargetData` takes a `RadientMeshMorphTargetDataCreateInfo`
containing target descriptions and a vertex count. It copies names, attribute
descriptions, and delta arrays during the call. Attach the resulting handle to
any geometry with the same vertex count.

## Assemble a mesh view

```cpp
RadientMeshGeometryData Geometry;
Geometry.pVertexData = pVertexData;
Geometry.pIndexData  = pIndexData;

RadientMeshPrimitiveCreateInfo Primitive;
Primitive.Name       = "Surface";
Primitive.FirstIndex = 0;
Primitive.IndexCount = IndexCount;
Primitive.pMaterial  = pMaterial;

RadientMeshViewCreateInfo ViewCI;
ViewCI.pGeometryData  = &Geometry;
ViewCI.GeometryCount  = 1;
ViewCI.pPrimitives    = &Primitive;
ViewCI.PrimitiveCount = 1;

RefCntAutoPtr<IRadientMeshAsset> pMesh;
Status = pMeshImportServices->CreateMeshView(ViewCI, &pMesh);
if (RADIENT_FAILED(Status))
    return Status;
```

The view may be submitted immediately after creating its data handles. It waits
for their source processing and uses their existing storage. All geometry data
must belong to the same asset manager as the mesh-import service. Views
currently require both vertex and index data.

For multiple geometries, provide a `pGeometryData` array and use
`pGeometryIndices` to map each primitive to an array entry. A null mapping
selects geometry zero for every primitive. Geometry handles can be reused
independently: two entries may share vertices and use different index data,
or vice versa. Each primitive selects its own index range and material.
Unused geometry entries are ignored; the resulting mesh description lists
used geometries in first-use order.

The call copies primitive metadata, names, and geometry mappings and retains
the referenced materials and geometry data. Input arrays and caller-owned
references can be released when it returns. Primitive ranges and compatibility
of morph targets across geometries are checked after data processing finishes.

## Completion and ownership

Creation normally returns `RADIENT_STATUS_PENDING`. Call
`IRadientAssetManager::WaitForAssetLoad` on the associated asset manager to wait
for a data handle or the assembled mesh. Waiting for the mesh also waits
for its data dependencies and reports their failures. Successful source loading
does not mean GPU uploads have completed; rendering uses the existing asset
upload pipeline.

Data handles are immutable. Keep a handle to reuse it in later views, or release
it after creating the views that need it. Views retain their geometry data.
Identical processed data uses the existing deduplication behavior, and unused
data is released when no handles or views retain it.
