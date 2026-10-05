// rt10_events.cpp -- the EVENT offsets: where this model's fine J lies outside its jwin18 window, with the needed
// correction range and the operands' bit / Booth-digit patterns.  106 events: 90 need J lower, 16 higher.
// Build: g++ -O2 -std=c++17 rt10_events.cpp -o /tmp/rt10 && /tmp/rt10   (run from tools/, reads jwin18.txt)
#include "base2.h"
#include <string>
static std::string bits(i64 v,int n){ std::string s; for(int i=n-1;i>=0;i--){ s+=((v>>i)&1)?'1':'0'; if(i%4==0&&i) s+='_'; } return s; }
static std::string booth(i64 v,int nd){ std::string s; for(int k=nd-1;k>=0;k--){ int b1=(v>>(2*k+1))&1,b0=(v>>(2*k))&1,bm=k?((v>>(2*k-1))&1):0; int d=-2*b1+b0+bm; char c = d==0?'0': d==1?'+': d==2?'P': d==-1?'-':'M'; s+=c; } return s; }
int main(){ build_tables(); FILE*f=fopen("jwin18.txt","r"); static long JL[1024],JH[1024]; char line[256]; while(fgets(line,256,f)){ if(line[0]=='#')continue; int t; long a,b,w; if(sscanf(line,"%d %ld %ld %ld",&t,&a,&b,&w)==4){JL[t+512]=a;JH[t+512]=b;} }
  puts("events: offsets where the model's fine J is OUTSIDE its window (needed correction range dJ, fine units)");
  puts("    t   t bits        dJ range     I1 (Booth digits, k=11..0)      I1 frac   J18 low byte   model J18");
  int nev=0,npos=0,nneg=0;
  for(int a=-511;a<=511;a++){ int t=a; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J18=(arr(I1,dx)+K)>>18; long lo=JL[t+512]-J18, hi=JH[t+512]-J18; if(lo<=0&&hi>=0)continue; nev++; if(hi<0)nneg++; else npos++;
    long double x=(long double)KC*t/1024.0L; long double fr=x-floorl(x);
    if(hi-lo<=12) printf("%5d  %s  [%+5ld,%+5ld]   %9lld %s   %.3Lf   %s   %lld\n",t,bits(t&1023,10).c_str(),lo,hi,(long long)I1,booth(I1,12).c_str(),fr,bits(J18&255,8).c_str(),(long long)J18); }
  printf("%d event offsets: %d need J lower, %d need J higher\n",nev,nneg,npos); }
