import numpy as np, time
np.set_printoptions(linewidth=230)
V=np.load('V26.npy'); S=512
def tr(P,sh): return np.where(P>=0,P>>sh,-((-P)>>sh))
def fitseg(lo,a,s,g,r1=40,quiet=False,label=""):
    # y = V[lo:lo+512]; a = magnitude offset array (>=0), s = sign array (+1/-1) for odd terms
    y=V[lo:lo+S]; g1,g2,g3=g
    u=(s*a)/512.0; A=np.stack([u**k for k in range(4)],1); c,_,_,_=np.linalg.lstsq(A,y.astype(float),rcond=None)
    C1c=int(round(c[1]*2**g1)); C2c=int(round(c[2]*2**g2)); C3c=int(round(c[3]*2**g3))
    best=(10**9,None)
    T2s={d2:tr((C2c+d2)*a*a,18+g2) for d2 in range(-12,13)}
    T3s={d3:s*tr((C3c+d3)*a*a*a,27+g3) for d3 in range(-12,13)}
    for d1 in range(-r1,r1+1):
        T1=s*tr((C1c+d1)*a,9+g1)
        for d2,T2 in T2s.items():
            base=T1+T2
            for d3,T3 in T3s.items():
                diff=y-(base+T3); vals,cnts=np.unique(diff,return_counts=True); kk=cnts.argmax(); v=S-cnts[kk]
                if v<best[0]: best=(v,(int(vals[kk]),C1c+d1,C2c+d2,C3c+d3))
    if not quiet: print(label,"grid",g,"mism",best[0],"C",best[1],flush=True)
    return best
b=np.arange(S,dtype=np.int64)
print("== even segments, start variable, finer C1 grids ==")
for seg in (0,2,4):
    for g1 in (1,2,4,6,8):
        fitseg(seg*S,b,np.ones(S,dtype=np.int64),(g1,1,1),r1=40*(1 if g1<=2 else 4),label="seg %d"%seg)
print("== odd segments, mirrored variable a=512-b (node at next multiple of 1024), sign -1 ==")
for seg in (1,3):
    a=512-b; s=-np.ones(S,dtype=np.int64)
    for g1 in (1,2,4,6):
        fitseg(seg*S,a,s,(g1,1,1),r1=40*(1 if g1<=2 else 4),label="seg %d"%seg)
print("V[1024]=%d sin*2^26=%.2f ; V[512]=%d sin=%.2f"%(V[1024],np.sin(1024*2*np.pi/65536)*2**26,V[512],np.sin(512*2*np.pi/65536)*2**26))
