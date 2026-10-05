import numpy as np, itertools, time
np.set_printoptions(linewidth=230)
V=np.load('V26.npy'); S=512; b=np.arange(S,dtype=np.int64)
def magb(P,sh,beta):
    B=int(round(beta*(1<<sh))); return np.where(P>=0,(P+B)>>sh,-(((-P)+B)>>sh))
def evalW(C,a,s,g,betas):
    C0,C1,C2,C3=C; g1,g2,g3=g; b1,b2,b3=betas
    return C0+s*magb(C1*a,9+g1,b1)+magb(C2*a*a,18+g2,b2)+s*magb(C3*a*a*a,27+g3,b3)
g=(1,1,1); ones=np.ones(S,dtype=np.int64)
# (1) seg 0: which b3 give 0 with b1 = 5/128
C=(0,6588397,-2,-2646); y=V[:S]
ok=[round(b3,4) for b3 in np.arange(0,0.2,0.0025) if np.count_nonzero(evalW(C,b,ones,g,(5/128,0,b3))!=y)==0]
print("seg0 zero-mismatch b3 range with b1=5/128:",ok[:3],"...",ok[-3:],"n=",len(ok))
ok1=[round(b1,4) for b1 in np.arange(0,0.2,0.0025) if np.count_nonzero(evalW(C,b,ones,g,(b1,0,5/128))!=y)==0]
print("seg0 zero-mismatch b1 range with b3=5/128:",ok1[:2],"...",ok1[-2:],"n=",len(ok1))
# (3) seg 2 leftovers with biases 5/128 all
C=(6577819,6556672,-15847,-2633); y=V[2*S:3*S]; a=b; s=ones
W=evalW(C,a,s,g,(5/128,5/128,5/128)); d=y-W; bad=np.where(d!=0)[0]
P1=C[1]*a; P2=-C[2]*a*a; P3=-C[3]*a*a*a
f1=(P1&1023)/1024.0; f2=(P2&((1<<19)-1))/2.0**19; f3=(P3&((1<<28)-1))/2.0**28
print("seg2 leftovers (bias 5/128 on all): b, y-W, f1, f2, f3, |T2|, |T3|")
for i in bad: print("  %3d %+d f1=%.3f f2=%.3f f3=%.3f |T2|=%.2f |T3|=%.2f"%(a[i],d[i],f1[i],f2[i],f3[i],P2[i]/2.0**19,P3[i]/2.0**28))
# (2) joint scan seg 2: d1,d2,d3 in +-3, biases in {0, 5/128, 1/16, 1/8}
t0=time.time(); best=(10**9,None)
for d1,d2,d3 in itertools.product(range(-3,4),range(-4,5),range(-3,4)):
    for bb in itertools.product((0,5/128,1/16,1/8),repeat=3):
        v=int(np.count_nonzero(evalW((C[0],C[1]+d1,C[2]+d2,C[3]+d3),a,s,g,bb)!=y))
        if v<best[0]: best=(v,bb,(C[0],C[1]+d1,C[2]+d2,C[3]+d3))
print("seg2 joint best:",best,"%.0fs"%(time.time()-t0))
