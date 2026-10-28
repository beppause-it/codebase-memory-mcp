"""Real binaries, private runtime only. No forced process termination.

Usage: python tests/test_daemon_cross_version_stop.py NEW_BINARY OLD_BINARY
Run on Windows or Linux with an already distributed old binary.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

new, old = [str(Path(p).resolve()) for p in sys.argv[1:]]
test_parent = Path(os.environ.get('LOCALAPPDATA', tempfile.gettempdir())) / 'CBM-CR38-tests'
test_parent.mkdir(mode=0o700, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='cbm-cr38-', dir=test_parent))
for name in ('home', 'cache', 'runtime'):
    (root / name).mkdir(mode=0o700)
env = dict(os.environ, HOME=str(root / 'home'), USERPROFILE=str(root / 'home'),
           CBM_CACHE_DIR=str(root / 'cache'), CBM_RUNTIME_DIR=str(root / 'runtime'))


def call(binary, *args, codes=(0,)):
    result = subprocess.run([binary, 'daemon', *args, '--json'], env=env,
                            capture_output=True, text=True, timeout=60)
    assert result.returncode in codes, (args, result.returncode, result.stdout, result.stderr)
    return json.loads(result.stdout)


def wait_absent():
    for _ in range(100):
        state = call(new, 'status', codes=(0, 3, 4, 5))
        if state['status'] == 'not_running':
            return
        time.sleep(.1)
    raise AssertionError(('daemon did not retire', state))


client = None
try:
    assert call(new, 'stop')['status'] == 'not_running'
    started = call(old, 'start')
    status = call(new, 'status', codes=(5,))
    assert status['status'] == 'build_conflict' and status['pid'] == started['pid']
    assert call(new, 'start', codes=(5,))['status'] == 'build_conflict'
    stop = call(new, 'stop')
    assert stop['accepted'] and stop['status'] == 'stopping'
    assert stop['pid'] == status['pid'] and stop['active_build'] == status['build_fingerprint']
    assert stop['active_build'] != stop['requested_build']
    wait_absent()
    assert call(new, 'stop')['status'] == 'not_running'
    call(old, 'start')
    client = subprocess.Popen([old], env=env, stdin=subprocess.PIPE,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    client.stdin.write(b'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"cr38","version":"1"}}}\n')
    client.stdin.flush()
    for _ in range(100):
        status = call(new, 'status', codes=(5,))
        if status['committed_clients']:
            break
        time.sleep(.1)
    assert status['committed_clients'] > 0
    busy = call(new, 'stop', codes=(6,))
    assert busy['status'] == 'busy' and not busy['accepted']
    assert client.pid in busy['client_pids'] and client.poll() is None
    client.stdin.close()
    client.wait(timeout=30)
    client = None
    for _ in range(100):
        status = call(new, 'status', codes=(5,))
        if not status['committed_clients']:
            break
        time.sleep(.1)
    assert call(new, 'stop')['accepted']
    wait_absent()
    call(new, 'start')
    assert call(new, 'status')['status'] == 'active'
    assert call(new, 'stop')['accepted']
    wait_absent()
    print(json.dumps({'result': 'PASS', 'new': new, 'old': old,
                      'cross_build': True, 'busy': True, 'restart_new': True,
                      'private_root': str(root)}))
finally:
    if client is not None:
        if client.stdin and not client.stdin.closed:
            client.stdin.close()
        client.wait(timeout=30)
    # Only this fixture's endpoint, cooperative native stop. No PID kill fallback.
    for binary in (new, old):
        result = subprocess.run([binary, 'daemon', 'stop', '--json'], env=env,
                                capture_output=True, timeout=60)
        if result.returncode == 0:
            break
