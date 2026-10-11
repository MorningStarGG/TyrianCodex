"""Pack-source cleanup and the real CMake deployment contract."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from build_release import write_pack
from validate_bundle import TEXTURE_PACKS, clean_pack_inputs

CMAKE = shutil.which("cmake") or str(Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) /
    "Microsoft Visual Studio/2019/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")


class PackCleanup(unittest.TestCase):
    def test_clean_sources_preserves_packs_and_runtime_art(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            for name, definition in TEXTURE_PACKS.items():
                source = data / "textures" / definition["sourceDir"]
                source.mkdir(parents=True)
                (source / "source.png").write_bytes(b"image")
                write_pack(data / "textures" / (name + ".pack"), [("image", b"image")])
            for name in ("ui", "markers"):
                source = data / "textures" / name
                source.mkdir()
                (source / "art.png").write_bytes(b"runtime art")
            (data / "catalog.json").write_text("{}")
            self.assertEqual(len(clean_pack_inputs(data)), len(TEXTURE_PACKS))
            self.assertEqual(clean_pack_inputs(data), [])
            self.assertEqual(len(list((data / "textures").glob("*.pack"))), len(TEXTURE_PACKS))
            self.assertTrue((data / "textures/ui/art.png").exists())
            self.assertTrue((data / "textures/markers/art.png").exists())
            self.assertEqual((data / "catalog.json").read_text(), "{}")

    def test_all_replacements_verified_before_any_removal(self):
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder)
            for name in ("cats", "skins"):
                source = data / "textures" / name
                source.mkdir(parents=True)
                (source / "image.png").write_bytes(b"source")
                write_pack(data / "textures" / (name + ".pack"), [("image", b"source")])
            (data / "textures/skins.pack").write_bytes(b"broken pack")
            with self.assertRaises(ValueError):
                clean_pack_inputs(data)
            self.assertTrue((data / "textures/cats/image.png").exists())
            self.assertTrue((data / "textures/skins/image.png").exists())
            (data / "textures/skins.pack").unlink()
            with self.assertRaises(OSError):
                clean_pack_inputs(data)
            self.assertTrue((data / "textures/cats/image.png").exists())

    def test_redirected_source_directory_is_never_deleted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            outside = root / "outside"
            outside.mkdir()
            sentinel = outside / "keep.png"
            sentinel.write_bytes(b"keep")
            data = root / "data"
            (data / "textures").mkdir(parents=True)
            try:
                (data / "textures/skins").symlink_to(outside, target_is_directory=True)
            except OSError:
                if os.name != "nt": raise
                subprocess.run(["cmd", "/c", "mklink", "/J", str(data / "textures/skins"), str(outside)],
                               capture_output=True, check=True)
            with self.assertRaises(ValueError):
                clean_pack_inputs(data)
            self.assertEqual(sentinel.read_bytes(), b"keep")


@unittest.skipUnless(Path(CMAKE).is_file(), "CMake is needed for deployment integration tests")
class DeploymentCleanup(unittest.TestCase):
    def deploy(self, root, data):
        dll = root / "test.dll"
        dll.write_bytes(b"test DLL")
        result = subprocess.run([CMAKE, f"-DSRC={dll}", f"-DDST_DLL={root / 'installed.dll'}",
                                 f"-DDATA_SRC={data}", f"-DDATA_DST={root / 'installed'}",
                                 "-P", str(ROOT / "cmake/deploy.cmake")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_inventory_excludes_and_cleans_every_pack_source(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = root / "data"
            for name, definition in TEXTURE_PACKS.items():
                for base in (data, root / "installed"):
                    source = base / "textures" / definition["sourceDir"]
                    source.mkdir(parents=True)
                    (source / "loose.png").write_bytes(b"never deploy")
                write_pack(data / "textures" / (name + ".pack"), [("image", b"packed image")])
            art = data / "textures/ui/button.png"
            art.parent.mkdir()
            art.write_bytes(b"runtime art")
            (data / "catalog.json").write_text("{}")
            self.deploy(root, data)
            for name, definition in TEXTURE_PACKS.items():
                self.assertFalse((root / "installed/textures" / definition["sourceDir"]).exists())
                self.assertTrue((data / "textures" / definition["sourceDir"] / "loose.png").exists())
                self.assertEqual((root / "installed/textures" / (name + ".pack")).read_bytes(),
                                 (data / "textures" / (name + ".pack")).read_bytes())
            self.assertEqual((root / "installed/textures/ui/button.png").read_bytes(), b"runtime art")

    def test_missing_pack_retains_existing_installed_sources(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = root / "data"
            (data / "textures/loading").mkdir(parents=True)
            (data / "textures/loading/new.png").write_bytes(b"do not deploy")
            old = root / "installed/textures/loading/old.png"
            old.parent.mkdir(parents=True)
            old.write_bytes(b"keep without replacement")
            result = self.deploy(root, data)
            self.assertEqual(old.read_bytes(), b"keep without replacement")
            self.assertFalse((old.parent / "new.png").exists())
            self.assertIn("replacement pack was not deployed", result.stderr)


if __name__ == "__main__":
    unittest.main()
