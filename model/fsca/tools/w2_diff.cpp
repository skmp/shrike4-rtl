// w2_diff.cpp -- the landmark test with ONE extra freedom: an arbitrary additive constant on J.
// If the latch carried a bias c outside one rounding period (the b38 solver implicitly allows this),
// the absolute values of J stop mattering and only the DIFFERENCE does:
//        J(342) - J(17)  must equal  18466836 - 45617 = 18421219
// (the baseline gives 18421216, three short).  Scanned over every alignment, both shifts, a widened
// KCw region and both latch-constant selections.  Nothing here is claimed to be a rounding constant --
// this is a deliberately over-permissive test, so a miss is a strong exclusion.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<27;      // the array recodes a SIGNED 28-bit operand
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    const i64 TGT=JL[854]-JL[529];
    printf("target J(342)-J(17) = %lld ; baseline w=13 s1=23 s2=18 KC gives %lld\n",(long long)TGT,
        (long long)((((arr((arr(KC,(i64)342<<13)+K)>>23,(i64)342<<13)+K)>>18))-(((arr((arr(KC,(i64)17<<13)+K)>>23,(i64)17<<13)+K)>>18))));
    long long tri=0,scanned=0,hits=0,hits3=0; int gbest=-1;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:tri,scanned,hits,hits3)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
      for(int s2=4;s2<=40;s2++){
        long double nom=157.886L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        tri++;
        // KCw bracket from the DIFFERENCE constraint, widened by +-3000 ppm
        long double cc=(long double)TGT*powl(2.0L,(long double)(s1+s2))/((long double)(342.0L*342.0L-17.0L*17.0L)*powl(2.0L,(long double)(2*w)));
        i64 lo=(i64)(cc*(1.0L-5e-4L))-16, hi=(i64)(cc*(1.0L+5e-4L))+16; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX;
        if(hi-lo>3000000ll) continue; scanned+=hi-lo+1;
        i64 d17=(i64)17<<w, d342=(i64)342<<w, dm=(i64)-342<<w;
        for(i64 kc=lo;kc<=hi;kc++){
            i64 P17=arr(kc,d17), Pp=arr(kc,d342);
            i64 v17=P17>>s1, vp=Pp>>s1;
            i64 b17=((i64)1<<s1)-(P17-(v17<<s1)), bpp=((i64)1<<s1)-(Pp-(vp<<s1));
            for(int c17=0;c17<2;c17++) for(int cp=0;cp<2;cp++){
                bool ok1; if(c17==0&&cp==0) ok1=true;
                else if(c17==1&&cp==1) ok1=(b17<((i64)1<<s1))&&(bpp<((i64)1<<s1));
                else if(c17==1&&cp==0) ok1=(b17<bpp)&&(b17<((i64)1<<s1));
                else ok1=(bpp<b17)&&(bpp<((i64)1<<s1));
                if(!ok1) continue;
                i64 Q17=arr(v17+c17,d17), Qp=arr(vp+cp,d342);
                i64 q17=Q17>>s2, qp=Qp>>s2;
                i64 k17=((i64)1<<s2)-(Q17-(q17<<s2)), kp=((i64)1<<s2)-(Qp-(qp<<s2));
                for(int e17=0;e17<2;e17++) for(int ep=0;ep<2;ep++){
                    if((qp+ep)-(q17+e17)!=TGT) continue;
                    bool ok2; if(e17==0&&ep==0) ok2=true;
                    else if(e17==1&&ep==1) ok2=(k17<((i64)1<<s2))&&(kp<((i64)1<<s2));
                    else if(e17==1&&ep==0) ok2=(k17<kp)&&(k17<((i64)1<<s2));
                    else ok2=(kp<k17)&&(kp<((i64)1<<s2));
                    if(!ok2) continue;
                    hits++;
                    // does the SAME configuration also give the right -342 difference?
                    i64 Pm=arr(kc,dm), vm=Pm>>s1;
                    bool stm=(Pm-(vm<<s1))!=0; bool got=false;
                    for(int cm=0;cm<2&&!got;cm++){ if(cm&&!stm) break; i64 Qm=arr(vm+cm,dm); i64 qm=Qm>>s2;
                        bool k2s=(Qm-(qm<<s2))!=0;
                        for(int em=0;em<2&&!got;em++){ if(em&&!k2s) break; i64 dd=(qm+em)-(q17+e17);
                            if(dd>=JL[170]-JL[529]&&dd<=JH[170]-JL[529]) got=true; } }
                    if(got){ hits3++;
                      // full 1024 score of this configuration, with the additive constant c fixed by t=17
                      // and K1,K2 chosen as the smallest values realising the required selections.
                      i64 K1v = c17? b17 : 0, K2v = e17? k17 : 0;
                      if((c17&&b17>=((i64)1<<s1))||(e17&&k17>=((i64)1<<s2))) continue;
                      auto Jf=[&](int t)->i64{ i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K1v)>>s1; return (arr(I1,dx)+K2v)>>s2; };
                      i64 cadd = JL[529]-Jf(17); int sc=0;
                      for(int i2=0;i2<1024;i2++) if(have[i2]){ i64 j=Jf(i2-512)+cadd; if(j>=JL[i2]&&j<=JH[i2]) sc++; }
                      #pragma omp critical
                      { if(sc>gbest){ gbest=sc; printf("  3-landmark config: w=%d s1=%d s2=%d KCw=%lld K1=%lld K2=%lld c=%+lld -> full %d/1024\n",
                          w,s1,s2,(long long)kc,(long long)K1v,(long long)K2v,(long long)cadd,sc); fflush(stdout); } }
                    }
                } }
        }
      } }
    printf("triples %lld ; KCw values scanned %lld\n",tri,scanned);
    printf("configurations with J(342)-J(17) EXACTLY %lld : %lld\n",(long long)TGT,hits);
    printf("   ... and the -342 difference also in window : %lld\n",hits3);
    printf("best full-1024 score among those (additive constant fixed by t=17): %d\n",gbest);
    return 0; }
