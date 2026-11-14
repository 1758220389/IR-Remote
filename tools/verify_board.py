"""Read back firmware and runtime health over ST-Link (no Flash writes)."""
import argparse
import json
import time
from pathlib import Path
from elftools.elf.elffile import ELFFile
from elftools.dwarf.dwarf_expr import DWARFExprParser
from pyocd.core.helpers import ConnectHelper
p=argparse.ArgumentParser()
p.add_argument('--elf',required=True,type=Path)
p.add_argument('--pack',required=True)
p.add_argument('--probe',required=True)
p.add_argument('--config-backup',type=Path)
p.add_argument('--capture-output',type=Path)
p.add_argument('--dma-output',type=Path)
a=p.parse_args()
with a.elf.open('rb') as f:
    elf=ELFFile(f)
    segments=[(s['p_paddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD' and s['p_filesz']]
    symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    dwarf=elf.get_dwarf_info()
    typedef=next(d for cu in dwarf.iter_CUs() for d in cu.iter_DIEs()
                 if d.tag=='DW_TAG_typedef' and d.attributes.get('DW_AT_name')
                 and d.attributes['DW_AT_name'].value==b'UiState')
    members={d.attributes['DW_AT_name'].value:d for d in typedef.get_DIE_from_attribute('DW_AT_type').iter_children() if d.tag=='DW_TAG_member'}
    signal_type=members[b'captured'].get_DIE_from_attribute('DW_AT_type')
    while signal_type.tag=='DW_TAG_typedef':signal_type=signal_type.get_DIE_from_attribute('DW_AT_type')
    signal_size=signal_type.attributes['DW_AT_byte_size'].value
    capture_location=members[b'captured'].attributes['DW_AT_data_member_location'].value
    if isinstance(capture_location,int): captured_offset=capture_location
    else:
        ops=DWARFExprParser(dwarf.structs).parse_expr(capture_location)
        assert len(ops)==1 and ops[0].op_name=='DW_OP_plus_uconst'
        captured_offset=ops[0].args[0]
    dma_members={}
    if a.dma_output:
        dma_type=next(d for cu in dwarf.iter_CUs() for d in cu.iter_DIEs()
                      if d.tag=='DW_TAG_typedef' and d.attributes.get('DW_AT_name')
                      and d.attributes['DW_AT_name'].value==b'IrDmaProbe')
        for member in dma_type.get_DIE_from_attribute('DW_AT_type').iter_children():
            if member.tag!='DW_TAG_member':continue
            location=member.attributes['DW_AT_data_member_location'].value
            if isinstance(location,int): offset=location
            else:
                ops=DWARFExprParser(dwarf.structs).parse_expr(location)
                assert len(ops)==1 and ops[0].op_name=='DW_OP_plus_uconst'
                offset=ops[0].args[0]
            dma_members[member.attributes['DW_AT_name'].value.decode()]=offset
session=ConnectHelper.session_with_chosen_probe(unique_id=a.probe,target_override='stm32f103c8',pack=a.pack,connect_mode='attach',frequency=1000000)
assert session is not None,'ST-Link unavailable'
with session:
    target=session.target
    for addr,data in segments:
        assert 0x08000000<=addr and addr+len(data)<=0x0800C000
        assert bytes(target.read_memory_block8(addr,len(data)))==data,'Firmware readback mismatch'
    if a.config_backup:
        before=a.config_backup.read_bytes()
        assert bytes(target.read_memory_block8(0x0800C000,len(before)))==before,'Download changed reserved configuration banks'
    clock=target.read32(symbols['SystemCoreClock'])
    t0=target.read32(symbols['milliseconds']);time.sleep(.2)
    t1=target.read32(symbols['milliseconds'])
    assert clock==72000000,clock
    assert ((t1-t0)&0xFFFFFFFF)>0,'SysTick is not advancing'
    assert target.read32(0xE000ED28)==0,'CFSR fault status is nonzero'
    state=bytes(target.read_memory_block8(symbols['ui'],16))
    diag=target.read_memory_block32(symbols['g_app_diagnostics'],9)
    print(json.dumps({'firmware_readback':'PASS','clock_hz':clock,'tick_delta_ms':(t1-t0)&0xFFFFFFFF,'ui_prefix_hex':state.hex(),'diagnostics':diag,'cpu_state':str(target.get_state()),'config_download_preserved':bool(a.config_backup)},indent=2))
    if a.capture_output:
        # Use the actual ARM DWARF member offset, never host ABI assumptions.
        signal=bytes(target.read_memory_block8(symbols['ui']+captured_offset,signal_size))
        a.capture_output.write_bytes(signal)
        print('Captured UiSignal snapshot:',a.capture_output)
    if a.dma_output:
        import struct
        base=symbols['g_ir_dma_probe']
        assert target.read8(base+dma_members['running'])==0,'DMA still running; wait for a frozen capture'
        falls=target.read16(base+dma_members['fall_count'])
        rises=target.read16(base+dma_members['rise_count'])
        flags=target.read32(base+dma_members['flags'])
        assert falls<=512 and rises<=512
        f=list(struct.unpack('<'+'H'*falls,bytes(target.read_memory_block8(base+dma_members['fall'],2*falls))))
        r=list(struct.unpack('<'+'H'*rises,bytes(target.read_memory_block8(base+dma_members['rise'],2*rises))))
        durations=[]
        if falls>=rises and falls-rises<=1:
            stamps=[f[i//2] if i%2==0 else r[i//2] for i in range(falls+rises)]
            durations=[(right-left)&65535 for left,right in zip(stamps,stamps[1:])]
        record={'source':'Independent TIM4 PB7 dual-input hardware capture and DMA, STLink readback',
                'fall_count':falls,'rise_count':rises,'flags':flags,
                'fall_timestamps':f,'rise_timestamps':r,'timings_us':durations}
        a.dma_output.write_text(json.dumps(record,indent=2))
        print('Frozen DMA capture:',a.dma_output,'falls',falls,'rises',rises,'flags',flags,'durations',len(durations))
