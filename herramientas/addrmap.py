import struct,collections,json,sys
U='/mnt/user-data/uploads/Proyecto Fragile Dreams Farewell Ruins of the Moon/para_claude/US_REF/main.dol'
E='/mnt/user-data/uploads/Proyecto Fragile Dreams Farewell Ruins of the Moon/para_claude/US/DATA/sys/main.dol'
def load(p):
    b=open(p,'rb').read(); h=struct.unpack('>18I18I18I',b[:0xD8])
    secs=[(h[i],h[18+i],h[36+i]) for i in range(18) if h[36+i]]
    text=[(o,a,s) for (o,a,s),i in zip(secs,range(99)) if i<2]
    words=[]; addrs=[]
    for o,a,s in [(h[i],h[18+i],h[36+i]) for i in range(7) if h[36+i]]:
        for k in range(0,s,4): words.append(struct.unpack('>I',b[o+k:o+k+4])[0]); addrs.append(a+k)
    return b,h,words,addrs
def regs_init(words):
    r={}
    for i,w in enumerate(words[:3000]):
        op=w>>26
        if op==15 and ((w>>21)&31) in (2,13): r[(w>>21)&31]=((w&0xffff)<<16)
        if op==24 and ((w>>21)&31) in (2,13) and ((w>>16)&31) in (2,13): r[(w>>21)&31]|=w&0xffff
    return r
def norm(w):
    op=w>>26
    if op in (14,15,24,25,12,13): return w&0xFFFF0000
    if op==18: return w&0xFC000003
    if 32<=op<=55:
        ra=(w>>16)&31
        if ra in (2,13): return w&0xFFFF0000
    return w
def refs(words,addrs,base):
    out={}  # index -> referenced addr
    lis={}
    for i,w in enumerate(words):
        op=w>>26; rd=(w>>21)&31; ra=(w>>16)&31; imm=w&0xffff; simm=imm-0x10000 if imm&0x8000 else imm
        if op==15: lis[rd]=(imm<<16,i); continue
        if (op==14 or 32<=op<=55) and ra in lis and i-lis[ra][1]<48:
            out[i]=(lis[ra][0]+simm)&0xffffffff
        elif op==24 and ra in lis and i-lis[ra][1]<48:
            out[i]=(lis[ra][0]|imm)&0xffffffff
        elif (op==14 or 32<=op<=55) and ra in (2,13):
            out[i]=(base[ra]+simm)&0xffffffff
        if op in (14,15,24) and rd in lis and not (op==14 and ra==rd) and not (op==24 and ra==rd): lis.pop(rd,None)
    return out
bu,hu,wu,au=load(U); be,he,we,ae=load(E)
ru=regs_init(wu); re_=regs_init(we); print('r2/r13 USA',{k:hex(v) for k,v in ru.items()},'EUR',{k:hex(v) for k,v in re_.items()})
nu=[norm(w) for w in wu]; ne=[norm(w) for w in we]
K=12
def kg(n):
    d=collections.defaultdict(list)
    for i in range(len(n)-K): d[tuple(n[i:i+K])].append(i)
    return d
du=kg(nu); de=kg(ne)
anchors=[]
for k,l in du.items():
    if len(l)==1 and k in de and len(de[k])==1: anchors.append((l[0],de[k][0]))
anchors.sort()
# keep monotonic chain (LIS)
import bisect
seq=[a[1] for a in anchors]; tv=[]; ti=[]; prev=[-1]*len(seq)
for i,x in enumerate(seq):
    j=bisect.bisect_left(tv,x)
    if j==len(tv): tv.append(x); ti.append(i)
    else: tv[j]=x; ti[j]=i
    prev[i]=ti[j-1] if j>0 else -1
k=ti[-1]; chain=[]
while k!=-1: chain.append(anchors[k]); k=prev[k]
chain.reverse()
print('anchors',len(anchors),'chain',len(chain))
imap={}
for iu,ie in chain:
    for d in range(K): imap[iu+d]=ie+d
print('mapped instr',len(imap),'of',len(wu))
fu=refs(wu,au,ru); fe=refs(we,ae,re_)
votes=collections.defaultdict(collections.Counter)
for i,a in fu.items():
    j=imap.get(i)
    if j is not None and j in fe and norm(wu[i])==norm(we[j]): votes[a][fe[j]]+=1
M={}
for a,c in votes.items():
    v,n=c.most_common(1)[0]; M[a]=v
print('address pairs',len(M))
json.dump({'%08X'%k:'%08X'%v for k,v in sorted(M.items())},open(sys.argv[1],'w'))
# delta stats per region
dd=collections.Counter()
for a,v in M.items():
    if a>=0x80269e80: dd[(a>>16, v-a)]+=1
print(sorted(dd.items())[:80])
