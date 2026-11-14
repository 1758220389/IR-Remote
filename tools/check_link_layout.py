"""Check generated Keil linker command/map, not only GUI memory settings."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
command=(root/'code/Objects/IR_Remote.lnp').read_text(errors='replace')
assert '--scatter' in command, 'Linker must use an explicit bounded scatter file'
layout=(root/'code/IR_Remote.sct').read_text()
assert re.search(r'LR_IROM1\s+0x08000000\s+0x0000C000',layout)
assert re.search(r'RW_IRAM1\s+0x20000000\s+0x00005000',layout)
maptext=(root/'code/Listings/IR_Remote.map').read_text(errors='replace')
assert re.search(r'Load Region LR_IROM1.*Max:\s*0x0000c000',maptext,re.I)
assert re.search(r'Execution Region RW_IRAM1.*Max:\s*0x00005000',maptext,re.I)
print('actual linker/map: Flash bounded 48KB, RAM bounded 20KB; reserved banks excluded: PASS')
