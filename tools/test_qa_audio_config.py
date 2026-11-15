"""Exercise temporary test muting and configuration restoration."""
import json
from pathlib import Path
import tempfile
import unittest
from qa_audio_config import muted_test_audio


class AudioConfigTest(unittest.TestCase):
    def test_all_buses_and_exception_restore_exact_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'
            before = b'\xef\xbb\xbf{ "audio": { "enabled": true, "master": 0.7, "extra": "keep" }, "other": 4 }\r\n'
            path.write_bytes(before)
            with self.assertRaisesRegex(RuntimeError, 'fixture failure'):
                with muted_test_audio(path):
                    audio = json.loads(path.read_text())['audio']
                    self.assertIs(audio['enabled'], False)
                    self.assertTrue(all(audio[k] == 0 for k in ('master','music','ambient','sfx','ui')))
                    self.assertEqual(audio['extra'], 'keep')
                    raise RuntimeError('fixture failure')
            self.assertEqual(path.read_bytes(), before)

    def test_other_game_settings_survive_audio_restoration(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'
            original = dict(audio=dict(enabled=True, music=.2), world='old')
            path.write_text(json.dumps(original))
            with muted_test_audio(path):
                current = json.loads(path.read_text()); current['world'] = 'saved'
                path.write_text(json.dumps(current))
            result = json.loads(path.read_text())
            self.assertEqual(result['audio'], original['audio'])
            self.assertEqual(result['world'], 'saved')

    def test_absent_audio_section_is_removed_after_run(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'; path.write_text('{"world": "QA"}')
            before = path.read_bytes()
            with muted_test_audio(path):
                self.assertFalse(json.loads(path.read_text())['audio']['enabled'])
            self.assertEqual(path.read_bytes(), before)

    def test_missing_config_is_not_left_behind(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'
            with muted_test_audio(path): self.assertTrue(path.exists())
            self.assertFalse(path.exists())

    def test_corrupt_game_save_restores_original_then_reports_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'; before = b'{"audio":{"enabled":true},"world":"QA"}'
            path.write_bytes(before)
            with self.assertRaisesRegex(RuntimeError, 'restored original'):
                with muted_test_audio(path): path.write_bytes(b'partial game write')
            self.assertEqual(path.read_bytes(), before)

    def test_deleted_game_config_restores_original(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'; before = b'{"audio":{"enabled":true},"world":"QA"}'
            path.write_bytes(before)
            with muted_test_audio(path): path.unlink()
            self.assertEqual(path.read_bytes(), before)

    def test_invalid_config_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'config.json'; path.write_bytes(b'invalid')
            with self.assertRaises(ValueError):
                with muted_test_audio(path): self.fail('Must reject before launch')
            self.assertEqual(path.read_bytes(), b'invalid')


if __name__ == '__main__': unittest.main()
