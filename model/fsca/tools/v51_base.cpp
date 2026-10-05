// v51_base.cpp -- INDEPENDENT scoring harness for the square-path latch J at the fine scale (shift 18).
// Reads jwin18.txt directly, builds its own window table, and scores the baseline chain.
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024], JH[1024]; static bool HAVE[1024];
static void load_win(){ memset(HAVE,0,sizeof HAVE);
  FILE*f=fopen("jwin18.txt","r"); if(!f){fprintf(stderr,"no jwin18.txt\n");exit(1);} char line[512]; int n=0;
  while(fgets(line,sizeof line,f)){ if(line[0]=='#')continue; long long t,lo,hi,w;
    if(sscanf(line,"%lld %lld %lld %lld",&t,&lo,&hi,&w)!=4) continue;
    if(t<-512||t>511){fprintf(stderr,"bad t %lld\n",t);exit(1);}
    if(hi-lo+1!=w){fprintf(stderr,"width mismatch at t=%lld\n",t);exit(1);}
    JL[t+512]=lo; JH[t+512]=hi; HAVE[t+512]=true; n++; }
  fclose(f); if(n!=1024){fprintf(stderr,"expected 1024 lines, got %d\n",n);exit(1);}
  for(int i=0;i<1024;i++) if(!HAVE[i]||JL[i]>JH[i]){fprintf(stderr,"infeasible/missing t=%d\n",i-512);exit(1);}
}
static inline i64 Jbase(int t,int order){ i64 dx=(i64)t<<13;
  i64 I1 = order? (arr(dx,KC)+K)>>23 : (arr(KC,dx)+K)>>23;
  return (arr(I1,dx)+K)>>18; }
int main(){ build_tables(); load_win();
  for(int order=0;order<2;order++){
    int tot=0,pos=0,neg=0;
    for(int t=-512;t<=511;t++){ i64 J=Jbase(t,order); bool ok=(J>=JL[t+512]&&J<=JH[t+512]);
      if(ok){tot++; if(t>=0)pos++; else neg++;} }
    printf("order=%s  score %d/1024   pos %d/512  neg %d/512   J(17)=%lld J(342)=%lld J(-342)=%lld\n",
      order?"arr(dx,KC)":"arr(KC,dx)", tot,pos,neg,
      (long long)Jbase(17,order),(long long)Jbase(342,order),(long long)Jbase(-342,order));
  }
  // sanity: window stats reported by b39
  i64 sumw=0; int tight=0; for(int i=0;i<1024;i++){sumw+=JH[i]-JL[i]+1; if(JH[i]-JL[i]<=3)tight++;}
  printf("windows: mean width %.1f, %d pinned width<=4 ; landmarks JL/JH: t=17 [%lld,%lld] t=342 [%lld,%lld] t=-342 [%lld,%lld]\n",
    (double)sumw/1024,tight,(long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
    (long long)JL[-342+512],(long long)JH[-342+512]);
  return 0; }
