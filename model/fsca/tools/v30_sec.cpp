// v30_sec.cpp -- independent re-measurement of branch 3's secondary claims.
#include "base2.h"
#include <cstdio>
static i64 arrN(i64 rec,i64 mul,int ND){ i64 R[40]; i64 T=0;
    for(int k=0;k<ND;k++){ auto bit=[&](int b)->int{return b<0?0:(int)((rec>>(b>62?62:b))&1);};
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag;
        R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18)T+=1ll<<(2*k);}
    if(ND>=3){int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2);}
    if(ND>=9){int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16;}
    return T;}
static Fsca F;
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static void winof(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 FL[1024],FH[1024];
template<class FN> static int sc_j(FN Jf){int s=0;for(int t=-512;t<=511;t++){i64 j=Jf(t);if(j>=FL[t+512]&&j<=FH[t+512])s++;}return s;}
// full model, NODE-DEPENDENT J
template<class FN> static int fullv_nt(FN Jf,int DU,int&guard){int v=0;guard=0;
    for(int node=0;node<=31;node++){int flip=node>=17,n=flip?32-node:node;const Tab&T=TB[n];i64 s=flip?-1:1;
      for(int t=-512;t<=511;t++){i64 dx=(i64)t<<13;
        i64 T1=T.C1f?sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3?sm(arr(T.c3<<5,A3)+K,26):0;
        i64 T2=T.S?sm(arr(Jf(node,t),T.S)+K,U2T[n]+DU):0;
        i64 V=C0T[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;winof(a,lo,hi);
        if(V<lo||V>hi){v++; if(node==0||node==1||node==2||node==16)guard++;}}}
    return v;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    {FILE*f=fopen("jwin18.txt","r");char ln[256];while(fgets(ln,sizeof ln,f)){if(ln[0]=='#')continue;long long t,l,h;
      if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){FL[t+512]=l;FH[t+512]=h;}}fclose(f);}
    auto base=[&](int t){i64 dx=(i64)t<<13;i64 I1=(arr(KC,dx)+K)>>23;return (arr(I1,dx)+K)>>18;};
    printf("BASE  jwin18 %d/1024\n", sc_j(base));

    // (a) digit count: I1/J identical for ND 14..18 ?  and ND=13 fine score
    for(int nd=10;nd<=18;nd++){
        int dI=0,dJ=0; for(int t=-512;t<=511;t++){i64 dx=(i64)t<<13;
            i64 I1a=(arr(KC,dx)+K)>>23, I1b=(arrN(KC,dx,nd)+K)>>23;
            i64 Ja=(arr(I1a,dx)+K)>>18, Jb=(arrN(I1b,dx,nd)+K)>>18;
            if(I1a!=I1b)dI++; if(Ja!=Jb)dJ++;}
        auto Jn=[&](int t){i64 dx=(i64)t<<13;i64 I1=(arrN(KC,dx,nd)+K)>>23;return (arrN(I1,dx,nd)+K)>>18;};
        auto Jn20=[&](int t){i64 dx=(i64)t<<13;i64 I1=(arrN(KC,dx,nd)+K)>>23;return (arrN(I1,dx,nd)+K)>>20;};
        int g; int vc=fullv_nt([&](int,int t){return Jn20(t);},0,g);
        printf("(a) ND=%2d sqpath: I1 diffs %4d  J diffs %4d  jwin18 %4d/1024   coarse viol %5d guard %d\n",nd,dI,dJ,sc_j(Jn),vc,g);
    }
    // (b) zero-extension of t at width w, first multiply only (and both)
    printf("(b) zero-extension (t treated as unsigned w-bit) :\n");
    for(int w=8;w<=12;w++){ i64 M=(1ll<<w)-1;
        auto Jz=[&](int t){ i64 tz=(i64)t & M; i64 dxz=tz<<13, dx=(i64)t<<13;
            i64 I1=(arr(KC,dxz)+K)>>23; return (arr(I1,dxz)+K)>>18; };
        printf("    w=%2d both-mults  jwin18 %4d/1024  J(17)=%lld J(342)=%lld J(-342)=%lld\n",w,sc_j(Jz),
            (long long)Jz(17),(long long)Jz(342),(long long)Jz(-342)); }
    // (d) the USER'S reading: t_sq = angle mod 1024 = distance from the LAST LUT entry point
    {
        auto Jm=[&](int t){ i64 ts = t>=0? t : t+1024; i64 dx=ts<<13;
            i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; };
        auto Jm20=[&](int t){ i64 ts=t>=0?t:t+1024; i64 dx=ts<<13;
            i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; };
        int g1,g2; int vf=fullv_nt([&](int,int t){return Jm(t);},2,g1);
        int vc=fullv_nt([&](int,int t){return Jm20(t);},0,g2);
        int gb; int vfb=fullv_nt([&](int,int t){return base(t);},2,gb);
        printf("(d) t_sq = angle mod 1024 : jwin18 %d/1024   fine viol %d (baseline fine %d)   coarse viol %d\n",
            sc_j(Jm),vf,vfb,vc);
        // t_sq = raw angle (no base subtraction) -- node dependent
        auto Jraw=[&](int node,int t){ i64 ts=node*1024+t; i64 dx=ts<<13;
            i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; };
        int g3; int vr=fullv_nt(Jraw,2,g3);
        printf("    t_sq = raw angle        : fine viol %d of 32768\n",vr);
    }
    // USER HYPOTHESIS 1: dx+1 for t<0 at the first multiply only (branch-3's best complement config)
    {
        auto Jc=[&](int t){ i64 dx=(i64)t<<13; i64 dx1=dx+(t<0?1:0);
            i64 I1=(arr(KC,dx1)+K)>>23; return (arr(I1,dx)+K)>>18; };
        auto Jc20=[&](int t){ i64 dx=(i64)t<<13; i64 dx1=dx+(t<0?1:0);
            i64 I1=(arr(KC,dx1)+K)>>23; return (arr(I1,dx)+K)>>20; };
        int g; int vc=fullv_nt([&](int,int t){return Jc20(t);},0,g);
        printf("H1  dx+1 for t<0 @mult1 : jwin18 %d/1024   coarse viol %d guard %d   J(17)=%lld J(342)=%lld J(-342)=%lld\n",
            sc_j(Jc),vc,g,(long long)Jc(17),(long long)Jc(342),(long long)Jc(-342));
    }
    // USER HYPOTHESIS 2(ii): dx = t<<14, s1=24, s2=19, KC unchanged
    {
        auto Jw=[&](int t){ i64 dx=(i64)t<<14; i64 I1=(arr(KC,dx)+K)>>24; return (arr(I1,dx)+K)>>19; };
        auto Jw20=[&](int t){ i64 dx=(i64)t<<14; i64 I1=(arr(KC,dx)+K)>>24; return (arr(I1,dx)+K)>>21; };
        int gf,gc; int vf=fullv_nt([&](int,int t){return Jw(t);},2,gf);
        int vc=fullv_nt([&](int,int t){return Jw20(t);},0,gc);
        printf("H2ii w=14,s1=24,s2=19  : jwin18 %d/1024   fine viol %d guard %d   coarse viol %d guard %d   J(17)=%lld J(342)=%lld J(-342)=%lld\n",
            sc_j(Jw),vf,gf,vc,gc,(long long)Jw(17),(long long)Jw(342),(long long)Jw(-342));
        // per-w table, KC fixed, s1=10+w, s2=5+w  (keeps s1+s2-2w = 41-26 invariant)
        printf("H2ii per-w (s1=10+w, s2=5+w, KC fixed): ");
        for(int w=6;w<=22;w++){ auto Jx=[&](int t){ i64 dx=(i64)t<<w; i64 I1=(arr(KC,dx)+K)>>(10+w); return (arr(I1,dx)+K)>>(5+w); };
            printf("w%d:%d ",w,sc_j(Jx)); }
        printf("\n");
        // H2(i): extra LSB appended, dx=(2t+b)<<13 -- but that doubles scale; use (2t+b)<<12
        for(int b=0;b<2;b++){ auto Jb=[&](int t){ i64 dx=((i64)2*t+b)<<12; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>18; };
            printf("H2i  dx=(2t+%d)<<12 : jwin18 %d/1024\n",b,sc_j(Jb)); }
    }
    return 0; }
