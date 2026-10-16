import json,struct,sys
P=json.load(open('/mnt/user-data/outputs/fd/codigos/parche_mando_eur.json'))
def apply(dol):
    b=bytearray(dol)
    hdr=list(struct.unpack('>18I18I18I',b[:0xD8]))
    off,addr,size=hdr[0:18],hdr[18:36],hdr[36:54]
    def fo(a):
        for o,ad,s in zip(off,addr,size):
            if s and ad<=a<ad+s: return o+a-ad
        raise Exception(hex(a))
    for w in P['writes']:
        a=int(w['addr'],16); o=fo(a)
        assert b[o:o+4].hex().upper()==w['expect'], w
        b[o:o+4]=bytes.fromhex(w['value'])
    cave=bytes.fromhex(P['cave']); cave+=b'\0'*((-len(cave))%32)
    slot=next(i for i in range(2,7) if size[i]==0)
    pos=len(b)+((-len(b))%32); b+=b'\0'*(pos-len(b)); b+=cave
    off[slot]=pos; addr[slot]=int(P['cave_addr'],16); size[slot]=len(cave)
    b[:0xD8]=struct.pack('>18I18I18I',*off,*addr,*size)
    return bytes(b)
if __name__=='__main__':
    d=apply(open(sys.argv[1],'rb').read()); open(sys.argv[2],'wb').write(d); print('ok',len(d))
