import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AndroidSigningTest(unittest.TestCase):
    def test_prepare(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            env = dict(os.environ, RUNNER_TEMP=directory, GITHUB_ENV=str(path / "env"))
            result = subprocess.run([sys.executable, str(ROOT / "android/prepare-signing.py")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((path / "foxhollow-public-debug.p12").read_bytes(),
                             (ROOT / "android/signing/public-debug.p12").read_bytes())
            self.assertEqual((path / "foxhollow-public-debug.p12").stat().st_mode & 0o777, 0o600)
            self.assertIn("FOXHOLLOW_KEYSTORE_ALIAS=androiddebugkey", (path / "env").read_text())
            self.assertIn("FOXHOLLOW_KEYSTORE_PASSWORD=android", (path / "env").read_text())

    def test_certificate_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            expected = "ab" * 32
            (path / "pin").write_text(expected)
            for actual, success in [(expected, True), ("cd" * 32, False), ("", False)]:
                (path / "report").write_text(f"Signer #1 certificate SHA-256 digest: {actual}\n")
                result = subprocess.run([sys.executable, str(ROOT / "android/verify-signing.py"),
                                         str(path / "report"), str(path / "pin")], capture_output=True)
                self.assertEqual(result.returncode == 0, success)


if __name__ == "__main__":
    unittest.main()
