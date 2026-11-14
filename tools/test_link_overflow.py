"""Regression: actual ARM linker must reject an image invading config Flash."""
from pathlib import Path
import argparse
import subprocess
import re
p=argparse.ArgumentParser()
p.add_argument('--output-dir',required=True,type=Path)
p.add_argument('--toolchain',type=Path,default=Path('D:/software/kelic51/ARM/ARMCC/bin'))
a=p.parse_args(); out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
source=out/'over_limit.c'; obj=out/'over_limit.o'
source.write_text('const unsigned char over_limit_blob[50000] = {1};\n')
subprocess.run([str(a.toolchain/'armcc.exe'),'--cpu','Cortex-M3','-c',str(source),'-o',str(obj)],check=True)
via=(root/'code/Objects/IR_Remote.lnp').read_text()
via=re.sub(r'--list\s+"[^"]+"\s+-o\s+[^\r\n]+',
           lambda m:f'--list "{out / "overflow.map"}" -o "{out / "overflow.axf"}"',via)
via+=f'\n"{obj}"\n--keep=over_limit.o(.constdata)\n'
command=out/'over_limit.lnp';command.write_text(via)
r=subprocess.run([str(a.toolchain/'armlink.exe'),'--via',str(command)],cwd=root/'code',capture_output=True,text=True)
(out/'link_overflow.log').write_text(r.stdout+r.stderr)
assert r.returncode!=0, 'UNSAFE: oversized image linked successfully'
assert re.search(r'L622[01]E|L6406E',r.stdout+r.stderr),(r.stdout+r.stderr)[:2000]
print('actual ARM linker rejects 50000-byte extra RO data at 48KB boundary: PASS')
