import itertools
exec(open('fipr_an3.py').read().split('print("AND array')[0])
res=[]
for L in (13,14,15,16,17):
    for mode in ('floor','mag'):
        for op in ('b','a'):
            pf=(lambda a,b,L=L,mode=mode: booth_prod(a,b,L,mode)) if op=='b' else (lambda a,b,L=L,mode=mode: booth_prod(b,a,L,mode))
            prods=[(pf(ma,mb),sh,qrz,qrn,m,ma,mb) for (ma,mb,m,top,sh,qrz,qrn) in cases]
            for K in range(0,1<<21,1<<12):
                ok=sum(((P+K)>>sh)==qrz for (P,sh,qrz,qrn,m,ma,mb) in prods)
                res.append((ok,L,mode,op,K))
res.sort(reverse=True); print("refined RZ search (ok/1000, L, mode, booth operand, K):"); [print("  ",r) for r in res[:10]]
# RN check for top candidates: final RNE from stage value truncated to 28 bits (bits 47..20) with/without sticky, and RN plain
def final_rn(P,sh,rule):
    P28=(P>>20)<<20
    q=P28>>sh; r=P28&((1<<sh)-1); h=1<<(sh-1)
    if rule=='rne_nosticky_2': g=r>>(sh-2); return q+((g>2) or (g==2 and (q&1)))
    if rule=='rne_sticky': return q+((r>h) or (r==h and (q&1)))
    if rule=='rhu': return q+(r>=h)
    if rule=='rne_full':  # from untruncated P
        q=P>>sh; r=P&((1<<sh)-1); return q+((r>h) or (r==h and (q&1)))
seen=set()
for (ok,L,mode,op,K) in res[:12]:
    key=(L,mode,op,K)
    if key in seen: continue
    seen.add(key)
    pf=(lambda a,b: booth_prod(a,b,L,mode)) if op=='b' else (lambda a,b: booth_prod(b,a,L,mode))
    out=[]
    for rule in ('rne_nosticky_2','rne_sticky','rhu','rne_full'):
        okrn=sum(final_rn(pf(ma,mb)+K,sh,rule)==qrn for (ma,mb,m,top,sh,qrz,qrn) in cases); out.append((rule,okrn))
    print("  RN for",key,"RZ ok",ok,":",out)
    if len(seen)>=4: break
# residual RZ exceptions for the best
ok,L,mode,op,K=res[0]
pf=(lambda a,b: booth_prod(a,b,L,mode)) if op=='b' else (lambda a,b: booth_prod(b,a,L,mode))
print("exceptions for best:")
for (ma,mb,m,top,sh,qrz,qrn) in cases:
    P=pf(ma,mb)+K
    if (P>>sh)!=qrz: print("   ma=%06x mb=%06x m=%012x sh=%d hw carry %d model %d  digits(b)=%s"%(ma,mb,m,sh,qrz-(m>>sh),(P>>sh)-(m>>sh),booth_digits(mb if op=='b' else ma)))
