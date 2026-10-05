import numpy as np
from harness import window, rne2
np.set_printoptions(linewidth=250, suppress=True)
def constraints(h,n,C0,C1,C2,C3,E1=0):
    y,tp=window(h,n); sel=tp>=0; yp=y[sel]; t=tp[sel]
    lin=(((-C1)*t)>>(20+E1)); t3=(t*t*t)>>8
    Q=4*C2*t*t; cubv=4*C3*t3
    W=C0-lin+(Q>>38)-(cubv>>48); ys=rne2(W); d=ys-yp
    fq=(Q&((1<<38)-1))/2.0**38; fc=(cubv&((1<<48)-1))/2.0**48
    sens_dn=(rne2(W-1)!=rne2(W))
    m=(fq<0.03)&sens_dn&(fc>0.05)&(fc<0.95)
    A=np.where(m&(d==1))[0]; B=np.where(m&(d!=1))[0]     # A: deficit>fq ; B: deficit<=fq
    return t[A],fq[A],t[B],fq[B],C2
sets=[('node0',)+constraints(0,0,67108864,-4194304,24592,640), ('node1',)+constraints(0,1,65105160,-3829715,21132,520)]
def score(fn):
    tot=0; det=[]
    for name,tA,fA,tB,fB,C2 in sets:
        DA=4*C2*(tA*tA-fn(tA))/2.0**38; DB=4*C2*(tB*tB-fn(tB))/2.0**38
        v=int(np.sum(DA<fA))+int(np.sum(DB>fB)); tot+=v; det.append(v)
    return tot,det
print("sizes:",[(s[0],len(s[1]),len(s[3])) for s in sets])
res=[]
for s in (14,15,16):
    for K in range(0,1<<s,16):
        res.append((score(lambda a,s=s,K=K:((a*a+K)>>s)<<s)[0],'addK_trunc',s,K))
res.sort(); print("addK:",res[:10])
res=[]
for s in (14,15,16):
    for K in range(0,1<<s,16):
        res.append((score(lambda a,s=s,K=K:(((a*a)>>s)<<s)+K)[0],'trunc_plusK',s,K))
res.sort(); print("trunc+K:",res[:10])
res=[]
for c in range(-64,65,1):
    for s in (14,15,16):
        res.append((score(lambda a,s=s,c=c:((a*(a+c))>>s)<<s)[0],'a(a+c)',s,c))
res.sort(); print("a(a+c):",res[:10])
res=[]
for p in range(0,8):
    for q in range(0,8):
        for s in (12,13,14,15,16):
            res.append((score(lambda a,p=p,q=q,s=s:((((a>>p)<<p)*((a>>q)<<q))>>s)<<s)[0],'trunc_ops',p,q,s))
res.sort(); print("trunc ops:",res[:10])
print("baseline tr15:",score(lambda a:((a*a)>>15)<<15), " exact:",score(lambda a:a*a))
