exec(open('fipr_an3.py').read().split('print("AND array')[0])
def booth_prod3(ma,mb,L,mode):
    # Booth r4 digits of mb; PP_k = d_k * ma * 4^k. Truncation below column L.
    tot=0
    for k,d in enumerate(booth_digits(mb)):
        if d==0: continue
        mag=abs(d)*ma*(1<<(2*k))
        if d>0: tot+=(mag>>L)<<L
        else:
            if mode=='floor': tot+=((-mag)>>L)<<L
            elif mode=='hot':                       # one's complement of placed PP, truncated; hot one at column 2k survives iff 2k>=L
                oc=-mag-(1<<(2*k)); tot+=((oc>>L)<<L)+((1<<(2*k)) if 2*k>=L else 0)
            elif mode=='hotL':                      # hot one always injected but at column max(2k, L)?? (variant)
                oc=-mag-(1<<(2*k)); tot+=((oc>>L)<<L)+(1<<max(2*k,L))
    return tot
def kinterval(pf):
    lo=-10**18; hi=10**18; lo_case=hi_case=None
    for c in cases:
        (ma,mb,m,top,sh,qrz,qrn)=c; P0=pf(ma,mb); nl=qrz*(1<<sh)-P0; nh=(qrz+1)*(1<<sh)-P0
        if nl>lo: lo,lo_case=nl,c
        if nh<hi: hi,hi_case=nh,c
    return lo,hi
for op in ('a','b'):
    for L in (14,15,16,17,18):
        for mode in ('floor','hot','hotL'):
            pf=(lambda a,b,L=L,mode=mode: booth_prod3(b,a,L,mode)) if op=='a' else (lambda a,b,L=L,mode=mode: booth_prod3(a,b,L,mode))
            lo,hi=kinterval(pf)
            # count violations at best K in [lo,hi) or at lo
            K=lo if lo<hi else lo
            viol=0
            for (ma,mb,m,top,sh,qrz,qrn) in cases:
                P0=pf(ma,mb)
                if not (qrz*(1<<sh)-P0 <= K < (qrz+1)*(1<<sh)-P0): viol+=1
            print("booth on %s L=%2d %-5s: K in [%8d, %8d)  %s  viol@K=lo %d"%(op,L,mode,lo,hi,"FEASIBLE" if lo<hi else "infeasible",viol))
print("--- boundary cases for booth on a, L=16, floor, K=2^19 ---")
pf=lambda a,b: booth_prod3(b,a,16,'floor')
for (ma,mb,m,top,sh,qrz,qrn) in cases:
    P=pf(ma,mb)+(1<<19); q=P>>sh
    d=P-(q<<sh)
    if q!=qrz or d< (1<<12) or (1<<sh)-d < (1<<12):
        print("  ma=%06x mb=%06x sh=%d hwcarry %d modelcarry %d  (P+K) mod 2^sh = %d  digits(a)=%s"%(ma,mb,sh,qrz-(m>>sh),q-(m>>sh),d,booth_digits(ma)))
