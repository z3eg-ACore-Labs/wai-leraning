"""Self-contained offline WAI policies/production-hook fixtures; no server, game, network or runtime model."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qol', type=Path, help='Optional QoL checkout for supported client-contract fixture')
options = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='wai-policies-') as directory:
    directory = Path(directory)
    sources = [repo / 'tests/action/policies.cpp']
    sources += [file for file in sorted((repo / 'tests/recording').glob('*.cpp')) if file.name != 'test_adapter.cpp']
    sources += [file for file in sorted((repo / 'tests/episodes').glob('*.cpp'))
                if file.name not in ['test_spline_registry.cpp', 'test_process_lifecycle.cpp']]
    includes = sorted({file.parent for file in (repo / 'src').rglob('*.h')})
    for source in sources:
        exe = directory / ('fixture.exe' if os.name == 'nt' else 'fixture')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            flags = ['/nologo', '/std:c++20', '/EHsc', '/UNDEBUG', *['/I' + str(p) for p in includes],
                     str(source), '/Fe:' + str(exe), '/Fo:' + str(directory / 'fixture.obj')]
        else:
            flags = ['-std=c++20', '-UNDEBUG', *['-I' + str(p) for p in includes], str(source), '-o', str(exe)]
        print('Policy:', source.relative_to(repo), flush=True)
        subprocess.run([compiler, *flags], cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)
    for script in sorted((repo / 'tests/action').glob('test_*.py')):
        subprocess.run([sys.executable, str(script), '--compiler', compiler,
                        '--output', str(directory / script.stem)], cwd=directory, check=True)
    adapter = [sys.executable, str(repo / 'tests/recording/check-adapter.py')]
    if options.qol:
        adapter += ['--qol', str(options.qol.resolve())]
    subprocess.run(adapter, cwd=directory, check=True)
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', str(repo / 'tests/recording'),
                    '-p', 'test_*.py'], cwd=directory, check=True)
print('WAI self-contained policy, adapter, validator and actual production-hook fixtures passed.')
