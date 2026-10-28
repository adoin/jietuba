import hashlib
import importlib.util
import json
import shutil
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest
import zipfile

from generate_winget import generate, PACKAGES


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.output = self.root / "output"
        for _, stem, _, _, full in PACKAGES:
            for arch in ("x64", "arm64"):
                path = self.assets / f"{stem}-release-2.1.8-{arch}.zip"
                with zipfile.ZipFile(path, "w") as bundle:
                    bundle.writestr(f"{stem}.exe", f"fixture-{arch}")
                    if full:
                        bundle.writestr("models/model.onnx", "fixture")

    def test_both_editions_and_architectures_use_final_archive_hash(self):
        paths = generate(self.assets, self.output, "2.1.8")
        self.assertEqual(len(paths), 6)
        for package, stem, _, alias, _ in PACKAGES:
            folder = self.output / "manifests" / "1" / Path(*package.split(".")) / "2.1.8"
            content = (folder / f"{package}.installer.yaml").read_text(encoding="utf-8")
            self.assertIn(f"PortableCommandAlias: {alias}", content)
            for arch in ("x64", "arm64"):
                archive = self.assets / f"{stem}-release-2.1.8-{arch}.zip"
                self.assertIn(f"Architecture: {arch}", content)
                self.assertIn(hashlib.sha256(archive.read_bytes()).hexdigest().upper(), content)
                self.assertIn(f"/releases/download/release-2.1.8/{archive.name}", content)

    def test_missing_architecture_writes_nothing(self):
        (self.assets / f"{PACKAGES[1][1]}-release-2.1.8-arm64.zip").unlink()
        with self.assertRaises(FileNotFoundError):
            generate(self.assets, self.output, "2.1.8")
        self.assertFalse(self.output.exists())

    def test_cli_archive_has_submission_directory_layout(self):
        archive = self.root / "winget-manifests.zip"
        result = subprocess.run([
            sys.executable, str(Path(__file__).with_name("generate_winget.py")),
            "--assets", str(self.assets), "--output", str(self.output),
            "--version", "2.1.8", "--archive", str(archive),
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(archive) as bundle:
            yaml_files = [p for p in bundle.namelist() if p.endswith(".yaml")]
            self.assertEqual(len(yaml_files), 6)
            package = PACKAGES[0][0]
            self.assertIn(f"manifests/1/{package.replace('.', '/')}/2.1.8/{package}.installer.yaml", yaml_files)

    def test_wrong_executable_layout_is_rejected(self):
        with zipfile.ZipFile(self.assets / f"{PACKAGES[0][1]}-release-2.1.8-x64.zip", "w") as bundle:
            bundle.writestr(f"subfolder/{PACKAGES[0][1]}.exe", "fixture")
        with self.assertRaisesRegex(ValueError, "root-level"):
            generate(self.assets, self.output, "2.1.8")

    def test_missing_models_is_rejected(self):
        with zipfile.ZipFile(self.assets / f"{PACKAGES[0][1]}-release-2.1.8-x64.zip", "w") as bundle:
            bundle.writestr(f"{PACKAGES[0][1]}.exe", "fixture")
        with self.assertRaisesRegex(ValueError, "missing model"):
            generate(self.assets, self.output, "2.1.8")

    def test_invalid_version_is_rejected(self):
        with self.assertRaises(ValueError):
            generate(self.assets, self.output, "../2.1.8")

    def test_changing_brand_config_changes_packages_and_repository(self):
        project = self.root / "alternate"
        (project / "scripts").mkdir(parents=True)
        (project / "branding.json").write_text(json.dumps({"name": "example", "display_name": "Example", "repository": "owner/project"}), encoding="utf-8")
        source = project / "scripts" / "generate_winget.py"
        shutil.copyfile(Path(__file__).with_name("generate_winget.py"), source)
        spec = importlib.util.spec_from_file_location("alternate_brand_generator", source)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.REPOSITORY, "https://github.com/owner/project")
        self.assertEqual(module.PACKAGES[0][:4], ("owner.example", "example_pp", "Example", "example"))
        self.assertEqual(module.PACKAGES[1][1], "example_lite")


if __name__ == "__main__":
    unittest.main()
