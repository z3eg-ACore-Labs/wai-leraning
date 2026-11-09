"""Compile the real WAI adapter against API doubles and optional QoL contract variants; no server."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qol', type=Path, help='Optional QoL checkout; include its real client contract')
options = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='wai-adapter-') as folder:
    folder = Path(folder)
    for name in ['Player.h', 'WorldSession.h', 'Playerbots.h', 'PlayerbotAI.h', 'PlayerbotAIConfig.h', 'PlayerbotMgr.h']:
        (folder / name).write_text('#include "AdapterMock.h"\n')
    variants = ['missing', 'unsupported'] + (['available'] if options.qol else [])
    for variant in variants:
        contract = folder / 'QolClientControl.h'
        if variant == 'unsupported':
            contract.write_text('#define AC_PLAYERBOTS_QOL_CLIENT_CONTROL_API 2\n')
        elif contract.exists():
            contract.unlink()
        includes = [folder, repo / 'tests/recording', repo / 'src/integration']
        if variant == 'available':
            includes.append(options.qol.resolve() / 'src/integration')
        exe = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            flags = ['/nologo', '/std:c++20', '/EHsc', *['/I' + str(p) for p in includes],
                     str(repo / 'tests/recording/test_adapter.cpp'), '/Fe:' + str(exe), '/Fo:' + str(folder / 'fixture.obj')]
        else:
            flags = ['-std=c++20', *['-I' + str(p) for p in includes], str(repo / 'tests/recording/test_adapter.cpp'), '-o', str(exe)]
        subprocess.run([compiler, *flags], cwd=folder, check=True)
        subprocess.run([str(exe)], cwd=folder, check=True)
        print('WAI adapter:', variant, 'passed')
