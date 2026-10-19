import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("apk_version", pathlib.Path(__file__).resolve().parents[1] / "android/verify-apk-version.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ApkVersionTest(unittest.TestCase):
    def test_match(self):
        module.verify("package: name='dev.foxhollow' versionCode='1000000034' versionName='dev-20fd3549'", "dev-20fd3549", 1000000034)

    def test_mismatch(self):
        for package in ("", "package: name='dev.foxhollow' versionCode='1000000032' versionName='dev-f142408b'",
                        "package: name='other' versionCode='1000000034' versionName='dev-20fd3549'",
                        "package: name='dev.foxhollow' versionCode='1000000032' versionName='dev-20fd3549'"):
            with self.assertRaises(ValueError):
                module.verify(package, "dev-20fd3549", 1000000034)


if __name__ == "__main__":
    unittest.main()
