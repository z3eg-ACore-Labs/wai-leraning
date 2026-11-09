"""Compile only the patched native process utility and run owned dummy children; never worldserver."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core', type=Path, required=True, help='Core with process-cancellation.patch applied')
parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
parser.add_argument('--include', type=Path, action='append', default=[], help='Additional compiler include (e.g. Boost)')
parser.add_argument('--boost-library', type=Path, help='Windows Boost filesystem release library')
options = parser.parse_args()
core = options.core.resolve()
tests = Path(__file__).resolve().parent
header = core / 'src/common/Utilities/StartProcess.h'
if '#define AC_ASYNC_PROCESS_CANCELLATION_API 1' not in header.read_text():
    raise SystemExit('Apply/check process-cancellation.patch on a disposable matching core first.')
with tempfile.TemporaryDirectory(prefix='wai-process-') as folder:
    folder = Path(folder)
    includes = [tests / 'process-stubs', core / 'src/common', core / 'src/common/Utilities', *options.include]
    sources = [tests / 'test_process_lifecycle.cpp', core / 'src/common/Utilities/StartProcess.cpp']
    exe = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
    if Path(options.compiler).name.lower() in ('cl', 'cl.exe'):
        if not options.boost_library:
            raise SystemExit('Supply --boost-library with the matching Windows Boost filesystem release library.')
        flags = ['/nologo', '/std:c++20', '/EHsc', '/MD', '/DBOOST_ALL_NO_LIB',
                 *['/I' + str(path) for path in includes], *map(str, sources), '/Fe:' + str(exe),
                 '/Fo:' + str(folder) + os.sep, '/link', '/LIBPATH:' + str(options.boost_library.parent),
                 str(options.boost_library), 'shell32.lib']
    else:
        flags = ['-std=c++20', '-pthread', *['-I' + str(path) for path in includes], *map(str, sources),
                 '-lboost_filesystem', '-lboost_system', '-o', str(exe)]
    subprocess.run([options.compiler, *flags], cwd=folder, check=True)
    subprocess.run([str(exe), sys.executable, str(tests / 'test_process_child.py'), str(folder / 'owned-ready.txt')],
                   cwd=folder, timeout=75, check=True)
