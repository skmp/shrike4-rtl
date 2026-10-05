import numpy as np, sys, itertools
m=np.load('m.npy').astype(np.int64); H=1<<23; S=1<<19; Q=1<<18
NF=np.load('nodefit.npy')

def rne2(W):
    y=W>>2; r=W&3
    return y+((r>2)|((r==2)&((y&1)==1)))

def window(h,n):
    center=h*H+n*S; lo=max(center-Q,h*H); hi=min(center+Q,(h+1)*H)
    return m[lo:hi], np.arange(lo-center,hi-center,dtype=np.int64)

# model variants: return W in 2^-26 units. C=(C0,C1,C2,C3) ints; grids: C0*1 (units), C1: c1*16, C2: c2*2, C3: c3*2
def model(v,tp,C):
    C0,C1,C2,C3=C
    if v=='floor':      # two's complement t', floor division (toward -inf)
        return C0 + ((C1*tp)>>20) + ((C2*tp*tp)>>35) + ((C3*tp*tp*tp)>>53)
    if v=='trunc0':     # truncate toward zero
        t1=C1*tp; t2=C2*tp*tp; t3=C3*tp*tp*tp
        f=lambda x,s: np.where(x>=0, x>>s, -((-x)>>s))
        return C0 + f(t1,20)+f(t2,35)+f(t3,53)
    if v=='ones':       # one's complement magnitude for negative t', sign applied after truncation of odd terms
        neg=tp<0; a=np.where(neg, ~tp & (Q-1), tp)  # for tp=-Q, ~tp&(Q-1) = Q-1
        T1=(C1*a)>>20; T2=(C2*a*a)>>35; T3=(C3*a*a*a)>>53   # C1,C3 negative -> floors
        return C0 + np.where(neg,-T1,T1) + T2 + np.where(neg,-T3,T3)
    if v=='ones_m1':    # like ones, but negated odd sum gets -1 unit (one's complement negate)
        neg=tp<0; a=np.where(neg, ~tp & (Q-1), tp)
        T1=(C1*a)>>20; T2=(C2*a*a)>>35; T3=(C3*a*a*a)>>53
        odd=T1+T3
        return C0 + np.where(neg,-odd-1,odd) + T2
    if v=='ones_abs':   # magnitude products of |C1|,|C3| truncated then subtracted
        neg=tp<0; a=np.where(neg, ~tp & (Q-1), tp)
        T1=((-C1)*a)>>20; T3=((-C3)*a*a*a)>>53; T2=(C2*a*a)>>35
        return C0 + np.where(neg,T1,-T1) + T2 + np.where(neg,T3,-T3)
    raise ValueError(v)

def mism(v,y,tp,C): return int(np.count_nonzero(rne2(model(v,tp,C))!=y))

def search(v,h,n,init,rad=(6,8,3,3),maxit=8):
    y,tp=window(h,n); C=list(init); best=mism(v,y,tp,C)
    for it in range(maxit):
        improved=False
        for k in range(4):
            for d in range(1,rad[k]+1):
                for sgn in (1,-1):
                    C2=list(C); C2[k]+=sgn*d; c=mism(v,y,tp,C2)
                    if c<best: best,C,improved=c,C2,True
        if not improved: break
    return best,C

def init_from_fit(i):
    c0,c1,c2,c3=NF[i]
    return [int(round((c0-0.125)*4)), int(round(c1*16)), int(round(c2*2)), int(round(c3*2))]

if __name__=="__main__":
    v=sys.argv[1]; nodes=[(h,n) for h in (0,1) for n in range(17)]
    sel=[int(a) for a in sys.argv[2:]] if len(sys.argv)>2 else range(len(nodes))
    tot=0
    for i in sel:
        h,n=nodes[i]; best,C=search(v,h,n,init_from_fit(i)); tot+=best
        print("variant %s half %d node %2d: mism %7d  C=%s"%(v,h,n,best,C), flush=True)
    print("total",tot)
