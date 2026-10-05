// v50_b4a.cpp -- reproduce the B(iv) alignment claim in ITS parametrisation (s1=23,s2=18 fixed,
// KC rescaled by 2^(26-2w)), then widen the KC span 64x to test whether +-4096 was load-bearing.
#include "v50_core.h"
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables(); if(!v50_load_jwin())return 1;
    for(int pass=0;pass<2;pass++){
      long long span = pass? 262144 : 4096;
      printf("== s1=23 s2=18 fixed, KC' = KC*2^(26-2w), swept +-%lld ==\n",span);
      for(int w=9;w<=17;w++){
        long long nom = (w<=13)? ((long long)KC<<(26-2*w)) : ((long long)KC>>(2*w-26));
        for(int ord=0;ord<2;ord++){
          int best=-1; long long bk=0;
          #pragma omp parallel for schedule(dynamic,256)
          for(long long kc=nom-span; kc<=nom+span; kc++){
            if(kc<1) continue; int s=0;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w;
                i64 I1=((ord?arr(dx,kc):arr(kc,dx))+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
                if(j>=VJL[t+512]&&j<=VJH[t+512])s++; }
            #pragma omp critical
            { if(s>best){best=s;bk=kc;} } }
          i64 j17=0,j342=0,jm=0;
          for(int t:{17,342,-342}){ i64 dx=(i64)t<<w; i64 I1=((ord?arr(dx,bk):arr(bk,dx))+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
              if(t==17)j17=j; else if(t==342)j342=j; else jm=j; }
          printf("  w=%-2d ord=%d nom=%-12lld best %4d/1024 at KC=%-12lld J(17)=%-7lld J(342)=%-10lld J(-342)=%-10lld tbits=%d%s\n",
              w,ord,nom,best,bk,(long long)j17,(long long)j342,(long long)jm,(16-w>0?16-w:0),
              (w==13&&ord==0)?"  <- published":"");
        } }
      puts("");
    }
    printf("targets: J(17)=45617  J(342)=18466836  J(-342) in [18466858,18466867]\n");
    return 0; }
