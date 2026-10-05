import numpy as np, itertools
V=np.load('V26.npy'); S=512; b=np.arange(S,dtype=np.int64)
# fine coefficients (1/16 grids): C1 in 1/(16*1024) per step, C2 in 1/16, C3 in 1/16  (from segfit: C1/step*2 -> per step)
segs={0:(0, 6433.9814, -1.0, -1323.0, b, 1), 2:(6577820, 6402.9992, -7923.4375, -1316.5, b, 1), 1:(6577819, 6402.998, -7923.4375, -1316.5, 512-b, -1), 4:(13092290, 6310.3544, -15770.5, -1297.5, b, 1)}
def mg(P,sh): return np.where(P>=0,P>>sh,-((-P)>>sh))
for seg,(C0,c1,c2,c3,a,s) in segs.items():
    y=V[seg*S:(seg+1)*S]
    C1=int(round(c1*16*1024)); C2=int(round(c2*16)); C3=int(round(c3*16))
    out=[]
    for k in range(0,9):                     # guard bits kept on each product before the single final truncation
        # product k-guard: P1 = C1*a / 2^(14) units -> keep k bits: >> (14-k) ; P2 = C2*a^2/2^22 ; P3 = C3*a^3/2^31
        P1=s*mg(C1*a,14-k); P2=mg(C2*a*a,22-k); P3=s*mg(C3*a*a*a,31-k)
        best=(10**9,None)
        for c0f in range(0,1<<k):            # fractional part of C0 in 2^-k units
            for dC0 in (-1,0,1):
                W=(((C0+dC0)<<k)+c0f+P1+P2+P3)>>k
                v=int(np.count_nonzero(W!=y))
                if v<best[0]: best=(v,(dC0,c0f))
        out.append((k,best[0],best[1]))
    print("seg",seg,":",out)
