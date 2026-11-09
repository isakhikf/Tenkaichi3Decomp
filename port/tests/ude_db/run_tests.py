#!/usr/bin/env python3
"""Run scalar, adapter, and unchanged-native-body tests with gcc/clang/Zig."""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent


def function(text,name):
    m=re.search(r'^[ \t]*(?:(?:static|inline)\s+)*(?:void|s32|u32|f32|int|UrIncoming)\s+'+re.escape(name)+r'\([^;\n]*\)\s*\{',text,re.M)
    if not m:raise ValueError('Native function missing: '+name)
    start=text.index('{',m.start());depth=1;i=start+1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[m.start():i]


def native_fixture(out):
    tech=(ROOT/'src/battle/btl_tech_a.c').read_text()
    header=(ROOT/'include/battle/btl_tech_a.h').read_text()
    record=re.search(r'typedef struct BtlAtkRecord\s*\{.*?\}\s*BtlAtkRecord\s*;',header,re.S)[0]
    move=(ROOT/'src/battle/btl_char_move.c').read_text()
    hit=(ROOT/'src/battle/btl_char_hit.c').read_text()
    incoming=re.search(r'typedef struct UrIncoming\s*\{.*?\}\s*UrIncoming\s*;',hit,re.S)[0]
    prelude='''typedef int s32;typedef unsigned u32;typedef unsigned char u8;typedef signed char s8;typedef unsigned short u16;typedef float f32;
'''+record+'''
typedef struct BtlTechChr {BtlAtkRecord *record;int id,ability40;} BtlTechChr;
static BtlAtkRecord *BtlAtk_GetRecord(BtlTechChr *c,s32 *id){if(id)*id=c->id;return c->record;}
static int BtlOpp_HasAbility(BtlTechChr *c,int id){return id==0x40&&c->ability40;}
typedef struct BtlMoveChr {int unk1068,unk106C,unk1070,unk1074,unk1078,unk107C,unk1080,unkFB0;unsigned held,previous,caps;unsigned char flags[400];float health;int ability38;} BtlMoveChr;
static int BtlInput_TestAction(BtlMoveChr *c,int id,int on){unsigned p=c->held&~c->previous;(void)on;return id==39?!!(p&1):id==38?((p&0x18)==0x18):id==43?!!((c->held&1)&&(p&0xC0)):0;}
static float BtlMember_GetHealthRatio(BtlMoveChr *c){return c->health;}
static int BtlChar_TestFlag(BtlMoveChr *c,int f){return c->flags[f];}
static unsigned BtlParam_GetFlags2(BtlMoveChr *c){return c->caps;}
static int BtlMember_HasAbility(BtlMoveChr *c,int id){return id==0x38&&c->ability38;}
'''
    bodies=[function(tech,n) for n in ['BtlAtk_IsId13to21','BtlAtk_GetPriority','BtlAtk_GetFlags','BtlAtk_GetGuardKindA','BtlAtk_GetGuardKindB','BtlAtk_GetGuardKindC','BtlAtk_RankGuardKind','BtlAtk_GetBestGuardKind','BtlAtk_GetGuardKindE']]
    (out/'native_functions.h').write_text('/* Do not edit. Exact native function bodies for host validation. */\n'+prelude+'\n'+'\n'.join(bodies)+'\n'+function(move,'BtlMove_UpdateDefenseTimers')+'\n'+incoming+'\n'+function(hit,'ur_merge')+'\n')
    db=json.loads((ROOT/'bt3_defesas_db.json').read_text(encoding='utf-8'))
    with (out/'expected_guards.bin').open('wb') as f:
        for r in db['attack_records']:
            names=['1_mid','2_high','3_low','4_best_raw','5_E','6_all']
            f.write(struct.pack('<Ib4b12b',r['flags'],r['priority'],*[r[k] for k in ('guard_a','guard_b','guard_c','guard_e')],
                *[r['guard_results_no_ability40'][k] for k in names],*[r['guard_results_ability40'][k] for k in names]))


def main():
    p=argparse.ArgumentParser();p.add_argument('--zig',type=Path);p.add_argument('--out',type=Path,default=HERE/'results')
    args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    if args.zig:
        cc=[str(args.zig.resolve()),'cc'];env['ZIG_GLOBAL_CACHE_DIR']=str(out/'zig-global');env['ZIG_LOCAL_CACHE_DIR']=str(out/'zig-local')
    else:
        compiler=shutil.which('gcc') or shutil.which('clang')
        if not compiler:raise SystemExit('Need gcc/clang or --zig path')
        cc=[compiler]
    def run(command,name):
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,text=True,encoding='utf-8',errors='replace',stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/name).write_text(result.stdout,encoding='utf-8')
        if result.returncode:
            print(result.stdout);raise SystemExit(f'{name} failed ({result.returncode})')
        return result.stdout
    run([sys.executable,HERE/'check_generated.py'],'generated.txt')
    native_fixture(out)
    suffix='.exe' if os.name=='nt' else ''
    core=['port/src/ude_db.c','port/src/ude.c','port/src/ude_recipe.c']
    report={}
    for name,sources,extra in [('test_ude_db',core+[HERE/'test_ude_db.c'],[]),
                               ('test_native',core+[HERE/'test_native.c'],[out/'expected_guards.bin']),
                               ('test_adapter',core+['port/src/ude_db_game.c',HERE/'test_adapter.c'],[])]:
        binary=out/(name+suffix)
        run(cc+['-std=c99','-O2','-Wall','-Wextra','-Werror','-Iport/src','-I'+str(out)]+sources+['-o',binary],name+'_compile.txt')
        text=run([binary]+extra,name+'.txt')
        report[name]={'passed':True,'result':text.strip().splitlines()[-1]}
        print(report[name]['result'])
    report['level']='host synthetic policy + actual native C timer/accessor bodies; not a running fight'
    report['compiler']=cc
    (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
