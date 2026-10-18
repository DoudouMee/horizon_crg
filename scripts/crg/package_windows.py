"""Package actual PE dependencies from the selected MSYS2 native prefix; reject unresolved DLLs."""
import json
import os
import re
import shutil
import subprocess
import sys
from collections import deque
from pathlib import Path

root = Path(__file__).resolve().parents[2]
prefix = Path(sys.argv[1]).resolve()
dest = root / 'dist/horizon-crg-windows'
bindir = dest / 'bin'
bindir.mkdir(parents=True, exist_ok=True)
queue = deque()
for name in ('horizon-eda', 'horizon-imp', 'horizon-crg', 'horizon-crg-native'):
    src = root / 'build' / (name + '.exe')
    shutil.copy2(src, bindir / src.name)
    queue.append(bindir / src.name)
for name in ('gspawn-win64-helper.exe', 'gspawn-win64-helper-console.exe'):
    src = prefix / 'bin' / name
    if src.exists(): shutil.copy2(src, bindir / name); queue.append(bindir / name)
for name in ('gdk-pixbuf-2.0', 'engines-3', 'ossl-modules'):
    source = prefix / 'lib' / name
    if source.exists():
        target = dest / 'lib' / name
        shutil.copytree(source, target, dirs_exist_ok=True)
        queue.extend(target.rglob('*.dll'))
        for extension in ('*.a', '*.la'):
            for archive in target.rglob(extension): archive.unlink()
for name in ('icons/Adwaita', 'icons/hicolor', 'glib-2.0/schemas', 'licenses'):
    source = prefix / 'share' / name
    if source.exists(): shutil.copytree(source, dest / 'share' / name, dirs_exist_ok=True)
lookup = {path.name.lower(): path for path in (prefix / 'bin').glob('*.dll')}
system = Path(os.environ['SYSTEMROOT']) / 'System32'
visited, bundled, unresolved = set(), set(), set()
while queue:
    path = queue.popleft()
    key = str(path).lower()
    if key in visited: continue
    visited.add(key)
    output = subprocess.check_output(['objdump', '-p', str(path)], text=True, errors='replace')
    for name in re.findall(r'DLL Name:\s*(\S+)', output):
        dll = name.lower()
        if dll in lookup:
            if dll not in bundled:
                target = bindir / lookup[dll].name
                shutil.copy2(lookup[dll], target)
                bundled.add(dll)
                queue.append(target)
        elif not dll.startswith(('api-ms-', 'ext-ms-')) and not (system / name).exists():
            unresolved.add(name)
if unresolved: raise RuntimeError('Unresolved PE imports: ' + ', '.join(sorted(unresolved)))
# Generate a cache with package-relative module paths. The launcher sets the package working directory.
loaders = list((dest / 'lib/gdk-pixbuf-2.0').glob('*/loaders/*.dll'))
if loaders:
    cache = subprocess.check_output([str(prefix/'bin/gdk-pixbuf-query-loaders.exe'), *map(str, loaders)], text=True)
    for base in (str(dest).replace('\\', '\\\\')+'\\\\', dest.as_posix()+'/', str(dest)+'\\'):
        cache = cache.replace(base, '')
    (loaders[0].parent.parent / 'loaders.cache').write_text(cache, encoding='utf-8')
shutil.copytree(root/'examples/crg/demo', dest/'examples/demo', dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('*.db', '*.db-*'))
shutil.copy2(root/'scripts/crg/open-demo.bat', dest/'open-demo.bat')
shutil.copy2(root/'docs/crg.md', dest/'README.md')
shutil.copy2(root/'LICENSE', dest/'LICENSE')
acceptance = root/'acceptance'
acceptance.mkdir(exist_ok=True)
(acceptance/'runtime-dependencies.json').write_text(json.dumps(sorted(bundled), indent=2), encoding='utf-8')
# Exclude MSYS2/UCRT from PATH, so tool success also tests the shipped runtime closure.
env = dict(os.environ, PATH=str(bindir)+os.pathsep+str(system), HORIZON_POOL=str(dest/'examples/demo/pool'))
subprocess.run([str(bindir/'horizon-crg.exe'), str(dest/'examples/demo/blocks.json'), str(dest/'examples/demo/pool'), str(acceptance/'packaged-demo')], env=env, cwd=dest, check=True)
subprocess.run([str(bindir/'horizon-crg-native.exe'), str(dest/'examples/demo/blocks.json'), str(dest/'examples/demo/pool'), str(acceptance/'packaged-roundtrip')], env=env, cwd=dest, check=True)
shutil.make_archive(str(root/'dist/horizon-crg-windows'), 'zip', root_dir=dest.parent, base_dir=dest.name)
print('Windows package verified with', len(bundled), 'runtime DLLs')
