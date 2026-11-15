"""Temporarily disable every game audio bus for automated desktop launches."""
from contextlib import contextmanager
import copy
import json
from pathlib import Path


@contextmanager
def muted_test_audio(path: Path):
    original_bytes = path.read_bytes() if path.exists() else None
    original = json.loads(original_bytes.decode('utf-8-sig')) if original_bytes is not None else {}
    if not isinstance(original, dict):
        raise ValueError('Test audio requires an object game configuration')
    muted = copy.deepcopy(original)
    audio = muted.setdefault('audio', {})
    if not isinstance(audio, dict):
        raise ValueError('Test audio requires an object audio configuration')
    audio.update(enabled=False, master=0., music=0., ambient=0., sfx=0., ui=0.)
    path.write_text(json.dumps(muted, indent=4) + '\n', encoding='utf-8')
    try:
        yield
    finally:
        # Restore only audio: direct flight runs may legitimately save settings
        # in other sections; their outer QA runner owns any full-file snapshot.
        try:
            current = json.loads(path.read_text(encoding='utf-8-sig')) if path.exists() else copy.deepcopy(original)
            if not isinstance(current, dict):
                raise ValueError('Game configuration is not an object')
        except (OSError, ValueError) as error:
            if original_bytes is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(original_bytes)
            raise RuntimeError('Invalid configuration after test; restored original configuration') from error
        if 'audio' in original:
            current['audio'] = original['audio']
        else:
            current.pop('audio', None)
        if current == original:
            if original_bytes is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(original_bytes)
        else:
            path.write_text(json.dumps(current, indent=4) + '\n', encoding='utf-8')
