import struct
dol=open('/mnt/user-data/uploads/Proyecto Fragile Dreams Farewell Ruins of the Moon/para_claude/US/DATA/sys/main.dol','rb').read()
h=struct.unpack('>18I18I18I',dol[:0xD8]); off,addr,size=h[:18],h[18:36],h[36:54]
T0,TB,TS=0x27c0,0x80006820,0x263cc0
t=dol[T0:T0+TS]
W=[struct.unpack('>I',t[i:i+4])[0] for i in range(0,TS,4)]
# function starts: stwu r1,-X(r1) or after blr; use heuristic: instruction after blr (and following padding)
starts=set([TB])
for i,w in enumerate(W):
    if w==0x4E800020 and i+1<len(W): starts.add(TB+4*(i+1))
starts=sorted(starts)
refs=set()
# branch targets
for i,w in enumerate(W):
    op=w>>26; pc=TB+4*i
    if op==18:
        d=w&0x3FFFFFC; d=d-0x4000000 if d&0x2000000 else d
        refs.add((pc+d) if not (w&2) else d)
    elif op==16:
        d=w&0xFFFC; d=d-0x10000 if d&0x8000 else d; refs.add(pc+d)
# lis/addi pairs in all text
regs={}
for i,w in enumerate(W):
    op=w>>26
    if op==15: regs[(w>>21)&31]=((w&0xffff)<<16,i)
    elif op in (14,24):
        ra=(w>>16)&31; imm=w&0xffff
        if ra in regs and i-regs[ra][1]<64:
            refs.add((regs[ra][0]+((imm-0x10000) if (op==14 and imm&0x8000) else imm))&0xffffffff)
# pointers in all data sections and text0
for o,a,s in zip(off,addr,size):
    if not s: continue
    seg=dol[o:o+s]
    for i in range(0,s-3,4):
        v=struct.unpack('>I',seg[i:i+4])[0]
        if TB<=v<TB+TS: refs.add(v)
dead=[]
for k,s0 in enumerate(starts):
    e=starts[k+1] if k+1<len(starts) else TB+TS
    if s0 not in refs:
        # ensure no ref lands anywhere inside (branch into middle)
        inside=any(s0<=r<e for r in refs)
        if not inside: dead.append((s0,e-s0))
dead.sort(key=lambda x:-x[1])
print(len(starts),len(dead))
for s0,n in dead[:25]: print(hex(s0),n)
import json; json.dump(dead,open('dead.json','w'))
