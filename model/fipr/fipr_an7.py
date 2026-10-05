import itertools, collections
lines=[l.split() for l in open('kos_fipr_test/fipr_test.out')]
fipr=[[int(x,16) for x in l] for l in lines if len(l)==11 and all(len(x)==8 for x in l)]
def dec(u): return (-1 if u>>31 else 1), ((u>>23)&0xff), (u&0x7fffff)|0x800000
def booth_digits(x,nd=13):
    ds=[]
    for i in range(nd):
        b_1=(x>>(2*i-1))&1 if i>0 else 0; b0=(x>>(2*i))&1; b1=(x>>(2*i+1))&1; ds.append(-2*b1+b0+b_1)
    return ds
def mult28(ma,mb,L=16,K=1<<19):   # Booth on ma (FVm operand), PPs floor-truncated below L, +K, truncate at bit 20 -> 28-bit p
    tot=0
    for k,d in enumerate(booth_digits(ma)):
        if d==0: continue
        pp=d*mb*(1<<(2*k)); tot+=(pp>>L)<<L
    return (tot+K)>>20
def fipr_model(a,b,mode,W,arule,final):
    prods=[]
    for i in range(4):
        if (a[i]&0x7fffffff)==0 or (b[i]&0x7fffffff)==0: continue
        sa,ea,ma=dec(a[i]); sb,eb,mb=dec(b[i]); p=mult28(ma,mb)
        if p==0: continue
        prods.append((sa*sb,ea+eb,p))     # value = sign * p * 2^(E-280), p in [2^26, 2^28)
    if not prods: return 0
    Emax=max(E for _,E,_ in prods)
    G=W-28                                # guard bits below p_max LSB
    S=0
    for s,E,p in prods:
        d=Emax-E; x=p<<G                  # aligned with G guard bits
        if d>0:
            if arule=='trunc': x=x>>d
            elif arule=='rn': x=(x+(1<<(d-1)))>>d
            elif arule=='jam': x=(x>>d)|(1 if (x&((1<<d)-1)) else 0)
            elif arule=='trunc_sticky': x=(x>>d)|(1 if (x&((1<<d)-1)) else 0)  # same as jam
        S+=s*x
    if S==0: return 0
    sign=-1 if S<0 else 1; S=abs(S)
    # value = S * 2^(Emax-280-G). normalize to 24 bits
    top=S.bit_length()-1; sh=top-23
    if sh>0:
        q=S>>sh; r=S&((1<<sh)-1); h=1<<(sh-1)
        if mode=='rn':
            if final=='rne_sticky': q+=(r>h) or (r==h and (q&1))
            elif final=='rne2': g=r>>(sh-2) if sh>=2 else r<<(2-sh); q+=(g>2) or (g==2 and (q&1))
            elif final=='rhu': q+=(r>=h)
    else: q=S<<(-sh)
    if q>>24: q>>=1; top+=1
    e=Emax-280-G+top+127-23 -23 +23   # biased exponent: value = q*2^-23 * 2^(top - ... ) -> derive: value = S*2^(Emax-280-G) = q*2^(sh)*2^(Emax-280-G); as 1.q: q/2^23 * 2^(23+sh+Emax-280-G)
    e=23+sh+Emax-280-G+127 if sh>0 else 23-(-sh)+Emax-280-G+127
    e=top+Emax-280-G+127
    return ((0x80000000 if sign<0 else 0)|((e&0xff)<<23)|(q&0x7fffff))
kinds={k:[v for i,v in enumerate(fipr) if i%5==k] for k in range(5)}
res=[]
for W in (28,29,30,31,32,34):
    for arule in ('trunc','rn','jam'):
        for final in ('rne_sticky','rne2','rhu'):
            sc={}
            for k in range(5):
                okn=sum(fipr_model(v[0:4],v[4:8],'rn',W,arule,final)==v[9] for v in kinds[k]); okz=sum(fipr_model(v[0:4],v[4:8],'rz',W,arule,final)==v[10] for v in kinds[k])
                sc[k]=(okn,okz)
            tot=sum(a+b for a,b in sc.values()); res.append((tot,W,arule,final,sc))
res.sort(key=lambda r:-r[0])
print("(total ok of 10000, W, align rule, final) per kind (RN ok, RZ ok):")
for r in res[:10]: print("  ",r[0],r[1],r[2],r[3],r[4])
