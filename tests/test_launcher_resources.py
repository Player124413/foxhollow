import pathlib
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1] / "android/app/src/main/res"


class LauncherResourcesTest(unittest.TestCase):
    def test_logs_provider_scope(self):
        android = "{http://schemas.android.com/apk/res/android}"
        manifest = ET.parse(ROOT.parent / "AndroidManifest.xml").getroot()
        provider = next(node for node in manifest.find("application").findall("provider")
                        if node.get(android + "name") == "androidx.core.content.FileProvider")
        self.assertEqual(provider.get(android + "exported"), "false")
        self.assertEqual(provider.get(android + "grantUriPermissions"), "true")
        paths = ET.parse(ROOT / "xml/log_paths.xml").getroot()
        self.assertEqual(len(paths), 1)
        self.assertEqual(paths[0].tag, "cache-path")
        self.assertEqual(paths[0].get("path"), "shared-logs/")
        self.assertNotIn("android.permission.READ_LOGS", [node.get(android + "name") for node in manifest.findall("uses-permission")])

    def test_translations(self):
        en = ET.parse(ROOT / "values/strings.xml").getroot()
        ru = ET.parse(ROOT / "values-ru/strings.xml").getroot()
        english = {node.attrib["name"]: node for node in en}
        russian = {node.attrib["name"]: node for node in ru}
        self.assertEqual(english.keys(), russian.keys())
        for key in english:
            a, b = english[key], russian[key]
            self.assertEqual(a.tag, b.tag)
            self.assertEqual(len(a), len(b))
            self.assertEqual(re.findall(r"%\d+\$[ds]", "".join(a.itertext())),
                             re.findall(r"%\d+\$[ds]", "".join(b.itertext())))
        for entries in (english, russian):
            message = entries["community_message"].text
            self.assertIn("Ranger Fox", message)
            self.assertIn("https://t.me/player1444ports", message)
            self.assertIn("https://github.com/JackPriceBurns/foxhollow", message)
            self.assertEqual([item.text for item in entries["language_labels"]], ["English", "Русский"])
        self.assertNotEqual(english["community_message"].text, russian["community_message"].text)


if __name__ == "__main__":
    unittest.main()
