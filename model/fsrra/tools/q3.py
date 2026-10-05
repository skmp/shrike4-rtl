import numpy as np, itertools
from harness import window, rne2
np.set_printoptions(linewidth=250, suppress=True)
y,tp=window(0,0); sel=tp>=0; yp=y[sel]; t=tp[sel]
C0,C1,C2x,C3x=(1<<26),-4194304,4*24592,4*640
lin=(C1*t)<<18; t3=(t*t*t)>>8
Wq=(lin+C2x*t*t); cubv=(C3x*t3)
W=C0+(Wq>>38)-(cubv>>48); ys=rne2(W); d=ys-yp
fq=(Wq&((1<<38)-1))/2.0**38; fc=(cubv&((1<<48)-1))/2.0**48
sens_dn=(rne2(W-1)!=rne2(W)); sens_up=(rne2(W+1)!=rne2(W))
mq=(fq<0.03)&sens_dn&(fc>0.05)&(fc<0.95); vq=(d==1)
mc=(fc<0.05)&sens_up&(fq>0.05)&(fq<0.95); vc=(d==-1)
print("quad test points",mq.sum(),"cube test points",mc.sum())
def score_q(D):  # D: deficit in units (array) ; inconsistency count
    return int(np.sum(mq&vq&(D<=fq)))+int(np.sum(mq&~vq&(D>fq)))
def score_c(D):
    return int(np.sum(mc&vc&(D<=fc)))+int(np.sum(mc&~vc&(D>fc)))
tt=t.astype(np.float64)
res=[]
for s in range(8,22):
    D=C2x*((t*t)&((1<<s)-1))/2.0**38; res.append((score_q(D),'sq_tr',s))
    Dr=C2x*(((t*t)+(1<<(s-1)))&((1<<s)-1))/2.0**38 - C2x*(1<<(s-1))/2.0**38; res.append((score_q(Dr),'sq_rn',s))
for k in range(4,12):
    D=((C2x*t*t)&((1<<(38-k))-1))/2.0**38; res.append((score_q(D),'prod_tr',k))
for s in range(8,20):
    for k in range(4,12):
        D=C2x*((t*t)&((1<<s)-1))/2.0**38 + ((C2x*((t*t)>>s)<<s)&((1<<(38-k))-1))/2.0**38; res.append((score_q(D),'sq_tr+prod_tr',s,k))
for a in range(0,6):
    for b in range(0,6):
        D=C2x*(t*t-((t>>a)<<a)*((t>>b)<<b))/2.0**38; res.append((score_q(D),'tab',a,b))
res.append((score_q(np.zeros(len(t))),'none'))
res.sort(key=lambda r:r[0]); print("QUAD best:",res[:12])
res=[]
for s in range(10,40):
    D=C3x*((t*t*t)&((1<<s)-1))/2.0**56; res.append((score_c(D),'cube_tr',s))
for s in range(8,30):
    sq=(t*t)>>s; D=C3x*(t*t*t-(sq*t)<<s)/2.0**56; res.append((score_c(D),'sq>>s *t',s))
for r in range(0,8):
    tr=(t>>r)<<r; D=C3x*(t*t*t-tr*tr*tr)/2.0**56; res.append((score_c(D),'(t>>r)^3',r))
    for s in range(8,26):
        sq=(t*t)>>s; D=C3x*(t*t*t-((sq*tr)<<s))/2.0**56; res.append((score_c(D),'(sq>>s)*(t>>r)',s,r))
        sq2=(tr*tr)>>s; D=C3x*(t*t*t-((sq2*tr)<<s))/2.0**56; res.append((score_c(D),'((t>>r)^2>>s)*(t>>r)',s,r))
for N in range(10,20):   # normalized N-bit cube
    a=(t*t*t); 
    bl=np.zeros(len(a),dtype=np.int64)
    for b in range(54,-1,-1): bl=np.where((bl==0)&((a>>b)!=0),b+1,bl)
    sh=np.maximum(bl-N,0); ct=(a>>sh)<<sh; D=C3x*(a-ct)/2.0**56; res.append((score_c(D),'cube_normN',N))
res.append((score_c(np.zeros(len(t))),'none'))
res.sort(key=lambda r:r[0]); print("CUBE best:",res[:12])
