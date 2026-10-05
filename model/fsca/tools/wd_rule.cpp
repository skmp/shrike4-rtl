// wd_rule.cpp -- with the finer square latch (s2 given, u2 = 50-s2+k) every t has a non-empty
// allowed-latch window.  Search the chain rule  I1=(arr(A,dx)+k1)>>s1 ; J=(arr(I1,dx)+k2)>>s2
// against those windows.  Score = number of t (of 1024) whose window contains J(t).
#include "wd_core.h"
static i64 JL[1024],JH[1024]; static int NFE=0;
int main(int argc,char**argv){ if(!load_fsca(FF))return 1;
    int s2=argc>1?atoi(argv[1]):18;
    Cfg c; c.i2K=3; c.i2Kv=1ll<<19; c.s2v=s2; c.u2base=50-s2;
    Model M; M.c=c; build_model(M); fit_C0(M); build_thr(M);
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=JMAX; bool ok=true;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!M.S[n])continue;
            int a=(node*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
            i64 b=base_of(M,n,t,flip); i64 aa,bb; if(!jint(M,n,b-h,b-l,aa,bb)){ok=false;break;}
            if(aa>gl)gl=aa; if(bb<gh)gh=bb; }
        if(!ok||gl>gh){ JL[t+512]=1;JH[t+512]=0; NFE++; } else {JL[t+512]=gl;JH[t+512]=gh;} }
    printf("s2=%d  infeasible t: %d\n",s2,NFE);
    { FILE*f=fopen(argc>2?argv[2]:"wd_jwin18.txt","w");
      for(int t=-512;t<=511;t++) fprintf(f,"%d %lld %lld %d\n",t,(long long)JL[t+512],(long long)JH[t+512],JL[t+512]<=JH[t+512]);
      fclose(f); }
    // width histogram
    { int h[8]={0}; for(int t=0;t<1024;t++){ i64 w=JH[t]-JL[t]+1; if(w<1)continue; int b=0; while(w>>b) b++; if(b>7)b=7; h[b]++; }
      printf("window width histogram (log2 bins):"); for(int i=0;i<8;i++)printf(" %d",h[i]); printf("\n"); }
    // chain search
    int best=-1; i64 bA=0; int bs1=0; i64 bk1=0,bk2=0;
    i64 k1s[]={0,1ll<<18,1ll<<19,1ll<<20,1ll<<21,1ll<<22};
    i64 k2s[]={0,1ll<<16,1ll<<17,1ll<<18,1ll<<19,1ll<<20};
    for(int s1=21;s1<=25;s1++){
      long double ctr = (long double)KC*(long double)(1ll<<s1)/(long double)(1ll<<23);
      i64 A0=(i64)ctr;
      for(i64 k1 : k1s) for(i64 k2 : k2s){
        int lb=-1; i64 lA=0;
#pragma omp parallel for schedule(dynamic)
        for(int dA=-3000;dA<=3000;dA++){ i64 A=A0+dA; if(A<=0||A>=(1ll<<24))continue;
            int sc=0; for(int t=-512;t<=511;t++){ if(JL[t+512]>JH[t+512])continue; i64 dx=(i64)t<<13;
                i64 I1=(arr(A,dx)+k1)>>s1; i64 J=(arr(I1,dx)+k2)>>s2;
                if(J>=JL[t+512]&&J<=JH[t+512])sc++; }
#pragma omp critical
            { if(sc>lb){lb=sc;lA=A;} }
        }
        if(lb>best){best=lb;bA=lA;bs1=s1;bk1=k1;bk2=k2;}
        if(lb>=900) printf("  s1=%2d k1=%8lld k2=%8lld -> best %4d at A=%lld (0x%llx)\n",s1,(long long)k1,(long long)k2,lb,(long long)lA,(long long)lA);
      } }
    printf("BEST: s1=%d A=%lld (0x%llx) k1=%lld k2=%lld -> %d / %d\n",bs1,(long long)bA,(long long)bA,(long long)bk1,(long long)bk2,best,1024-NFE);
    return 0; }
