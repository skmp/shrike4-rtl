import collections
exec(open('fipr_an1.py').read().split("carry=[c for c")[0])   # reuse parsing -> cases
def stage(m,c,t): return (((m+(1<<c)) if c>=0 else m)>>t)<<t
res=[]
for t in range(14,26):
    for c in [-1]+list(range(12,t)):
        ok=sum(((stage(m,c,t)>>sh)==qrz) for (m,top,sh,q_ex,rem,qrz,qrn) in cases)
        res.append((ok,t,c))
res.sort(reverse=True); print("RZ carry consistency: (ok of 1000, truncate at t, add 2^c):"); [print("  ",r) for r in res[:8]]
best=res[0]; t,c=best[1],best[2]
print("inconsistent cases for t=%d c=%d:"%(t,c))
for (m,top,sh,q_ex,rem,qrz,qrn) in cases:
    q=stage(m,c,t)>>sh
    if q!=qrz: print("   top %d sh %d rem=%0*x  hw carry %d model carry %d   rem bits(sh-1..0)=%s"%(top,sh,(sh+3)//4,rem,qrz-q_ex,q-q_ex,format(rem,'0%db'%sh)))
# relative-to-top variants: add at top-c, truncate at top-t
res=[]
for tk in range(24,32):
    for ck in range(tk+1,tk+8):
        ok=sum(((stage(m,top-ck,top-tk)>>sh)==qrz) for (m,top,sh,q_ex,rem,qrz,qrn) in cases); res.append((ok,tk,ck))
res.sort(reverse=True); print("relative: (ok, keep top..top-tk, add at top-ck):",res[:5])
