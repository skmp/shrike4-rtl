// wv_chain_hit.cpp -- DIRECT evaluation of a named chain structure: J at the core t, and the full
// score against jwin.txt.  No solving, no search: plain arithmetic.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
static i64 WL[1024],WH[1024]; static int WF[1024];
static void load(){ FILE*f=fopen("jwin.txt","r"); for(int i=0;i<1024;i++){int t,fe;long long a,b;
    if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&fe)!=4){exit(1);} WL[i]=a;WH[i]=b;WF[i]=fe;} fclose(f);}
static inline i64 RULE(int r,i64 P,i64 Kc,int s){ i64 x=P+Kc; switch(r){
  case 0: return x>>s;                                   // floor
  case 1: return -((-x)>>s);                             // ceil
  case 2: return x>=0? (x>>s) : -((-x)>>s);              // toward zero
  case 3: return x>=0? (x>>s) : -((~x)>>s);              // sm on P+K
  default: return P>=0? ((P+Kc)>>s) : -(((~P)+Kc)>>s); } } // magsm
static inline i64 MASK(int m,i64 v){ if(m==0)return v; if(m==1)return v&~(i64)1;
    i64 a=v<0?-v:v; a&=~(i64)1; return v<0?-a:a; }
int main(int argc,char**argv){
  // cst j g1 role1 s1 rule1 K1 g2 role2 s2 rule2 m1 mask sign K2
  i64 cst=atoll(argv[1]); int g1=atoi(argv[3]),r1=atoi(argv[4]),s1=atoi(argv[5]),rl1=atoi(argv[6]);
  i64 K1=atoll(argv[7]); int g2=atoi(argv[8]),r2=atoi(argv[9]),s2=atoi(argv[10]),rl2=atoi(argv[11]);
  int m1=atoi(argv[12]),mk=atoi(argv[13]),sg=atoi(argv[14]); i64 K2=atoll(argv[15]);
  build_tables(); load();
  auto Jof=[&](int t)->i64{ i64 dxa=(i64)t<<g1; i64 P1= r1? arr(dxa,cst): arr(cst,dxa);
      i64 I1=MASK(mk, RULE(rl1,P1,K1,s1)+m1); i64 dxb=(i64)t<<g2;
      i64 P2= r2? arr(dxb,I1): arr(I1,dxb); i64 v=RULE(rl2,P2,K2,s2); return sg? -v: v; };
  int core[4]={-342,376,-376,258}; int coreok=0;
  for(int i=0;i<4;i++){ int t=core[i]; i64 J=Jof(t); int ok=(J>=WL[t+512]&&J<=WH[t+512]); coreok+=ok;
     printf("t=%5d  J=%12lld  window [%lld,%lld]  %s\n",t,(long long)J,(long long)WL[t+512],(long long)WH[t+512],ok?"OK":"MISS"); }
  int s=0; for(int i=0;i<1024;i++){ if(!WF[i])continue; i64 J=Jof(i-512); if(J>=WL[i]&&J<=WH[i])s++; }
  printf("core satisfied: %d/4   full score vs jwin.txt: %d / 1022\n",coreok,s);
  // a few sanity prints of the shape of J
  for(int t : {1,2,10,100,-100,511,-512}) printf("   J(%d) = %lld   (39.4712 t^2 = %.1f)\n",t,(long long)Jof(t),39.4712*t*t);
  return 0; }
