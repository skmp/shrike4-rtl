// s2_hard.cpp -- WHICH offsets resist?  The 5 that admit no KC in a +-20000 band and the 67 that admit
// no first-latch rounding constant. These are the structurally hardest samples: everything else can be
// absorbed by a constant somewhere, they cannot.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 KCM=0x4ef145;
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} }
    fclose(f);
    const i64 R=20000;
    std::vector<int> noKC, noK1;
    for(int i=0;i<1024;i++){ if(!have[i])continue; int t=i-512; i64 dx=(i64)t<<13;
        bool any=false;
        for(i64 d=-R;d<=R&&!any;d++){ i64 I1=(arr(dx,KCM+d)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
            if(j>=JL[i]&&j<=JH[i]) any=true; }
        if(!any) noKC.push_back(t);
        i64 P=arr(dx,KCM), a=P>>23;
        i64 j1=(arr(a,dx)+K)>>18, j2=(arr(a+1,dx)+K)>>18;
        bool ok=(j1>=JL[i]&&j1<=JH[i])||(j2>=JL[i]&&j2<=JH[i]);
        if(!ok) noK1.push_back(t);
    }
    printf("OFFSETS ADMITTING NO KC ANYWHERE IN +-20000 (%zu):\n  ",noKC.size());
    for(int t:noKC) printf("%+d ",t); printf("\n");
    printf("  window widths:"); for(int t:noKC) printf(" t=%+d:[%lld,%lld]w%lld",t,
        (long long)JL[t+512],(long long)JH[t+512],(long long)(JH[t+512]-JL[t+512]+1)); printf("\n");
    printf("\nOFFSETS ADMITTING NO FIRST-LATCH ROUNDING CONSTANT (%zu):\n  ",noK1.size());
    for(size_t i=0;i<noK1.size();i++){ printf("%+d ",noK1[i]); if((i+1)%20==0)printf("\n  "); } printf("\n");
    // structure of the 67
    int pos=0,neg=0; int b7[8]={0}; std::vector<int> aw;
    for(int t:noK1){ if(t>0)pos++; else if(t<0)neg++; b7[t&7]++; aw.push_back(t<0?-t:t); }
    std::sort(aw.begin(),aw.end());
    printf("  t>0: %d   t<0: %d\n",pos,neg);
    printf("  by t&7:"); for(int k=0;k<8;k++) printf(" %d:%d",k,b7[k]); printf("\n");
    printf("  |t| min %d median %d max %d\n",aw.front(),aw[aw.size()/2],aw.back());
    printf("  window width of these: ");
    { std::vector<i64> w; for(int t:noK1) w.push_back(JH[t+512]-JL[t+512]+1); std::sort(w.begin(),w.end());
      printf("min %lld median %lld max %lld  (all offsets: median ",(long long)w.front(),(long long)w[w.size()/2],(long long)w.back());
      std::vector<i64> aw2; for(int i=0;i<1024;i++) if(have[i]) aw2.push_back(JH[i]-JL[i]+1);
      std::sort(aw2.begin(),aw2.end()); printf("%lld)\n",(long long)aw2[aw2.size()/2]); }
    // are the landmarks in these sets?
    for(int t : {17,-17,342,-342}){ bool a=std::find(noKC.begin(),noKC.end(),t)!=noKC.end();
        bool b=std::find(noK1.begin(),noK1.end(),t)!=noK1.end();
        printf("  t=%+4d : noKC=%s  noK1=%s\n",t,a?"YES":"no",b?"YES":"no"); }
    return 0; }
