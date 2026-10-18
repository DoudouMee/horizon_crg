"""Launch the actual native editor under Xvfb, open its CRG menu and capture evidence."""
import os
import json
import re
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
    xdo('windowfocus', window)
    xdo('key', 'Home')
    time.sleep(0.5)
    subprocess.run(['import', '-window', 'root', str(acceptance/'native-editor.png')], check=True)
    # GTK F10 opens the main menu. The action below opens the report itself.
    xdo('key', 'F10')
    time.sleep(0.5)
    subprocess.run(['import', '-window', 'root', str(acceptance/'native-menu.png')], check=True)
    if p.poll() is not None: raise RuntimeError('Native editor crashed while opening menu')
    xdo('key', 'Escape')
    properties = subprocess.check_output(['xprop', '-id', window, '_GTK_UNIQUE_BUS_NAME', '_GTK_WINDOW_OBJECT_PATH'], text=True)
    bus = re.search(r'_GTK_UNIQUE_BUS_NAME[^=]*= "([^"]+)"', properties)
    path = re.search(r'_GTK_WINDOW_OBJECT_PATH[^=]*= "([^"]+)"', properties)
    if not bus or not path: raise RuntimeError('Native GTK action endpoint is unavailable')
    invocation = subprocess.Popen(['gdbus', 'call', '--session', '--dest', bus.group(1), '--object-path', path.group(1),
                                  '--method', 'org.gtk.Actions.Activate', 'crg_analysis', '[]', '{}'], stdout=log, stderr=log)
    report_window = None
    for _ in range(40):
        try:
            candidates = xdo('search', '--onlyvisible', '--name', '^CRG verification analysis$').splitlines()
            if candidates: report_window = candidates[-1]; break
        except subprocess.CalledProcessError: pass
        time.sleep(0.2)
    if not report_window: raise RuntimeError('CRG verification report did not open through the native window action')
    subprocess.run(['import', '-window', 'root', str(acceptance/'native-report.png')], check=True)
    xdo('windowfocus', report_window)
    # The native notebook's second tab is visible at this location in the report.
    xdo('mousemove', '--window', report_window, '205', '64')
    xdo('click', '1')
    time.sleep(0.5)
    subprocess.run(['import', '-window', 'root', str(acceptance/'native-scenarios.png')], check=True)
    xdo('mousemove', '--window', report_window, '930', '700')
    xdo('click', '1')
    chooser_window = None
    for _ in range(40):
        try:
            candidates = xdo('search', '--onlyvisible', '--name', '^Export CRG verification data$').splitlines()
            if candidates: chooser_window = candidates[-1]; break
        except subprocess.CalledProcessError: pass
        time.sleep(0.2)
    if not chooser_window: raise RuntimeError('CRG export file chooser did not open')
    export = acceptance/'gui-export.json'
    xdo('windowfocus', chooser_window)
    xdo('key', 'ctrl+l')
    xdo('type', '--clearmodifiers', str(export))
    xdo('key', 'Return')
    for _ in range(40):
        if export.exists() and export.with_suffix('.csv').exists(): break
        time.sleep(0.2)
    else: raise RuntimeError('Native UI did not write all export files')
    actual = json.loads(export.read_text())
    expected = json.loads((acceptance/'demo.json').read_text())
    for key in ('valid_structure', 'scenarios', 'diagnostics'):
        if actual[key] != expected[key]: raise RuntimeError('UI export differs from headless analysis: '+key)
    for extension in ('.dot', '.csv'):
        if export.with_suffix(extension).read_bytes() != (acceptance/('demo'+extension)).read_bytes():
            raise RuntimeError('UI companion export differs from headless analysis: '+extension)
    xdo('key', 'Return')
    time.sleep(0.3)
    xdo('windowfocus', report_window)
    xdo('key', 'Escape')
    invocation.wait(timeout=5)
    if p.poll() is not None: raise RuntimeError('Native editor exited after closing the CRG report')
    (acceptance/'gui-smoke.txt').write_text('PASS: native editor displayed the example, opened the CRG report, and exported matching JSON/DOT/CSV through the file chooser.\nProperty editing remains manual acceptance.\n')
except Exception:
    subprocess.run(['import', '-window', 'root', str(acceptance/'gui-failure.png')], check=False)
    raise
finally:
    p.terminate()
    try: p.wait(timeout=8)
    except subprocess.TimeoutExpired: p.kill(); p.wait()
    log.close()
