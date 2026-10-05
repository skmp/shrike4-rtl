// w2_chk.cpp -- adversarial checks on w2_land's machinery.
//  (1) brute-force vs the "3 of 4 selections" rule for free latch constants, on small shifts
//  (2) reproduce the independent e6_nol1 fact: at the baseline geometry NO integer I1 puts J(342) in window
//  (3) bracket sufficiency: scan 64x wider than the bracket on sampled triples, look for t=342 hits outside
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    // ---- (1) brute force the free-constant rule ----
    printf("== check 1: free-latch-constant rule vs brute force ==\n");
    int bad=0, cases=0;
    for(int s1=5;s1<=9;s1++) for(int w=10;w<=14;w++) for(i64 kc=5173000;kc<5173040;kc++){
        i64 dxa=(i64)17<<w, dxb=(i64)342<<w;
        i64 Pa=arr(kc,dxa), Pb=arr(kc,dxb);
        i64 va=Pa>>s1, vb=Pb>>s1;
        i64 bpa=((i64)1<<s1)-(Pa-(va<<s1)), bpb=((i64)1<<s1)-(Pb-(vb<<s1));
        bool seen[2][2]={{false,false},{false,false}};
        for(i64 k1=0;k1<((i64)1<<s1);k1++){ int ca=(int)(((Pa+k1)>>s1)-va), cb=(int)(((Pb+k1)>>s1)-vb);
            if(ca<0||ca>1||cb<0||cb>1){ printf("  UNEXPECTED latch step ca=%d cb=%d\n",ca,cb); bad++; continue; }
            seen[ca][cb]=true; }
        for(int ca=0;ca<2;ca++) for(int cb=0;cb<2;cb++){ bool pred;
            if(ca==0&&cb==0) pred=true;
            else if(ca==1&&cb==1) pred=(bpa<((i64)1<<s1))&&(bpb<((i64)1<<s1));
            else if(ca==1&&cb==0) pred=(bpa<bpb)&&(bpa<((i64)1<<s1));
            else pred=(bpb<bpa)&&(bpb<((i64)1<<s1));
            cases++; if(pred!=seen[ca][cb]){ bad++; if(bad<5) printf("  MISMATCH s1=%d w=%d kc=%lld sel=(%d,%d) pred=%d real=%d\n",s1,w,(long long)kc,ca,cb,(int)pred,(int)seen[ca][cb]); } }
    }
    printf("  %d selection cases brute-forced, %d mismatches\n",cases,bad);
    // ---- (2) e6_nol1 cross-check: at baseline geometry is any I1 good for t=342? ----
    printf("\n== check 2: is ANY integer I1 (free K2) able to put J(342) in window? ==\n");
    for(int w=13;w<=13;w++) for(int s2=16;s2<=22;s2++){
        i64 dx=(i64)342<<w; long long n=0; i64 first=-1;
        i64 c0=((i64)JL[854]<<s2)/dx - 4, c1=(((i64)JH[854]+2)<<s2)/dx + 4;
        for(i64 i1=c0;i1<=c1;i1++){ i64 q=arr(i1,dx)>>s2; if((q>=JL[854]&&q<=JH[854])||(q+1>=JL[854]&&q+1<=JH[854])){ if(first<0)first=i1; n++; } }
        printf("  w=13 s2=%2d : %lld admissible I1 values (searched %lld)%s\n",s2,n,(long long)(c1-c0+1), n?"":"   <-- NONE, matches e6_nol1");
    }
    printf("  same for t=17 (window width 1):\n");
    for(int s2=16;s2<=22;s2++){ i64 dx=(i64)17<<13; long long n=0;
        i64 c0=((i64)JL[529]<<s2)/dx-4, c1=(((i64)JH[529]+2)<<s2)/dx+4;
        for(i64 i1=c0;i1<=c1;i1++){ i64 q=arr(i1,dx)>>s2; if(q==JL[529]||q+1==JL[529]) n++; }
        printf("    s2=%2d : %lld admissible I1\n",s2,n); }
    // ---- (3) bracket sufficiency ----
    printf("\n== check 3: bracket sufficiency (scan 64x wider, count t=342 hits outside) ==\n");
    long long outside=0, inside=0, tri=0;
    for(int w=6;w<=22;w+=4) for(int s1=8;s1<=40;s1+=4) for(int s2=8;s2<=40;s2+=4){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        i64 lo,hi; brk(342,w,s1,s2,lo,hi); i64 wid=hi-lo+1; if(wid>200000) continue; tri++;
        i64 L=lo-31*wid, H=hi+32*wid; if(L<1)L=1; if(H>KCMAX)H=KCMAX;
        i64 dx=(i64)342<<w;
        for(i64 kc=L;kc<=H;kc++){ i64 P=arr(kc,dx), v=P>>s1; int ok=0;
            for(int c=0;c<2;c++){ i64 q=arr(v+c,dx)>>s2; if((q>=JL[854]&&q<=JH[854])||(q+1>=JL[854]&&q+1<=JH[854])) ok=1; }
            if(ok){ if(kc>=lo&&kc<=hi) inside++; else { outside++; if(outside<6) printf("  OUTSIDE hit: w=%d s1=%d s2=%d kc=%lld bracket=[%lld,%lld]\n",w,s1,s2,(long long)kc,(long long)lo,(long long)hi); } } }
    }
    printf("  %lld triples widened: %lld hits inside bracket, %lld outside\n",tri,inside,outside);
    return 0; }
