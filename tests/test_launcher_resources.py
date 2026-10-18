import pathlib
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1] / "android/app/src/main/res"


class LauncherResourcesTest(unittest.TestCase):
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
            self.assertIn("https://t.me/player1444ports", message)
            self.assertIn("https://github.com/JackPriceBurns/foxhollow", message)
            self.assertEqual([item.text for item in entries["language_labels"]], ["English", "Русский"])
        self.assertNotEqual(english["community_message"].text, russian["community_message"].text)


if __name__ == "__main__":
    unittest.main()
