"""Exercise release input rejection and relocated real linker input replay."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/ci' / (name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

class ReleaseTests(unittest.TestCase):
    def test_immutable_image_only(self):
        mod = module('metadata')
        image = 'ghcr.io/revive360/devkit@sha256:' + 'a' * 64
        self.assertEqual(mod.image_reference(image + '\n'), image)
        for invalid in ('ghcr.io/revive360/devkit:latest', image + '\nother', image.replace('revive360', 'other'), 'pending'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                mod.image_reference(invalid)

    def test_tags_and_branch_proof(self):
        mod = module('metadata')
        self.assertEqual(mod.release_metadata('refs/tags/v0.3.3-rc.1', '123'), {'version':'0.3.3-rc.1', 'tag':'v0.3.3-rc.1', 'draft':'true'})
        self.assertEqual(mod.release_metadata('refs/heads/f55-ci-release', '123')['draft'], 'false')
        for ref, run in [('refs/tags/vbad', '123'), ('refs/tags/v0.3.3\ninjected=true', '123'), ('refs/heads/main','abc')]:
            with self.assertRaises(ValueError):
                mod.release_metadata(ref, run)

    def test_relink_after_original_inputs_moved(self):
        mod = module('source-kit')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build, kit = root / 'original space', root / 'kit'
            build.mkdir(); kit.mkdir()
            (build / 'main.c').write_text('int main(void) { return 0; }\n')
            subprocess.run(['cc', '-c', 'main.c', '-o', 'main.o'], cwd=build, check=True)
            command = mod.collect_link('cc main.o -o app', build, kit)
            shutil.rmtree(build)
            subprocess.run(command, cwd=kit, check=True)
            self.assertEqual(subprocess.run([str(kit / 'app.elf')]).returncode, 0)

    def test_unknown_absolute_linker_input_fails(self):
        mod = module('source-kit')
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                mod.collect_link('cc /does-not-exist/input.a -o app', Path(folder), Path(folder))
