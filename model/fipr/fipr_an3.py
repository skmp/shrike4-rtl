import collections, itertools
lines=[l.split() for l in open('kos_fipr_test/fipr_test.out')]
fipr=[[int(x,16) for x in l] for l in lines if len(l)==11 and all(len(x)==8 for x in l)]
single=[v for i,v in enumerate(fipr) if i%5==0]
def dec(u): return (-1 if u>>31 else 1), ((u>>23)&0xff), (u&0x7fffff)|0x800000
cases=[]
for v in single:
    sa,ea,ma=dec(v[0]); sb,eb,mb=dec(v[4]); m=ma*mb; top=m.bit_length()-1; sh=top-23
    eexp=ea+eb-127+top-46; _,erz,qrz=dec(v[10]); _,ern,qrn=dec(v[9])
    if erz==eexp+1: qrz=1<<24
    if ern==eexp+1: qrn=1<<24
    cases.append((ma,mb,m,top,sh,qrz,qrn))
def booth_digits(x,nd=13):
    ds=[]
    for i in range(nd):
        b_1=(x>>(2*i-1))&1 if i>0 else 0; b0=(x>>(2*i))&1; b1=(x>>(2*i+1))&1; ds.append(-2*b1+b0+b_1)
    return ds
def and_dropped(ma,mb,L):
    d=0
    for i in range(24):
        if (ma>>i)&1:
            d+= (mb & ((1<<max(L-i,0))-1))<<i if L-i>0 else 0
    return d
def booth_prod(ma,mb,L,mode):   # PPs = d_k * ma * 4^k, truncated per PP below column L
    tot=0
    for k,d in enumerate(booth_digits(mb)):
        pp=d*ma*(1<<(2*k))
        if L>0:
            if mode=='floor': pp=(pp>>L)<<L
            else: pp = (pp>>L)<<L if pp>=0 else -(((-pp)>>L)<<L)
        tot+=pp
    return tot
def score(prod_fn,Ks):
    best=(0,None)
    for K in Ks:
        ok=0
        for (ma,mb,m,top,sh,qrz,qrn) in cases:
            P=prod_fn(ma,mb)+K
            if (P>>sh)==qrz: ok+=1
        if ok>best[0]: best=(ok,K)
    return best
print("AND array, drop columns < L, + K:")
for L in (14,16,18,19,20,21,22):
    Ks=[0]+[1<<k for k in range(L-4,L+3)]+[3<<k for k in range(L-4,L+1)]+[(1<<L)*n//8 for n in range(1,16)]
    print("  L=%d:"%L, score(lambda a,b,L=L: a*b-and_dropped(a,b,L), Ks))
print("Booth r4 on FVn operand (b), per-PP truncation below L, + K:")
for L in (16,18,19,20,21,22):
    for mode in ('floor','mag'):
        Ks=[0]+[1<<k for k in range(L-3,L+4)]+[(1<<L)*n//8 for n in range(1,24)]
        print("  L=%d %s:"%(L,mode), score(lambda a,b,L=L,mode=mode: booth_prod(a,b,L,mode), Ks))
print("Booth r4 on FVm operand (a):")
for L in (18,19,20,21):
    for mode in ('floor','mag'):
        Ks=[0]+[1<<k for k in range(L-3,L+4)]+[(1<<L)*n//8 for n in range(1,24)]
        print("  L=%d %s:"%(L,mode), score(lambda a,b,L=L,mode=mode: booth_prod(b,a,L,mode), Ks))
print("reference: exact + 2^18 then trunc:", score(lambda a,b: a*b, [1<<18]))
