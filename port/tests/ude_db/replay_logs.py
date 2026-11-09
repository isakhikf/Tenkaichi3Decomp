#!/usr/bin/env python3
"""Replay complete DB1 logs; audit missing fields in older C5/C3 recordings.

Never invents collision outcomes. An incomplete historical record is reported
as not replayable by DB1, rather than filled with guessed eligibility fields.
"""
from pathlib import Path
import argparse
import collections
import csv
import ctypes
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while b:=f.read(1024*1024):h.update(b)
    return h.hexdigest()


class Driver:
    def __init__(self,cc,out,env):
        dll=out/('replay_api.dll' if os.name=='nt' else 'replay_api.so')
        cmd=cc+['-shared','-O2','-std=c99','-Iport/src']+([] if os.name=='nt' else ['-fPIC'])+[
            'port/src/ude_db.c','port/src/ude.c','port/src/ude_recipe.c',HERE/'replay_api.c','-o',dll]
        subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.lib=ctypes.CDLL(str(dll))
        assert self.lib.UdeDbReplay_Version()==71
        self.lib.UdeDbReplay_Step.argtypes=[ctypes.POINTER(ctypes.c_int)]*3+[ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.c_int)]
        self.lib.UdeDbReplay_State.argtypes=[ctypes.c_int,ctypes.POINTER(ctypes.c_uint),ctypes.c_int]
        self.lib.UdeDbReplay_Reset()

    def step(self,row,restore=True):
        r=row['recipe_snapshot'];assert len(r)==71
        config=row['config'];assert len(config)==11
        h=[row['frame'],row['player'],row['opp_character'],row['projectile_hits'],row['own_flag41'],row['own_motion'],row['hp'],row['close'],
           row['my_action_frame'],row['opp_action_frame'],row['pending'],row['recover_ok'],row.get('online',0),row.get('replay',0),row.get('cpu_side',0),
           row.get('fighting',1),row.get('time_stopped',0),1,1,1]
        if restore and 'state_before' in row:
            values=(ctypes.c_uint*13)(*row['state_before']);self.lib.UdeDbReplay_State(row['player'],values,1)
        result=(ctypes.c_uint*7)();rejected=(ctypes.c_int*22)()
        self.lib.UdeDbReplay_Step((ctypes.c_int*20)(*h),(ctypes.c_int*71)(*r),(ctypes.c_int*11)(*config),row['physical'],result,rejected)
        return list(result),list(rejected)

    def state(self,player=0):
        v=(ctypes.c_uint*13)();self.lib.UdeDbReplay_State(player,v,0);return list(v)


def selftest(driver,out):
    header=(ROOT/'port/src/ude_recipe.h').read_text()
    names=re.findall(r'UDE_X_(\w+)',header[header.index('enum {'):header.index('UDE_X_COUNT')])
    ix={n:i for i,n in enumerate(names)}
    db=json.loads((ROOT/'bt3_defesas_db.json').read_text(encoding='utf-8'))
    cfg=list(db['ude_db1_policy']['defaults'].values());cfg[0]=1
    rec=next(r for r in db['attack_records'] if r['character_id']==0 and r['attack_id']>=11 and r['flags']&1 and not r['flags']&0x4000000)
    r=[0]*71
    for key,value in {'OK':1,'MY_ACT':11,'OPP_ACT':0x70,'OWN_FLAGS':0xB1,'FREE':1,'MELEE':1,'FIRST_HIT':1,'DODGE_WIN':1,'FLAG5':1,'ATK':rec['attack_id'],'ATK_FLAGS':rec['flags'],'MY_KI':100000,'OPP_UNK2':2}.items():r[ix[key]]=value
    for i in range(6):r[ix['W1068']+i]=-15 if i==3 else -30
    rows=[]
    for frame in range(120):
        row=dict(schema=1,candidate='UDE-DB1',frame=frame,player=0,opp_character=0,projectile_hits=-1,own_flag41=0,own_motion=0,
                 hp=40000,close=1,my_action_frame=0,opp_action_frame=frame,pending=0,recover_ok=0,
                 physical=0x80 if 20<=frame<25 else 0,recipe_snapshot=list(r),config=list(cfg),state_before=driver.state())
        result,rejected=driver.step(row)
        row.update(recipe=result[0],reason=result[1],output=result[5],added=result[4],press_edge=result[6],rejected=rejected)
        rows.append(row)
    path=out/'synthetic_db1_log.jsonl'
    path.write_text(''.join(json.dumps(row,separators=(',',':'))+'\n' for row in rows),encoding='utf-8')
    report=db1_log(path,driver)
    assert report['matched_decisions']==120 and report['manual_bits_removed']==0 and report['minimum_press_gap']>=30
    report['scope']='synthetic fixture only; no observed game successes'
    return report


def db1_log(path,driver):
    driver.lib.UdeDbReplay_Reset()
    report={'file':str(path),'sha256':sha(path),'rows':0,'matched_decisions':0,'manual_bits_removed':0,'interventions':0,'presses':0,
            'reasons':{},'minimum_press_gap':None,'observed_success_flags':{},'counterfactual_successes':None}
    reasons=collections.Counter();observed=collections.Counter();previous={};min_gap=10**9
    with path.open(encoding='utf-8-sig') as f:
        for line in f:
            row=json.loads(line)
            if row.get('candidate')!='UDE-DB1':raise ValueError('Not a complete DB1 log')
            result,rejected=driver.step(row)
            report['rows']+=1;report['matched_decisions']+=result[0]==row['recipe'] and result[1]==row['reason'] and result[5]==row['output'] and rejected==row['rejected']
            report['manual_bits_removed']+=bool(row['physical']&~result[5]);report['interventions']+=bool(result[4]);report['presses']+=bool(result[6]);reasons[result[1]]+=1
            if result[6]:
                if row['player'] in previous:min_gap=min(min_gap,row['frame']-previous[row['player']])
                previous[row['player']]=row['frame']
            # These flags belong to the recorded trajectory. They are not
            # retroactively assigned to a different policy or input.
            observed['0x12C']+=bool(row.get('observed_evade_flags',0)&8)
            observed['0x79']+=bool(row.get('observed_guard_flags',0)&32)
            observed['0x6B']+=bool(row.get('observed_guard_flags',0)&8)
    report['minimum_press_gap']=None if min_gap==10**9 else min_gap
    report['reasons']=dict(reasons);report['observed_success_flags']=dict(observed)
    report['flag_0x7B']='not exposed by legacy query 25; Z is disabled by default'
    return report


def historical_c5(path,cc,out,env):
    exe=out/('replay_legacy.exe' if os.name=='nt' else 'replay_legacy')
    cmd=cc+['-std=c99','-O2','-Iport/src','port/src/ude.c','port/src/ude_recipe.c','port/tests/ude/replay_log.c','-o',exe]
    subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    result=subprocess.run([str(exe),str(path),'fidelity'],cwd=ROOT,check=True,capture_output=True,text=True).stdout.strip()
    with path.open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
    counts={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',result)}
    assert counts['compared']>0 and counts['same_injected']==counts['compared']
    return {'file':str(path),'sha256':sha(path),'rows':len(rows),'legacy_fidelity':counts,
            'db1_fully_replayable_rows':0,'db1_missing':['recipe_snapshot[71]','opponent character ID','native first projectile hit count','own flag 0x41'],
            'db1_interventions':None,'db1_successes':None,'scope':'Legacy decisions reproduced; DB1 decisions/successes cannot be reconstructed from this schema.'}


def historical_trace(path):
    kinds=collections.Counter();accept=collections.Counter();refuse=collections.Counter()
    removed=changed=pad_rows=0;minus1=0;previous={};epochs=0;last_frame=-1
    labels={22:'guard_or_Z',23:'dodge_or_Sway',24:'projectile_evade',26:'throw_break',27:'deflect',28:'reflect',29:'absorb'}
    if path.stat().st_size%192:raise ValueError('Truncated C3 trace')
    with path.open('rb') as f:
        while data:=f.read(192*4096):
            for row in struct.iter_unpack('<48I',data):
                k=row[2];kinds[k]+=1
                if k==12:
                    if last_frame>=0 and row[1]+120<last_frame:epochs+=1;previous.clear()
                    last_frame=row[1]
                    wins=tuple(ctypes.c_int32(row[i]).value for i in (15,16,14,18,17,19))
                    key=(epochs,row[3])
                    if key in previous:minus1+=sum(now==-1 and old!=-1 for old,now in zip(previous[key],wins))
                    previous[key]=wins
                elif k==0:
                    pad_rows+=1;removed+=bool(row[23]&~row[24]);changed+=row[23]!=row[24]
                if k in labels:
                    label=labels[k]
                    if k==22 and row[26]==2:label='Z_counter'
                    if k==23 and row[26]==2:label='Sonic_Sway'
                    (accept if row[26] else refuse)[label]+=1
    return {'file':str(path),'sha256':sha(path),'records':sum(kinds.values()),'record_kinds':dict(kinds),
            'pad_filter_samples':pad_rows,'samples_with_output_changed':changed,'samples_with_physical_bits_removed':removed,
            'native_calls_accepted':dict(accept),'native_calls_refused':dict(refuse),'window_transitions_to_minus1':minus1,
            'db1_fully_replayable_rows':0,'db1_interventions':None,'db1_successes':None,
            'scope':'C3 observations, possibly mixed offline/online epochs. Accepted native calls include manual and legacy effects. Repeated VBlank samples and -1 transitions are NOT automatically wasted assistant taps.',
            'db1_missing':['pre-input contact ETA','complete recipe snapshot and manual ownership state','per-decision configuration'],
            'flag_success_mapping':'Resolver observations are reported by kind/result, not invented raw flags 0x12C/0x79/0x7B/0x6B.'}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path);p.add_argument('--out',type=Path,default=HERE/'results/replay')
    p.add_argument('--c5',type=Path,default=ROOT/'port/tests/ude/data/real_c5_ude_log.csv');p.add_argument('--c3-dir',type=Path)
    p.add_argument('--db1-log',type=Path,action='append',default=[]);args=p.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True);env=os.environ.copy()
    if args.zig:
        cc=[str(args.zig.resolve()),'cc'];env['ZIG_GLOBAL_CACHE_DIR']=str(HERE/'results/zig-global');env['ZIG_LOCAL_CACHE_DIR']=str(out/'zig-local')
    else:cc=[shutil.which('gcc') or shutil.which('clang') or 'gcc']
    driver=Driver(cc,out,env)
    report={'synthetic_fidelity':selftest(driver,out),'historical_c5':historical_c5(args.c5.resolve(),cc,out,env),
            'historical_c3':[],'db1_logs':[db1_log(f.resolve(),driver) for f in args.db1_log],
            'runtime_defense_success_comparison':None,'limitation':'No counterfactual native acceptance can be deduced from logs of a different policy.'}
    if args.c3_dir:
        report['historical_c3']=[historical_trace(f) for f in sorted(args.c3_dir.glob('*.bt3trace'))]
    (out/'replay_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'synthetic_rows':report['synthetic_fidelity']['rows'],'synthetic_matched':report['synthetic_fidelity']['matched_decisions'],
        'C5_legacy':report['historical_c5']['legacy_fidelity'],'C3_files':len(report['historical_c3']),
        'C3_native_records':sum(r['records'] for r in report['historical_c3']), 'DB1_historical_successes':'not observable'},ensure_ascii=False))


if __name__=='__main__':main()
