// v40_g.cpp -- how far does the amplitude argument reach?  The bound [-655360,65536] is a property of
// THIS array (rows cut below column 16, hot ones from column 18).  Scan the truncation column C and the
// hot-one column H for the SECOND multiply (and then the first) and ask: (i) how big does the loss get,
// (ii) can J(17)=45617 be reached at all, (iii) what does the full 1024 score do.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
static i64 JLv[1024],JHv[1024];
static bool loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f)return false; char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,a,b,w; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JLv[t+512]=a;JHv[t+512]=b;n++; } fclose(f); return n==1024; }
static i64 arrC(i64 rec,i64 mul,int C,int H,int gA,int gB){ i64 R[14]; i64 T=0;
    for(int k=0;k<14;k++){ auto bit=[&](int b)->int{return b<0?0:(int)((rec>>(b>62?62:b))&1);};
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k));
        T+=(R[k]>>C)<<C; if(d<0&&2*k>=H) T+=1ll<<(2*k); }
    if(gA&&C>=1){ int m=((R[0]>>(C-1))&1)+((R[1]>>(C-1))&1)+((R[2]>>(C-1))&1); T+=(1ll<<C)*(m>=2); }
    if(gB){ int p=((R[6]>>C)^(R[7]>>C)^(R[8]>>C))&1; T-=(1ll<<C)*p; }
    return T; }
int main(){ if(!loadwin()){printf("FATAL\n");return 1;}
    printf("[g0] sanity: arrC(.,.,16,18,1,1) == arr() ? ");
    { int bad=0; for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13; if(arrC(KC,dx,16,18,1,1)!=arr(KC,dx)) bad++; }
      printf("%s (%d/1024 differ)\n",bad?"NO":"YES",bad); }
    printf("[g1] second multiply retruncated (first multiply left as the FIPR-validated array):\n");
    printf("      C   maxloss(units)  maxloss/2^18   J(17)   best full score over H,gA,gB\n");
    for(int C=10;C<=26;C++){
        i64 wl=0; for(int k=0;k*2<C;k++) wl += (1ll<<C)-(1ll<<(2*k));
        i64 hot=0; for(int k=0;2*k<C+2;k++) hot += 1ll<<(2*k);
        i64 lb=wl+hot+(1ll<<C);
        int best=0,bj=0,bH=0,bgA=0,bgB=0; bool hit17=false; int hitcount=0;
        for(int H=0;H<=28;H+=1) for(int gA=0;gA<2;gA++) for(int gB=0;gB<2;gB++){
            int sc=0; i64 j17=0;
            for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arrC(I1,dx,C,H,gA,gB)+K)>>18;
                if(J>=JLv[t+512]&&J<=JHv[t+512])sc++; if(t==17)j17=J; }
            if(j17==45617){hit17=true;hitcount++;}
            if(sc>best){best=sc;bj=(int)j17;bH=H;bgA=gA;bgB=gB;} }
        printf("     %3d  %13lld   %10.2f   %6d   %4d/1024 at H=%d gA=%d gB=%d   J(17)=45617 reached: %s (%d variants)\n",
            C,(long long)lb,(double)lb/262144.0,bj,best,bH,bgA,bgB,hit17?"YES":"no",hitcount); }
    printf("[g2] BOTH multiplies retruncated at the same C (H,gA,gB swept independently per multiply is 1e6 combos; here H=C+2,gA=gB=1):\n");
    for(int C=10;C<=24;C++){ int sc=0; i64 j17=0,j342=0,jm=0;
        for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13; i64 I1=(arrC(KC,dx,C,C+2,1,1)+K)>>23; i64 J=(arrC(I1,dx,C,C+2,1,1)+K)>>18;
            if(J>=JLv[t+512]&&J<=JHv[t+512])sc++; if(t==17)j17=J; if(t==342)j342=J; if(t==-342)jm=J; }
        printf("     C=%2d : %4d/1024  J(17)=%lld (need 45617)  J(342)=%lld (need 18466836)  J(-342)=%lld (need 18466858..67)\n",
            C,sc,(long long)j17,(long long)j342,(long long)jm); }
    return 0; }
