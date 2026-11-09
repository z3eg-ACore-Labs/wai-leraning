"""Shared offline fixture compiler; never configures or builds worldserver."""
from pathlib import Path
import subprocess


def compile_and_run(compiler, root, fixture):
    exe = fixture.with_suffix('.exe')
    if Path(compiler).name.lower() in ('cl', 'cl.exe'):
        flags = ['/nologo', '/EHsc', '/std:c++20', '/MD', '/UNDEBUG', '/I' + str(root / 'src/action'),
                 str(fixture), '/Fe:' + str(exe), '/Fo:' + str(fixture.with_suffix('.obj'))]
    else:
        flags = ['-std=c++20', '-pthread', '-UNDEBUG', '-I' + str(root / 'src/action'), str(fixture), '-o', str(exe)]
    subprocess.run([compiler, *flags], cwd=fixture.parent, check=True)
    subprocess.run([str(exe)], cwd=fixture.parent, check=True)
