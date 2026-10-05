// wd_arule.cpp -- invert the SECOND square multiply to per-t allowed I1 sets, then find the best
// first-stage constant A exactly (scan) for each (s1,k1,k2).  s2 = 18, u2 = 32+k.
#include "wd_core.h"
static i64 JL[1024],JH[1024];
static i64 I1LO[1024],I1HI[1024]; static char I1OK[1024]; static unsigned long long I1MASK[1024][2];
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int s2=argc>1?atoi(argv[1]):18;
    Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
    Model M; M.c=c; build_model(M); fit_C0(M); build_thr(M);
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=JMAX;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,t,flip); i64 aa,bb; if(!jint(M,n,b-h,b-l,aa,bb)){gl=1;gh=0;break;}
            if(aa>gl)gl=aa; if(bb<gh)gh=bb; }
        JL[t+512]=gl; JH[t+512]=gh; }
    i64 k2s[]={0,1ll<<16,1ll<<17,1ll<<18,1ll<<19};
    i64 k1s[]={0,1ll<<17,1ll<<18,1ll<<19,1ll<<20,1ll<<21,1ll<<22};
    printf("s1  k1        k2        bestA        (hex)     score/1024   A/KC-1\n");
    int gbest=-1; i64 gA=0; int gs1=0; i64 gk1=0,gk2=0;
    for(i64 k2 : k2s){
        // allowed I1 set per t (scan a window around the ideal I1 = KC*t/1024)
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 c0=(i64)KC*t/1024;
            I1OK[t+512]=0; I1MASK[t+512][0]=I1MASK[t+512][1]=0; I1LO[t+512]=1;I1HI[t+512]=0;
            if(JL[t+512]>JH[t+512]) continue;
            for(int d=-64;d<64;d++){ i64 I1=c0+d; i64 J=(arr(I1,dx)+k2)>>s2;
                if(J>=JL[t+512]&&J<=JH[t+512]){ I1OK[t+512]=1; if(I1LO[t+512]>I1HI[t+512]){I1LO[t+512]=I1;} I1HI[t+512]=I1;
                    int b=d+64; I1MASK[t+512][b>>6] |= 1ull<<(b&63); } } }
        int nok=0; for(int t=0;t<1024;t++) if(I1OK[t])nok++;
        for(int s1=21;s1<=24;s1++) for(i64 k1 : k1s){
            long double ctr=(long double)KC*(long double)(1ll<<s1)/8388608.0L; i64 A0=(i64)(ctr+0.5L);
            int lb=-1; i64 lA=0;
#pragma omp parallel for schedule(dynamic)
            for(int dA=-6000;dA<=6000;dA++){ i64 A=A0+dA; if(A<=0||A>=(1ll<<24))continue; int sc=0;
                for(int t=-512;t<=511;t++){ if(!I1OK[t+512])continue; i64 dx=(i64)t<<13;
                    i64 I1=(arr(A,dx)+k1)>>s1; i64 c0=(i64)KC*t/1024; i64 d=I1-c0;
                    if(d<-64||d>=64)continue; int b=(int)d+64; if(I1MASK[t+512][b>>6]>>(b&63)&1) sc++; }
#pragma omp critical
                { if(sc>lb){lb=sc;lA=A;} } }
            if(lb>gbest){gbest=lb;gA=lA;gs1=s1;gk1=k1;gk2=k2;}
            if(lb>=930) printf("%2d  %-9lld %-9lld %-11lld 0x%-8llx %4d/%d   %+.3Le\n",s1,(long long)k1,(long long)k2,
                (long long)lA,(long long)lA,lb,nok,(long double)lA/((long double)KC*(1ll<<s1)/8388608.0L)-1.0L);
        } }
    printf("GLOBAL BEST: s1=%d A=%lld (0x%llx) k1=%lld k2=%lld -> %d/1024\n",gs1,(long long)gA,(long long)gA,(long long)gk1,(long long)gk2,gbest);
    return 0; }
