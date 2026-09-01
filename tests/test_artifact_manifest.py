import argparse
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "scripts" / "artifact_manifest.py"
SPEC = importlib.util.spec_from_file_location("artifact_manifest", MODULE_PATH)
artifact_manifest = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(artifact_manifest)


class ArtifactManifestTest(unittest.TestCase):
    def test_create_verify_and_tamper_detection(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "model.bin"
            engine = root / "engine.plan"
            output = root / "manifest.json"
            model.write_bytes(b"model-weights")
            engine.write_bytes(b"serialized-engine")
            args = argparse.Namespace(
                model=model,
                engine=engine,
                model_id="example/model",
                model_revision="abc123immutable",
                precision="fp16",
                maximum_batch_size=8,
                maximum_sequence_length=2048,
            )
            manifest = artifact_manifest.create_manifest(args)
            output.write_text(json.dumps(manifest), encoding="utf-8")
            artifact_manifest.verify_manifest(output)
            engine.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                artifact_manifest.verify_manifest(output)

    def test_mutable_revision_is_rejected(self) -> None:
        args = argparse.Namespace(model_revision="main")
        with self.assertRaisesRegex(ValueError, "immutable revision"):
            artifact_manifest.create_manifest(args)


if __name__ == "__main__":
    unittest.main()
