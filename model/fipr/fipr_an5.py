exec(open('fipr_an3.py').read().split('print("AND array')[0])
def booth_prod2(ma,mb,L,hot):
    # Booth r4 on mb; negative PP = ~(|d|*ma) truncated below L, plus hot-one at column 2k (kept only if 2k>=L when hot=='drop')
    tot=0
    for k,d in enumerate(booth_digits(mb)):
        if d==0: continue
        mag=abs(d)*ma*(1<<(2*k))
        if d>0: tot+=(mag>>L)<<L
        else:
            comp=((~mag)>>L)<<L           # one's complement truncated (floor of -mag-1)
            one=1<<(2*k)
            if hot=='keep' or (hot=='drop' and 2*k>=L): comp+=one
            elif hot=='floor': comp=((-mag)>>L)<<L
            tot+=comp
    return tot
def kinterval(pf):
    lo=-10**18; hi=10**18; conf=[]
    for (ma,mb,m,top,sh,qrz,qrn) in cases:
        P0=pf(ma,mb); need_lo=qrz*(1<<sh)-P0; need_hi=(qrz+1)*(1<<sh)-P0   # K in [need_lo, need_hi)
        lo=max(lo,need_lo); hi=min(hi,need_hi)
    # count violations for K = best guess (median of feasible?) -> use K=clamp
    K=lo if lo<hi else (lo+hi)//2
    viol=sum(not (qrz*(1<<sh)-pf(ma,mb) <= K < (qrz+1)*(1<<sh)-pf(ma,mb)) for (ma,mb,m,top,sh,qrz,qrn) in cases)
    return lo,hi,viol
for op in ('a','b'):
    for L in (14,15,16,17,18):
        for hot in ('floor','keep','drop'):
            pf=(lambda a,b,L=L,hot=hot: booth_prod2(b,a,L,hot)) if op=='a' else (lambda a,b,L=L,hot=hot: booth_prod2(a,b,L,hot))
            lo,hi,viol=kinterval(pf)
            print("booth on %s L=%d hot=%-5s : K interval [%d, %d) width %d  -> %s"%(op,L,hot,lo,hi,hi-lo,"FEASIBLE" if lo<hi else "infeasible (viol at K=lo: %d)"%viol))
