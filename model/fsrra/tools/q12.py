import numpy as np
from harness import window, rne2
y,tp=window(0,0); sel=tp>=0; yp=y[sel]; t=tp[sel]
C0,C1,C2,C3=67108864,-4194304,24592,640
lin=(4*t); t3=(t*t*t)>>8; cub=(4*C3*t3)>>48
def cnt(sq):
    W=C0-lin+((4*C2*sq)>>38)-cub
    return int(np.count_nonzero(rne2(W)!=yp))
sq0=t*t; lo=sq0&0x7fff; tr=sq0-lo
print("tr15:",cnt(tr)," exact:",cnt(sq0))
best=[]
for thr in range(31000,32768,32):
    for K in range(2600,2900,10):
        sq=tr+np.where(lo>=thr,32768,K); best.append((cnt(sq),thr,K))
best.sort(); print(best[:10])
# also: no threshold, just +K ; and threshold only
print("K only 2730:",cnt(tr+2730)," thr only 31488:",cnt(tr+np.where(lo>=31488,32768,0)))
