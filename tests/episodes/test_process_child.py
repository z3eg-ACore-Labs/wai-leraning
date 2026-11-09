"""Owned offline subprocess fixture. No game server, network, or database."""
from pathlib import Path
import sys
import time

if sys.argv[1] == 'hold':
    Path(sys.argv[2]).write_text('ready', encoding='ascii')
    time.sleep(30)
elif sys.argv[1] == 'flood':
    sys.stderr.write('e' * 262144)
    sys.stderr.flush()
    sys.stdout.write('o' * 262144)
    sys.stdout.flush()
else:
    sys.exit(7)
