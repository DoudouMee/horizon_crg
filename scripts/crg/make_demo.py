#!/usr/bin/env python3
"""Generate a reproducible, self-contained native Horizon CRG schematic and pool."""
import json
import sys
import uuid
from pathlib import Path

NS = uuid.UUID('58be5c8b-4981-4b81-a628-8d9f2908323d')
def uid(name): return str(uuid.uuid5(NS, name))
def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
def placement(x=0, y=0): return dict(shift=[x, y], angle=0, mirror=False)
def text(label, x, y, size=1800000):
    return dict(text=label, placement=placement(x, y), origin='center', size=size, width=0, layer=0)

def generate(dest):
    pool = dest / 'pool'
    for folder in ('units', 'entities', 'symbols', 'packages', 'parts', 'padstacks', 'frames', 'decals', '3d_models'):
        (pool / folder).mkdir(parents=True, exist_ok=True)
        (pool / folder / '.keep').touch()
    (dest / 'pictures').mkdir(parents=True, exist_ok=True)
    (dest / 'pictures' / '.keep').touch()
    null = str(uuid.UUID(int=0))
    write(pool / 'pool.json', dict(type='pool', uuid=uid('pool'), name='CRG verification primitives', default_via=null,
                                  default_frame=null, pools_included=[]))
    bid, nc, sid = uid('top'), uid('nc'), uid('sheet')
    block = dict(type='block', uuid=bid, name='CRG demonstration', net_class_default=nc, net_classes={nc:dict(name='default')},
                 nets={}, buses={}, components={}, block_instances={}, project_meta={
                     'project_name':'crg-demo', 'project_title':'CRG verification demonstration',
                     'crg.modes':json.dumps({'functional':{'test_mode':0, 'scan_enable':0},
                                             'dft_shift':{'test_mode':1, 'scan_enable':1},
                                             'dft_capture':{'test_mode':1, 'scan_enable':0}})})
    sheet = dict(name='Clock and reset verification', index=1, frame=None, junctions={}, symbols={}, texts={},
                 net_labels={}, power_symbols={}, bus_rippers={}, net_lines={}, bus_labels={}, lines={}, arcs={})
    sheet['texts'][uid('title')] = text('CRG / Native schematic / Functional + DFT', 65000000, 20000000, 3000000)
    # role, direction, native net. Module input roles use simple electrical-level semantics.
    modules = [
        ('XTAL', 'clock_source', [('out','output','xtal')], {'frequency_hz':24000000}, 0, 0),
        ('LOCK', 'control_source', [('out','output','lock')], {'key':'pll_lock','values':[1], 'binding':'tb.pll_locked'}, 0, -1),
        ('PLL', 'pll', [('in','input','xtal'),('lock','input','lock'),('out','output','pll')], {'multiply':20,'divide':1}, 1, 0),
        ('TCLK', 'clock_source', [('out','output','test_clk')], {'frequency_hz':50000000}, 1, -2),
        ('TMODE', 'control_source', [('out','output','test_mode')], {'key':'test_mode','values':[0,1],'binding':'tb.test_mode'}, 1, -3),
        ('MUX', 'mux', [('in0','input','pll'),('in1','input','test_clk'),('sel','input','test_mode'),('out','output','selected')],
         {'select_map':{'0':'in0','1':'in1'}, 'switch_protocol':'Stop affected clocks, change test_mode, restart and wait for stability'}, 2, 0),
        ('CFG', 'control_source', [('out','output','div_cfg')], {'key':'div_cfg','values':[0,1],'binding':'reg.CRG_DIV[0]'}, 2, -2),
        ('DIV', 'divider', [('in','input','selected'),('cfg','input','div_cfg'),('out','output','divided')], {'divide_map':{'0':2,'1':4}}, 3, 0),
        ('EN', 'control_source', [('out','output','clk_en')], {'key':'clk_en','values':[0,1],'binding':'reg.CRG_EN[0]'}, 3, -2),
        ('SCAN', 'control_source', [('out','output','scan_enable')], {'key':'scan_enable','values':[0,1],'binding':'tb.scan_enable'}, 3, -3),
        ('ICG', 'gate', [('in','input','divided'),('en','input','clk_en'),('test_en','input','scan_enable'),('out','output','core_clk')], {}, 4, 0),
        ('CORE_CLK', 'clock_output', [('in','input','core_clk')], {'binding':'dut.core_clk'}, 5, 0),
        ('POR', 'reset_source', [('out','output','por_n')], {'key':'por_n','values':[0,1],'active_level':0,'binding':'tb.por_n'}, 1, -5),
        ('RSTSYNC', 'reset_sync', [('in','input','por_n'),('clk','input','core_clk'),('out','output','core_rst_n')],
         {'input_active_level':0,'output_active_level':0,'stages':2,'async_assert':True}, 4, -5),
        ('CORE_RST', 'reset_output', [('in','input','core_rst_n')], {'binding':'dut.core_rst_n'}, 5, -5),
    ]
    for name, kind, ports, params, col, row in modules:
        mid, unit_id, gate_id, entity_id, symbol_id = (uid(name + '/' + k) for k in ('component','unit','gate','entity','symbol'))
        unit = dict(type='unit', uuid=unit_id, name=name, manufacturer='', pins={})
        sym = dict(type='symbol', uuid=symbol_id, unit=unit_id, name=name, pins={}, junctions={}, lines={}, texts={})
        inputs = [p for p in ports if p[1]=='input']
        outputs = [p for p in ports if p[1]=='output']
        for direction, group in [('input',inputs), ('output',outputs)]:
            for index, (role, _, net) in enumerate(group):
                pid = uid(name + '/pin/' + role)
                unit['pins'][pid] = dict(primary_name=role, direction=direction)
                y = int((len(group)-1)*2500000-index*5000000)
                x = -10000000 if direction=='input' else 10000000
                sym['pins'][pid] = dict(position=[x,y], length=2500000,
                                       orientation='right' if direction=='input' else 'left', pad_visible=False)
        for index, point in enumerate([[-7500000,-10000000],[7500000,-10000000],[7500000,10000000],[-7500000,10000000]]):
            sym['junctions'][uid(name + '/corner/' + str(index))] = dict(position=point)
        for index in range(4): sym['lines'][uid(name + '/edge/' + str(index))] = {
            'from':uid(name + '/corner/' + str(index)), 'to':uid(name + '/corner/' + str((index+1)%4)), 'width':180000, 'layer':0}
        sym['texts'][uid(name + '/refdes-text')] = text('$REFDES', 0, 12000000)
        sym['texts'][uid(name + '/kind-text')] = text(kind, 0, 0, 1500000)
        write(pool / 'units' / (name+'.json'), unit)
        write(pool / 'entities' / (name+'.json'), dict(type='entity', uuid=entity_id, name=name, prefix='U', gates={gate_id:dict(name='A', suffix='', unit=unit_id)}))
        write(pool / 'symbols' / (name+'.json'), sym)
        comp = dict(entity=entity_id, refdes=name, value=kind, connections={}, crg=dict(kind=kind, ports={}, parameters=params))
        px, py = col*43000000, row*28000000
        sym_inst = uid(name+'/placed')
        sheet['symbols'][sym_inst] = dict(component=mid, gate=gate_id, symbol=symbol_id, placement=placement(px,py))
        for role, direction, net in ports:
            pid = uid(name + '/pin/' + role); path = gate_id + '/' + pid
            nid = uid('net/' + net)
            block['nets'][nid] = dict(name=net, net_class=nc, is_power=False)
            comp['connections'][path] = dict(net=nid)
            comp['crg']['ports'][role] = path
            pinpos = sym['pins'][pid]['position']
            junc = uid(name+'/junction/'+role)
            dx = -6000000 if direction=='input' else 6000000
            sheet['junctions'][junc] = dict(position=[px+pinpos[0]+dx, py+pinpos[1]])
            sheet['net_lines'][uid(name+'/wire/'+role)] = dict(
                **{'from':dict(junc=None,pin=sym_inst+'/'+pid,bus_ripper=None), 'to':dict(junc=junc,pin=None,bus_ripper=None)},
                width=0, net=nid, bus=None)
            sheet['net_labels'][uid(name+'/label/'+role)] = dict(junction=junc, orientation='right', size=1250000, last_net=nid)
        block['components'][mid] = comp
    write(dest/'top_block.json', block)
    write(dest/'top_schematic.json', dict(type='schematic', uuid=uid('schematic'), name='CRG', sheets={sid:sheet}))
    write(dest/'blocks.json', dict(type='blocks', top_block=bid, blocks={bid:dict(block_filename='top_block.json', symbol_filename='', schematic_filename='top_schematic.json')}))
    return dest

if __name__=='__main__':
    print(generate(Path(sys.argv[1]) if len(sys.argv)>1 else Path('examples/crg/demo')))
