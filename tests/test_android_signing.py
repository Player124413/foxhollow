import json
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
            env = dict(os.environ, RUNNER_TEMP=directory, GITHUB_ENV=str(path / "env"),
                       FOXHOLLOW_ANDROID_SIGNING_BUNDLE=json.dumps({
                           "keystore": "c2lnbmluZy10ZXN0", "password": "test_password_not_a_secret_123", "alias": "foxhollow"}))
            result = subprocess.run([sys.executable, str(ROOT / "android/prepare-signing.py")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((path / "foxhollow-signing.p12").read_bytes(), b"signing-test")
            self.assertEqual((path / "foxhollow-signing.p12").stat().st_mode & 0o777, 0o600)
            self.assertIn("FOXHOLLOW_KEYSTORE_ALIAS=foxhollow", (path / "env").read_text())
            env["FOXHOLLOW_ANDROID_SIGNING_BUNDLE"] = ""
            result = subprocess.run([sys.executable, str(ROOT / "android/prepare-signing.py")],
                                    env=env, capture_output=True)
            self.assertNotEqual(result.returncode, 0)

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
