#!/usr/bin/python3
"""fn.py <gen_dir> <C function name> [x] : prints the names of runtime functions a generated function calls (and its body with x)"""
import re,sys,glob,os
gen=sys.argv[1]; want=sys.argv[2]
names=[l.strip().strip('",') for l in open(gen+'/gm_tables.c').read().split('\n')[19733+1:19733+1+400]]
for f in sorted(glob.glob(gen+'/gm_code_*.c')):
    s=open(f).read()
    m=re.search(r'\ngm_val '+want+r'\(.*?\n\}\n',s,re.S)
    if m:
        body=m.group(0)
        calls=re.findall(r'gm_ftab\[(\d+)\]',body)
        print(os.path.basename(f), len(body.split('\n')), 'calls:',' '.join(sorted(set(names[int(c)] for c in calls))))
        if len(sys.argv)>3: print(body)
