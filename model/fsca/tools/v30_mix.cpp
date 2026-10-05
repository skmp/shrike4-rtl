// v30_mix.cpp -- all four first/second operand-order combinations, coarse + fine, plus an
// arrN(.,.,14) == arr(.,.) equivalence fuzz.  Independent of w3_common.h except for the fuzz.
#include "base2.h"
#include <cstdio>
#include <random>
static i64 arrN(i64 rec, i64 mul, int ND) {   // verbatim copy of w3_common.h's arrN, for the fuzz only
    i64 R[40]; i64 T = 0;
    for (int k = 0; k < ND; k++) { auto bit=[&](int b)->int{return b<0?0:(int)((rec>>(b>62?62:b))&1);};
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag;
        R[k]=row*(1ll<<(2*k)); T+=(R[k]>>16)<<16; if(d<0&&2*k>=18) T+=1ll<<(2*k); }
    if(ND>=3){int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); T+=(1ll<<16)*(m15>=2);}
    if(ND>=9){int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1; T-=(1ll<<16)*p16;}
    return T; }
static Fsca F;
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static void winof(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 FL[1024],FH[1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    { FILE*f=fopen("jwin18.txt","r"); char ln[256]; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){FL[t+512]=l;FH[t+512]=h;} } fclose(f); }
    // fuzz arrN(.,.,14) vs arr
    { std::mt19937_64 g(12345); int bad=0;
      for(long i=0;i<3000000;i++){ i64 a=(i64)(g()%(1ull<<28))-(1ll<<27), b=(i64)(g()%(1ull<<28))-(1ll<<27);
        if(arrN(a,b,14)!=arr(a,b)) bad++; }
      // plus every operand pair the model actually uses
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        if(arrN(KC,dx,14)!=arr(KC,dx))bad++; if(arrN(dx,KC,14)!=arr(dx,KC))bad++;
        i64 I1a=(arr(KC,dx)+K)>>23, I1b=(arr(dx,KC)+K)>>23;
        if(arrN(I1a,dx,14)!=arr(I1a,dx))bad++; if(arrN(dx,I1a,14)!=arr(dx,I1a))bad++;
        if(arrN(I1b,dx,14)!=arr(I1b,dx))bad++; if(arrN(dx,I1b,14)!=arr(dx,I1b))bad++; }
      printf("arrN(.,.,14) vs arr(.,.): %d mismatches over 3e6 random 28-bit pairs + all model pairs\n",bad); }
    const char* nm[4]={"I1=arr(KC,dx) J=arr(I1,dx)","I1=arr(KC,dx) J=arr(dx,I1)","I1=arr(dx,KC) J=arr(I1,dx)","I1=arr(dx,KC) J=arr(dx,I1)"};
    for(int o=0;o<4;o++){
        auto Jf=[&](int t,int s2)->i64{ i64 dx=(i64)t<<13;
            i64 I1=(o&2)?((arr(dx,KC)+K)>>23):((arr(KC,dx)+K)>>23);
            return (o&1)?((arr(dx,I1)+K)>>s2):((arr(I1,dx)+K)>>s2); };
        int sc=0; for(int t=-512;t<=511;t++){ i64 j=Jf(t,18); if(j>=FL[t+512]&&j<=FH[t+512])sc++; }
        for(int DU=0;DU<=2;DU+=2){ int s2=DU?18:20; int v=0,g=0;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
              for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=T.C1f?sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3?sm(arr(T.c3<<5,A3)+K,26):0;
                i64 T2=T.S?sm(arr(Jf(t,s2),T.S)+K,U2T[n]+DU):0;
                i64 V=C0T[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; winof(a,lo,hi);
                if(V<lo||V>hi){v++; if(node==0||node==1||node==2||node==16)g++;} } }
            printf("%-28s %s viol=%3d guard=%d%s\n",nm[o],DU?"fine  ":"coarse",v,g, DU?"":"");
            if(DU==0) printf("%-28s jwin18(s2=18) score %4d/1024  pins %lld / %lld / %lld\n","",sc,
                (long long)Jf(17,18),(long long)Jf(342,18),(long long)Jf(-342,18)); }
    }
    return 0; }
