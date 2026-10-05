import collections
lines=[l.split() for l in open('kos_fipr_test/fipr_test.out')]
fipr=[[int(x,16) for x in l] for l in lines if len(l)==11 and all(len(x)==8 for x in l)]
single=[v for i,v in enumerate(fipr) if i%5==0]
def dec(u): return (-1 if u>>31 else 1), ((u>>23)&0xff), (u&0x7fffff)|0x800000
cases=[]
for v in single:
    sa,ea,ma=dec(v[0]); sb,eb,mb=dec(v[4]); m=ma*mb; top=m.bit_length()-1; sh=top-23
    q_ex=m>>sh; rem=m&((1<<sh)-1)
    _,erz,qrz=dec(v[10]); _,ern,qrn=dec(v[9])
    # handle carry overflow to next exponent: if qrz==0x800000 and erz==expected+1 -> q=2^24
    eexp=ea+eb-127+top-46
    if erz==eexp+1: qrz=1<<24
    if ern==eexp+1: qrn=1<<24
    cases.append((m,top,sh,q_ex,rem,qrz,qrn))
carry=[c for c in cases if c[5]!=c[3]]; print("singles:",len(cases),"RZ carry cases:",len(carry),"RZ q_hw - q_exact values:",collections.Counter(c[5]-c[3] for c in cases))
# For each candidate first-stage rounding position p (absolute bit in 48-bit product) and rule, predict carry into kept bits
def first_stage(m,p,rule):
    if p<=0: return m
    if rule=='rn': return ((m+(1<<(p-1)))>>p)<<p
    if rule=='rne':
        q=m>>p; r=m&((1<<p)-1); h=1<<(p-1); q+=(r>h) or (r==h and (q&1)); return q<<p
    if rule=='trunc': return (m>>p)<<p
    if rule=='jam': return ((m>>p)<<p)|(1<<(p-1))
res=[]
for mode in ('abs','rel'):
    for k in range(12,30):
        for rule in ('rn','rne','trunc','jam'):
            ok=0
            for (m,top,sh,q_ex,rem,qrz,qrn) in cases:
                p = k if mode=='abs' else top-k   # rel: keep k+1 significant bits
                mm=first_stage(m,p,rule); q=mm>>sh
                ok+=(q==qrz)
            res.append((ok,mode,k,rule))
res.sort(reverse=True); print("RZ-consistency of first-stage rounding (of 1000):"); [print("  ",r) for r in res[:10]]
# derive per-case constraints on p for RN rule (abs): carry iff bits sh-1..p all 1 and bit p-1 = 1
print("carry cases: (top, sh, run of ones below sh, bit after run):")
for (m,top,sh,q_ex,rem,qrz,qrn) in carry[:20]:
    L=0
    while L<sh and (rem>>(sh-1-L))&1: L+=1
    print("   top %d sh %d rem=%0*x run=%d"%(top,sh,(sh+3)//4,rem,L))
