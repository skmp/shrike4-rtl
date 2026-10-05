// v40_e.cpp -- (1) focused self-test of the v40_d solver at the baseline config
//              (2) the negative-K2 landmark hits, RE-SCORED over all 1024 offsets.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JLv[1024],JHv[1024];
static bool loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f) return false; char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,a,b,w; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JLv[t+512]=a;JHv[t+512]=b;n++; }
  fclose(f); return n==1024; }
static inline i64 fdiv(i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0))) q--; return q; }
static inline i64 A(i64 x,i64 y,int role){ return role? arr(y,x) : arr(x,y); }
static int TL[3]={17,342,-342};
struct Cfg{int w,s1,s2,r1,r2; i64 kc,k1lo,k1hi,k2lo,k2hi;};
static int fullscore(int w,int s1,int s2,int r1,int r2,i64 kc,i64 k1,i64 k2,const std::vector<i64>&Apre){
    int sc=0; for(int t=-512;t<512;t++){ i64 dx=(i64)t<<w; i64 I1=fdiv(Apre[t+512]+k1,1ll<<s1);
        i64 J=fdiv(A(I1,dx,r2)+k2,1ll<<s2); if(J>=JLv[t+512]&&J<=JHv[t+512]) sc++; } return sc; }
int main(){ if(!loadwin()){printf("FATAL\n");return 1;}
    // ---- 1. focused self-test: baseline config must satisfy its OWN J values through the solver's algebra
    { int w=13,s1=23,s2=18; i64 kc=KC; i64 tgt[3];
      for(int i=0;i<3;i++){ i64 dx=(i64)TL[i]<<w; i64 I1=(arr(kc,dx)+K)>>s1; tgt[i]=(arr(I1,dx)+K)>>s2; }
      i64 lo=-(1ll<<40),hi=(1ll<<40); i64 I1s[3];
      for(int i=0;i<3;i++){ i64 dx=(i64)TL[i]<<w; i64 a=arr(kc,dx); i64 q=fdiv(a,1ll<<s1); i64 th=(1ll<<s1)-(a-q*(1ll<<s1));
          i64 k1=K; I1s[i]=q+(k1>=th?1:0);
          i64 chk=(arr(kc,dx)+K)>>s1; if(chk!=I1s[i]) printf("   !! region algebra WRONG at t=%d (%lld vs %lld)\n",TL[i],(long long)I1s[i],(long long)chk);
          i64 P=arr(I1s[i],dx); i64 aa=tgt[i]*(1ll<<s2)-P, bb=(tgt[i]+1)*(1ll<<s2)-1-P; lo=std::max(lo,aa); hi=std::min(hi,bb); }
      printf("[e1] SELF-TEST at the baseline config w=13 s1=23 s2=18 KC=0x4EF145: solver's K2 interval = [%lld,%lld] ; the model's K2 = 524288 is %s ; K1=524288 reproduces I1 exactly\n",
          (long long)lo,(long long)hi,(lo<=524288&&524288<=hi)?"INSIDE (solver correct)":"OUTSIDE (solver BROKEN)"); }
    // ---- 2. the negative-K2 hits, rescored
    printf("[e2] re-running the landmark solve with K2 allowed negative, then scoring every hit over all 1024 offsets\n");
    i64 WLO[3]={45617,18466836,18466858}, WHI[3]={45617,18466836,18466867};
    int bestscore=0; char bestdesc[256]=""; int nhit=0;
    for(int w=2;w<=20;w++){
      i64 dx[3]; for(int i=0;i<3;i++) dx[i]=(i64)TL[i]<<w;
      for(int s1=6;s1<=34;s1++) for(int s2=6;s2<=30;s2++){
        int E=s1+s2-2*w; if(E<0||E>28) continue;
        i64 slack=(i64)(3.0L*342.0L*powl(2.0L,(long double)(w-s2)))+8;
        long double f=powl(2.0L,(long double)E)/116964.0L;
        i64 klo=(i64)((18466836.0L-slack)*f)-4, khi=(i64)((18466836.0L+slack)*f)+4;
        if(klo<2) klo=2; if(khi>=(1ll<<28)) khi=(1ll<<28)-1; if(khi<klo||khi-klo>8000000) continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(i64 kc=klo;kc<=khi;kc++){
          i64 q[3],th[3]; for(int i=0;i<3;i++){ i64 a=A(kc,dx[i],r1); q[i]=fdiv(a,1ll<<s1); th[i]=(1ll<<s1)-(a-q[i]*(1ll<<s1)); }
          i64 bs[4]={0,th[0],th[1],th[2]}; std::sort(bs,bs+4);
          for(int b=0;b<4;b++){ i64 k1=bs[b]; if(k1>=(1ll<<s1)) continue; if(b&&bs[b]==bs[b-1]) continue;
            i64 k1hi=(1ll<<s1)-1; for(int z=0;z<4;z++) if(bs[z]>k1) { k1hi=bs[z]-1; break; }
            for(int c1=-2;c1<=2;c1++){
              i64 I1[3]; bool ok=true; for(int i=0;i<3;i++){ I1[i]=q[i]+(k1>=th[i]?1:0)+c1; if(I1[i]>=(1ll<<27)||I1[i]<=-(1ll<<27)) ok=false; }
              if(!ok) continue;
              i64 lo=-(1ll<<42),hi=(1ll<<42);
              for(int i=0;i<3&&lo<=hi;i++){ i64 P=A(I1[i],dx[i],r2); i64 aa=WLO[i]*(1ll<<s2)-P, bb=(WHI[i]+1)*(1ll<<s2)-1-P; lo=std::max(lo,aa); hi=std::min(hi,bb); }
              if(lo>hi) continue;
              if(lo>=0) continue;                 // non-negative K2 already known to give 0 hits
              nhit++;
              // full score: K1 sampled across its region (+ the c1 integer part), K2 across [lo,hi]
              std::vector<i64> Apre(1024); for(int t=-512;t<512;t++) Apre[t+512]=A(kc,(i64)t<<w,r1);
              for(int a1=0;a1<8;a1++){ i64 kk1=k1+(k1hi-k1)*a1/7 + (i64)c1*(1ll<<s1);
                for(int a2=0;a2<24;a2++){ i64 kk2=lo+(hi-lo)*a2/23;
                  int sc=fullscore(w,s1,s2,r1,r2,kc,kk1,kk2,Apre);
                  if(sc>bestscore){ bestscore=sc; snprintf(bestdesc,sizeof bestdesc,"w=%d s1=%d s2=%d r1=%d r2=%d KC=%lld K1=%lld K2=%lld",w,s1,s2,r1,r2,(long long)kc,(long long)kk1,(long long)kk2);} } } } } } } }
    printf("[e2] %d landmark-hitting configs with NEGATIVE K2 ; best full jwin18 score over sampled (K1,K2): %d/1024 at %s\n",nhit,bestscore,bestdesc);
    printf("     (baseline 918/1024 ; the published two-multiply-family ceiling is 938/944)\n");
    return 0; }
