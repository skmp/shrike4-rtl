// n2_sm.cpp -- the trace at t=-155 says I1 is floored one unit too far. Does using sm() (truncate
// toward zero) for the SQUARE PATH's latches fix it?  Test all 4 combinations, pure (no other change).
#include "base2.h"
#include <cstdio>
#include <cstring>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 shf(i64 v,int u,int m){ return m? sm(v,u):(v>>u); }
static i64 Jof(int t,int m1,int m2){ i64 dx=(i64)t<<13; i64 I1=shf(arr(KC,dx)+K,23,m1); return shf(arr(I1,dx)+K,18,m2); }
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    printf("jwin18 score, PURE shift-convention change (nothing else altered):\n");
    for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++){
        int s=0,sp=0,sn=0,np=0,nn=0;
        for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 j=Jof(t,m1,m2); bool ok=(j>=JL[i]&&j<=JH[i]);
            s+=ok; if(t>=0){np++;sp+=ok;} else {nn++;sn+=ok;} }
        printf("  I1 shift=%-2s  J shift=%-2s : %4d/1024   (t>=0: %d/%d   t<0: %d/%d)%s\n",
            m1?"sm":">>",m2?"sm":">>",s,sp,np,sn,nn,(m1==0&&m2==0)?"   <- current model":"");
    }
    printf("\nlandmarks per convention (need J(17)=45617, J(342)=18466836, J(-342) in [18466858,18466867]):\n");
    for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++)
        printf("  (%s,%s): J(17)=%lld  J(342)=%lld  J(-342)=%lld\n",m1?"sm":">>",m2?"sm":">>",
            (long long)Jof(17,m1,m2),(long long)Jof(342,m1,m2),(long long)Jof(-342,m1,m2));
    return 0; }
