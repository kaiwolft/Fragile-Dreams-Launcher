import sys,subprocess,re
B=0x80006820
s,e=int(sys.argv[1],16),int(sys.argv[2],16)
out=subprocess.run(['llvm-objdump','-d','--triple=powerpc-unknown-unknown',f'--start-address={s-B}',f'--stop-address={e-B}',sys.argv[3] if len(sys.argv)>3 else '/tmp/claude-0/-home-claude/87eb4c35-e22a-5d81-93f6-92d35114d72d/scratchpad/text1.elf'],capture_output=True,text=True).stdout
for l in out.splitlines()[6:]:
    m=re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} ){4})\s*(.*)',l)
    if not m: continue
    ins=re.sub(r'0x([0-9a-f]+) <[^>]*>',lambda k:hex(int(k.group(1),16)+B),m.group(3))
    print(hex(int(m.group(1),16)+B), m.group(2).replace(' ',''), ins)
