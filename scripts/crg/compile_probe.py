"""Compile changed native integration units before the full build for quick diagnostics."""
import subprocess
import sys

build = sys.argv[1]
listing = subprocess.check_output(['ninja', '-C', build, '-t', 'targets', 'all'], text=True)
patterns = ('src_crg_', 'src_imp_imp_schematic_crg.cpp', 'src_block_component.cpp', 'src_imp_fab_output_window.cpp')
targets = []
for line in listing.splitlines():
    target, sep, rule = line.partition(': ')
    if sep and target.endswith(('.o', '.obj')) and any(pattern in target for pattern in patterns):
        targets.append(target)
if not targets: raise RuntimeError('Native compile probe did not find integration targets')
subprocess.run(['ninja', '-C', build, '-j', '4', *targets], check=True)
