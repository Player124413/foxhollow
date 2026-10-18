import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

CHECKER = pathlib.Path(__file__).resolve().parents[1] / 'android' / 'verify-native.py'


class ElfValidation(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        self.source = self.root / 'fixture.c'
        self.source.write_text('int SDL_main(void) { return 0; }\n'
                               'int JNI_OnLoad(void) { return 0; }\n'
                               'void Java_org_libsdl_app_SDLActivity_nativeRunMain(void) {}\n')

    def tearDown(self):
        self.directory.cleanup()

    def library(self, alignment=16384):
        output = self.root / 'fixture.so'
        subprocess.run(['cc', '-shared', '-fPIC', str(self.source),
                        '-Wl,-z,max-page-size=' + str(alignment), '-o', str(output)], check=True)
        return output

    def verify(self, library, abi='x86_64'):
        return subprocess.run([sys.executable, str(CHECKER), str(library), abi,
                               shutil.which('readelf')], capture_output=True, text=True)

    def test_valid_elf(self):
        self.assertEqual(self.verify(self.library()).returncode, 0)

    def test_rejects_wrong_abi(self):
        self.assertNotEqual(self.verify(self.library(), 'arm64-v8a').returncode, 0)

    def test_rejects_small_pages(self):
        self.assertNotEqual(self.verify(self.library(4096)).returncode, 0)

    def test_rejects_missing_entry(self):
        self.source.write_text('int not_sdl_main(void) { return 0; }\n')
        self.assertNotEqual(self.verify(self.library()).returncode, 0)


if __name__ == '__main__':
    unittest.main()
