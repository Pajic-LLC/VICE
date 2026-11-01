#!/usr/bin/env python3
"""Execute a customer-style module and physical carrier without a desktop."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def validate(vice):
    def run(*args, expected=0):
        p = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=240)
        assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
        return json.loads(p.stdout)
    with tempfile.TemporaryDirectory(prefix='vice-playback-') as d:
        root = Path(d)
        project = root / 'project'
        shutil.copytree(Path(__file__).resolve().parent.parent / 'examples/interaction-playground', project,
                        ignore=shutil.ignore_patterns('.vice'))
        source = project / 'assets/worlds/interaction.vworld'
        original = source.read_bytes()
        exe = run('project', 'build', project / 'interaction.viceproj')['data']['game_executable']
        first, second, same = (root / name for name in ('first.vworldc', 'second.vworldc', 'same.vworldc'))
        a = run('runtime', 'simulate', exe, 360, first)['data']
        assert a['physics'] and not a['rendered'] and not a['audio_output']
        first_world = run('world', 'inspect', first)['data']
        assert int(a['world_ticks']) > 0
        run('runtime', 'simulate', exe, 360, same)
        same_world = run('world', 'inspect', same)['data']
        assert first_world['entities'] == same_world['entities'], 'Entity playback was not deterministic'
        assert first_world['world_clock'] == same_world['world_clock'], 'Clock playback was not deterministic'
        b = run('runtime', 'simulate', exe, 120, second, '--restore', first)['data']
        assert int(b['world_ticks']) > int(a['world_ticks'])
        second_world = run('world', 'inspect', second)['data']
        car_id = 'a2000000-0000-4000-8000-000000000002'
        car_a = next(e for e in first_world['entities'] if e['id'] == car_id)
        car_b = next(e for e in second_world['entities'] if e['id'] == car_id)
        assert car_a != car_b, 'Real vehicle/module/physics state did not advance'
        before = first.read_bytes()
        run('runtime', 'simulate', exe, 1, first, expected=3)
        assert first.read_bytes() == before
        for count in ('0', '-1', '72001', '1.5', '1x'):
            run('runtime', 'simulate', exe, count, root / 'absent.vworldc', expected=2)
        invalid = root / 'bad.vworldc'
        invalid.write_bytes(b'not a checkpoint')
        run('runtime', 'simulate', exe, 1, root / 'absent.vworldc', '--restore', invalid, expected=3)
        assert not (root / 'absent.vworldc').exists()
        assert source.read_bytes() == original
        return {'ok': True, 'checks': ['actual_module_and_physics', 'deterministic_checkpoint',
                'restore_continues_time_and_vehicle', 'cooked_inspection', 'no_overwrite',
                'invalid_steps_and_restore_rejected', 'authoring_source_unchanged'],
                'checkpoint_sha256': hashlib.sha256(before).hexdigest()}

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--vice', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.vice.resolve()), indent=2))
