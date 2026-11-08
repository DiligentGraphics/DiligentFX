"""Check the JavaScript subset without changing Radient's native API model."""

from copy import deepcopy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "BuildTools/Bindings"))
from javascript.generate import Generator

MODEL = json.loads(Path(sys.argv.pop(1)).read_text(encoding="utf-8"))
ADAPTERS = json.loads(Path(__file__).with_name("API.json").read_text(encoding="utf-8"))


class RadientGenerationTest(unittest.TestCase):
    def test_native_model_is_preserved(self):
        original = deepcopy(MODEL)
        Generator(MODEL, "Radient", ["RadientDataBlob.h", "RadientEngine.h"], ADAPTERS).generate()
        self.assertEqual(MODEL, original)
        declarations = {d["name"]: d for d in MODEL["declarations"]}
        info = declarations["Diligent::RadientDataBlobCreateInfo"]
        self.assertEqual(len(info["fields"]), 5)
        self.assertEqual(info["fields"][1]["policy"]["binding"], "manual")
        modes = declarations["Diligent::RADIENT_DATA_BLOB_STORAGE_MODE"]["values"]
        self.assertEqual(len(modes), 2)

    def test_owning_factories_and_native_defaults(self):
        cpp, js, types = Generator(MODEL, "Radient", ["RadientDataBlob.h", "RadientEngine.h"], ADAPTERS).generate()
        self.assertIn("new (std::nothrow) Diligent::RadientDataBlobCreateInfo{}", cpp)
        self.assertIn("Diligent::RADIENT_DATA_BLOB_STORAGE_MODE_COPY", cpp)
        self.assertNotIn("RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE", cpp)
        self.assertIn("CreateRadientDataBlob(CreateInfo: RadientDataBlobCreateInfo): Result<IRadientDataBlob | null>", types)
        self.assertIn("Size?: bigint", types)
        self.assertIn("pData?: Uint8Array | null", types)
        for name in ("OnLastReaderReleased", "OnDestroy", "pUserData", "StorageMode"):
            self.assertNotIn(name, types)
            self.assertNotIn(name, js)

    def test_inherited_interfaces_and_scopes(self):
        _, _, types = Generator(MODEL, "Radient", ["RadientDataBlob.h", "RadientEngine.h"], ADAPTERS).generate()
        self.assertIn("IRadientMutableDataBlob extends IRadientDataBlob", types)
        self.assertIn("BeginRead(): Result<ReadByteAccess | null>", types)
        self.assertIn("BeginWrite(): Result<WriteByteAccess | null>", types)
        self.assertIn("Resize(NewSize: bigint): number", types)
        self.assertNotIn("EndRead()", types)
        self.assertNotIn("EndWrite()", types)

    def test_scene_api_uses_selected_methods_and_native_defaults(self):
        cpp, js, types = Generator(MODEL, "Radient", ["RadientDataBlob.h", "RadientEngine.h"], ADAPTERS).generate()
        self.assertIn("new (std::nothrow) Diligent::RadientEngineCreateInfo{}", cpp)
        self.assertIn("WorkerThreadCount?: number", types)
        self.assertIn("CreateScene(CreateInfo: RadientSceneCreateInfo): Result<IRadientScene | null>", types)
        self.assertIn("GetScene(): IRadientScene", types)
        self.assertIn("GetLocalTransform(Entity: bigint): Result<StructSnapshot<RadientTransform>>", types)
        self.assertIn("GetRootEntities(Capacity: number): Result<", types)
        self.assertIn("KeepWorldTransform?: boolean", types)
        for name in ("GetAssetManager", "SetMesh", "SetCustomComponentData", "IThreadPool", "Backend", "Resources"):
            self.assertNotIn(name, types)
            self.assertNotIn(name, js)


if __name__ == "__main__":
    unittest.main()
