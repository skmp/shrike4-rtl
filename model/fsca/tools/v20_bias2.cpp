// v20_bias2.cpp -- the additive-free-bias test with K1,K2 free, but with the rounding-constant
// SELECTIONS solved JOINTLY across all three landmark offsets (w2_diff.cpp tests the -342 selection
// independently and then scores with K1 = c17?b17:0, which does not realise the +342 selection when
// c17=0,c342=1 -- so its scored configurations need not satisfy the landmarks at all).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
typedef __int128 i128;
static i64 JL[1024],JH[1024];
static const i64 L=1ll<<21, KCMAX=1ll<<27;
int main(){
    { FILE*f=fopen("jwin18.txt","r");
      char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
    const i64 A=JL[529], C=JL[854], E=JL[170], F=JH[170], TGT=C-A;
    const i64 DML=E-A, DMH=F-A;
    long long tri=0,scanned=0,hit2=0,hit3=0,hit3_indep=0; int gbest=-1;
    struct B{ int w,s1,s2; i64 kc,K1,K2,c; int sc; } BB{0,0,0,0,0,0,0,-1};
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:tri,scanned,hit2,hit3,hit3_indep)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        int ee=s1+s2-2*w-15; if(ee<-30) continue;
        i128 nom=ee>=0?((i128)KC<<ee):((i128)KC>>(-ee)); if(nom<1||nom>=((i128)1<<27)) continue;
        tri++;
        // same bracket shape as w2_diff.cpp: KCw from the difference constraint, +-500 ppm
        long double cc=(long double)TGT*powl(2.0L,(long double)(s1+s2))/((342.0L*342.0L-17.0L*17.0L)*powl(2.0L,(long double)(2*w)));
        i64 lo=(i64)(cc*(1.0L-5e-4L))-16, hi=(i64)(cc*(1.0L+5e-4L))+16; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX;
        if(hi-lo>3000000ll) continue;
        i64 dxs[3]={(i64)17<<w,(i64)342<<w,-((i64)342<<w)};
        for(i64 kc=lo;kc<=hi;kc++){ scanned++;
            i64 P[3],v[3],b1[3]; const i64 M1=(i64)1<<s1, M2=(i64)1<<s2;
            for(int i=0;i<3;i++){ P[i]=arr(kc,dxs[i]); v[i]=P[i]>>s1; b1[i]=M1-(P[i]-(v[i]<<s1)); }
            bool any2=false, any3=false;
            for(int cm=0;cm<8;cm++){
                int c[3]={cm&1,(cm>>1)&1,(cm>>2)&1};
                i64 Lo1=0,Hi1=M1;
                for(int i=0;i<3;i++){ if(c[i]) Lo1=std::max(Lo1,b1[i]); else Hi1=std::min(Hi1,b1[i]); }
                if(Lo1>=Hi1||Lo1>=M1) continue;
                i64 K1v=Lo1;
                i64 Q[3],q[3],b2[3];
                for(int i=0;i<3;i++){ Q[i]=arr(v[i]+c[i],dxs[i]); q[i]=Q[i]>>s2; b2[i]=M2-(Q[i]-(q[i]<<s2)); }
                for(int dm=0;dm<8;dm++){
                    int d[3]={dm&1,(dm>>1)&1,(dm>>2)&1};
                    i64 Lo2=0,Hi2=M2;
                    for(int i=0;i<3;i++){ if(d[i]) Lo2=std::max(Lo2,b2[i]); else Hi2=std::min(Hi2,b2[i]); }
                    if(Lo2>=Hi2||Lo2>=M2) continue;
                    i64 K2v=Lo2;
                    i64 J17=q[0]+d[0], Jp=q[1]+d[1], Jm=q[2]+d[2];
                    if(Jp-J17!=TGT) continue;
                    any2=true;
                    if(Jm-J17<DML||Jm-J17>DMH) continue;
                    any3=true;
                    // verify by direct evaluation, then full 1024 score
                    auto Jf=[&](int t)->i64{ i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K1v)>>s1; return (arr(I1,dx)+K2v)>>s2; };
                    if(Jf(17)!=J17||Jf(342)!=Jp||Jf(-342)!=Jm) continue;   // selection realisation check
                    i64 cadd=A-Jf(17); int sc=0;
                    for(int t=-512;t<=511;t++){ i64 j=Jf(t)+cadd; if(j>=JL[t+512]&&j<=JH[t+512]) sc++; }
#pragma omp critical
                    { if(sc>BB.sc) BB=B{w,s1,s2,kc,K1v,K2v,cadd,sc}; }
                }
            }
            if(any2) hit2++; if(any3) hit3++;
            // w2_diff-style INDEPENDENT -342 test, for comparison
            { bool i2=false,i3=false;
              for(int c17=0;c17<2&&!i3;c17++) for(int cp=0;cp<2&&!i3;cp++){
                bool ok1; if(!c17&&!cp) ok1=true; else if(c17&&cp) ok1=(b1[0]<M1)&&(b1[1]<M1);
                else if(c17) ok1=(b1[0]<b1[1])&&(b1[0]<M1); else ok1=(b1[1]<b1[0])&&(b1[1]<M1);
                if(!ok1) continue;
                i64 Q0=arr(v[0]+c17,dxs[0]),Q1=arr(v[1]+cp,dxs[1]);
                i64 q0=Q0>>s2,q1=Q1>>s2,k0=M2-(Q0-(q0<<s2)),k1=M2-(Q1-(q1<<s2));
                for(int e0=0;e0<2;e0++) for(int e1=0;e1<2;e1++){
                    if((q1+e1)-(q0+e0)!=TGT) continue;
                    bool ok2; if(!e0&&!e1) ok2=true; else if(e0&&e1) ok2=(k0<M2)&&(k1<M2);
                    else if(e0) ok2=(k0<k1)&&(k0<M2); else ok2=(k1<k0)&&(k1<M2);
                    if(!ok2) continue; i2=true;
                    i64 Pm=arr(kc,dxs[2]), vm=Pm>>s1; bool stm=(Pm-(vm<<s1))!=0;
                    for(int cmm=0;cmm<2;cmm++){ if(cmm&&!stm) break; i64 Qm=arr(vm+cmm,dxs[2]); i64 qm=Qm>>s2;
                        bool k2s=(Qm-(qm<<s2))!=0;
                        for(int em=0;em<2;em++){ if(em&&!k2s) break; i64 dd=(qm+em)-(q0+e0);
                            if(dd>=DML&&dd<=DMH) i3=true; } }
                } }
              if(i3) hit3_indep++; }
        }
    }
    printf("triples %lld ; KCw values scanned %lld\n",tri,scanned);
    printf("kc with J(342)-J(17) exact for SOME joint (K1,K2)          : %lld\n",hit2);
    printf("kc where the -342 difference is ALSO in window, SAME K1,K2 : %lld\n",hit3);
    printf("kc passing w2_diff's INDEPENDENT -342 test (their 'hits3') : %lld\n",hit3_indep);
    if(BB.sc>=0) printf("best full jwin18 score with all three landmarks satisfied  : %d  (w=%d s1=%d s2=%d KCw=%lld K1=%lld K2=%lld c=%+lld)\n",
        BB.sc,BB.w,BB.s1,BB.s2,(long long)BB.kc,(long long)BB.K1,(long long)BB.K2,(long long)BB.c);
    else printf("best full jwin18 score with all three landmarks satisfied  : NONE\n");
    return 0;
}
