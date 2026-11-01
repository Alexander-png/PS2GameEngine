import sys
sys.argv=[sys.argv[0],sys.argv[1]]+sys.argv[2:]
import gmroomgraph2 as g
# monkeypatch: run main but capture obj_eff via closure is awkward; replicate minimal
from gmwin import DataWin
from gmref import build_refs
dw=DataWin(sys.argv[1]); vr,fr,_=build_refs(dw)
funcs,vars_=dw.functions(),dw.variables(); ents=dw.code_entries()
rooms,objs,evs=dw.rooms(),dw.objects(),dw.object_events()
rp=dw.ptr_list("ROOM"); ri=[r["name"] for r in rooms].index(sys.argv[2])
seen=set()
for oi,cc in g.room_instances(dw,ri,rp):
    if oi in seen or not 0<=oi<len(objs): continue
    seen.add(oi); n=0; ung=0
    for t,sub,cid,who in evs[oi]:
        if 0<=cid<len(ents) and ents[cid][1]:
            c,go,a,cp=g.scan(dw,vr,fr,funcs,vars_,ents[cid][1],ents[cid][4])
            for x in go:
                if x[0]=="goto" and x[1] and x[1][0]=="int" and len(x)==2: ung+=1
                n+=1
    if ung: print(objs[oi]["name"] if isinstance(objs[oi],dict) else objs[oi][0],ung,n)
