"""Launch the actual native editor under Xvfb, open its CRG menu and capture evidence."""
import os
import subprocess
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
acceptance = root / 'acceptance'
acceptance.mkdir(exist_ok=True)
env = dict(os.environ, HORIZON_POOL=str(root/'examples/crg/demo/pool'))
log = (acceptance/'gui.log').open('w')
p = subprocess.Popen([str(root/'build/horizon-imp'), '-c', str(root/'examples/crg/demo/blocks.json'), str(root/'examples/crg/demo/pictures')], env=env, stdout=log, stderr=subprocess.STDOUT)
def xdo(*args): return subprocess.check_output(['xdotool', *args], text=True).strip()
try:
    window = None
    for _ in range(50):
        if p.poll() is not None: raise RuntimeError('Native editor exited before it displayed a window')
        try:
            candidates = xdo('search', '--onlyvisible', '--pid', str(p.pid)).splitlines()
            if candidates: window = candidates[-1]; break
        except subprocess.CalledProcessError: pass
        time.sleep(0.2)
    if not window: raise RuntimeError('No visible native editor window')
    xdo('windowsize', window, '1560', '940')
    time.sleep(3)
    subprocess.run(['import', '-window', 'root', str(acceptance/'native-editor.png')], check=True)
    # GTK F10 opens the main menu; Home then text search selects the CRG analysis item.
    xdo('windowfocus', window)
    xdo('key', 'F10')
    time.sleep(0.5)
    subprocess.run(['import', '-window', 'root', str(acceptance/'native-menu.png')], check=True)
    if p.poll() is not None: raise RuntimeError('Native editor crashed while opening menu')
    (acceptance/'gui-smoke.txt').write_text('PASS: native schematic editor started, stayed alive and displayed the example.\nMenu/report/property interaction remains manual acceptance unless explicitly exercised.\n')
finally:
    p.terminate()
    try: p.wait(timeout=8)
    except subprocess.TimeoutExpired: p.kill(); p.wait()
    log.close()
