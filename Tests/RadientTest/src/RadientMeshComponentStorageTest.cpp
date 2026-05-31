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

#include "RadientMeshComponentStorage.hpp"

#include <cstring>
#include <memory>
#include <utility>

using namespace Diligent;

namespace
{

TEST(RadientMeshComponentStorageTest, AssignCopiesURI)
{
    // Verifies that mesh component storage owns a URI copy instead of keeping
    // the caller-provided raw string pointer.
    const char OriginalURI[] = "mesh://heap";
    const char ChangedURI[]  = "mesh://changed";

    std::unique_ptr<char[]> MeshURI = std::make_unique<char[]>(64);
    std::memcpy(MeshURI.get(), OriginalURI, sizeof(OriginalURI));

    RadientMeshComponent Mesh;
    Mesh.Mesh.URI     = MeshURI.get();
    Mesh.Mesh.Version = 7;

    MeshComponentStorage Storage;
    Storage.Assign(Mesh);

    std::memcpy(MeshURI.get(), ChangedURI, sizeof(ChangedURI));
    MeshURI.reset();

    // The stored component should still point to the internal string copy and
    // preserve the original asset version after the source buffer is gone.
    EXPECT_STREQ(Storage.Component.Mesh.URI, OriginalURI);
    EXPECT_EQ(Storage.Component.Mesh.URI, Storage.MeshURI.c_str());
    EXPECT_EQ(Storage.Component.Mesh.Version, 7u);
}

TEST(RadientMeshComponentStorageTest, MoveConstructorRepairsURI)
{
    // Moving storage must repair the component URI pointer so it points into
    // the moved-to storage object.
    RadientMeshComponent Mesh;
    Mesh.Mesh.URI     = "mesh://move";
    Mesh.Mesh.Version = 11;

    MeshComponentStorage Source;
    Source.Assign(Mesh);

    MeshComponentStorage Moved{std::move(Source)};

    // The moved component should retain the asset identity and point at the
    // destination-owned string.
    EXPECT_STREQ(Moved.Component.Mesh.URI, "mesh://move");
    EXPECT_EQ(Moved.Component.Mesh.URI, Moved.MeshURI.c_str());
    EXPECT_EQ(Moved.Component.Mesh.Version, 11u);
}

TEST(RadientMeshComponentStorageTest, MoveAssignmentRepairsURI)
{
    // Move assignment must replace the previous component data and reconnect
    // raw URI pointers to the destination storage.
    RadientMeshComponent Mesh;
    Mesh.Mesh.URI     = "mesh://assigned";
    Mesh.Mesh.Version = 13;

    MeshComponentStorage Source;
    Source.Assign(Mesh);

    RadientMeshComponent OtherMesh;
    OtherMesh.Mesh.URI     = "mesh://other";
    OtherMesh.Mesh.Version = 1;

    MeshComponentStorage Destination;
    Destination.Assign(OtherMesh);
    Destination = std::move(Source);

    // The old destination URI should be discarded; the assigned component
    // should expose the moved asset and destination-owned URI storage.
    EXPECT_STREQ(Destination.Component.Mesh.URI, "mesh://assigned");
    EXPECT_EQ(Destination.Component.Mesh.URI, Destination.MeshURI.c_str());
    EXPECT_EQ(Destination.Component.Mesh.Version, 13u);
}

TEST(RadientMeshComponentStorageTest, KeepsNullURI)
{
    // Null asset URIs are valid for an empty mesh reference and should stay
    // null through assign and move operations.
    RadientMeshComponent Mesh;
    Mesh.Mesh.URI     = nullptr;
    Mesh.Mesh.Version = 5;

    MeshComponentStorage Storage;
    Storage.Assign(Mesh);

    EXPECT_EQ(Storage.Component.Mesh.URI, nullptr);
    EXPECT_TRUE(Storage.MeshURI.empty());

    MeshComponentStorage Moved;
    Moved = std::move(Storage);

    // Moving an empty URI should not synthesize a string or leave a dangling
    // pointer in the component.
    EXPECT_EQ(Moved.Component.Mesh.URI, nullptr);
    EXPECT_TRUE(Moved.MeshURI.empty());
}

} // namespace
