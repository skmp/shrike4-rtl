// m3_resid.cpp -- with the 1s-complement operand negate in place, what is the SHAPE of the residual?
#include "base2.h"
#include <cstdio>
#include <vector>
#include <cmath>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 chainJ(int t,int q,int c){
    i64 dx=((i64)t<<13)+(t<0?q:0);
    i64 I1=(arr(KC,dx)+K)>>23;
    return ((arr(I1,dx)+K)>>18) - c;
}
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    for(int q=0;q>=-1;q--){
        printf("=== q=%d : residual (model J - nearest window edge), by |t| band ===\n",q);
        double sum=0; int n=0,inw=0; int mn=1<<30,mx=-(1<<30);
        for(int i=0;i<1024;i++){ if(!have[i])continue; int t=i-512; if(t==0)continue;
            i64 j=chainJ(t,q,0); i64 d = j<JL[i]? j-JL[i] : (j>JH[i]? j-JH[i] : 0);
            sum+=d; n++; if(!d) inw++; if(d<mn)mn=(int)d; if(d>mx)mx=(int)d; }
        printf("  in window %d/1024   residual min %d  max %d  mean %.2f\n",inw,mn,mx,sum/n);
        printf("  |t|:      ");for(int b=0;b<8;b++)printf("%4d-%-4d ",b*64,b*64+63);printf("\n");
        for(int sgn=1;sgn>=-1;sgn-=2){ printf("  t%s mean d:",sgn>0?"> 0":"< 0");
            for(int b=0;b<8;b++){ double s=0;int c=0;
                for(int a=b*64;a<b*64+64;a++){ int t=sgn*a; if(t<-512||t>511||t==0)continue; int i=t+512; if(!have[i])continue;
                    i64 j=chainJ(t,q,0); i64 d=j<JL[i]?j-JL[i]:(j>JH[i]?j-JH[i]:0); s+=d;c++; }
                printf("%9.2f ",c?s/c:0.0); } printf("\n"); }
        // best single constant
        int best=-1,bc=0;
        for(int c=-64;c<=64;c++){ int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=chainJ(i-512,q,c); if(j>=JL[i]&&j<=JH[i]) sc++; }
            if(sc>best){best=sc;bc=c;} }
        printf("  best J-constant offset c=%+d -> %d/1024\n\n",bc,best);
    }
    return 0; }
