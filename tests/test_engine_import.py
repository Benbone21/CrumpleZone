"""Importer checks use temporary synthetic fixtures, never shipped vehicle audio."""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

spec = importlib.util.spec_from_file_location("engine_import", Path(__file__).parents[1] / "tools/import_engine_audio.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class EngineImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        with wave.open(str(self.folder / "fixture.wav"), "wb") as output:
            output.setnchannels(2); output.setsampwidth(2); output.setframerate(22050)
            output.writeframes(b"".join(struct.pack("<hh", int(math.sin(i * 0.03) * 6000),
                                                   int(math.sin(i * 0.04) * 4000)) for i in range(22050)))
        self.plan = {"engine": "honda_d16a6", "source": "temporary synthetic test fixture",
                     "author": "test only", "license": "CC0-1.0", "clips": [
                         {"file": "fixture.wav", "start": 0.1, "duration": 0.25, "rpm": rpm, "load": load}
                         for rpm in (850, 3000) for load in (0, 1)]}
        self.plan_path = self.folder / "plan.json"

    def run_import(self):
        self.plan_path.write_text(json.dumps(self.plan), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            return module.import_bank(self.plan_path, self.folder / "banks")

    def test_valid_clips_preserve_recording_and_provenance(self):
        original_hash = hashlib.sha256((self.folder / "fixture.wav").read_bytes()).digest()
        result = self.run_import()
        manifest = (result / "bank.txt").read_text()
        self.assertIn("license CC0-1.0", manifest)
        self.assertIn("author test only", manifest)
        self.assertEqual(len(list(result.glob("*.wav"))), 4)
        for filename in result.glob("*.wav"):
            with wave.open(str(filename), "rb") as clip:
                self.assertEqual(clip.getnchannels(), 2)
                self.assertEqual(clip.getnframes(), round(0.25 * 22050))
                self.assertEqual(clip.getframerate(), 22050)
        self.assertEqual(original_hash, hashlib.sha256((self.folder / "fixture.wav").read_bytes()).digest())

    def test_existing_bank_is_preserved(self):
        result = self.run_import()
        before = (result / "bank.txt").read_bytes()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.run_import()
        self.assertEqual(before, (result / "bank.txt").read_bytes())

    def test_invalid_coverage_creates_no_bank(self):
        self.plan["clips"][1]["load"] = 0
        with self.assertRaisesRegex(ValueError, "duplicate RPM"):
            self.run_import()
        self.assertFalse((self.folder / "banks").exists())

    def test_out_of_range_crop_creates_no_bank(self):
        self.plan["clips"][0]["start"] = 0.95
        with self.assertRaisesRegex(ValueError, "beyond"):
            self.run_import()
        self.assertFalse((self.folder / "banks").exists())

    def test_license_and_metadata_are_required(self):
        self.plan["license"] = "unknown"
        with self.assertRaisesRegex(ValueError, "CC0"):
            self.run_import()
        self.plan["license"] = "CC0-1.0"
        self.plan["author"] = ""
        with self.assertRaisesRegex(ValueError, "author"):
            self.run_import()

if __name__ == "__main__":
    unittest.main()
