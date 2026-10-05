// rt14_hitrate.cpp -- how often is the chain's J inside its jwin18 window, AS A FUNCTION OF WINDOW WIDTH?
// If J were exact wherever it can be seen, the hit rate would be ~100% at every width.  If J carries a zero-mean
// error of a few units at EVERY offset, the hit rate rises with the width like P(|err| fits).  Also prints, for
// the narrow windows, the signed distance from the model to the window centre (fine units).
#include "base2.h"
#include <cmath>
int main(){ build_tables(); FILE*f=fopen("jwin18.txt","r"); static long JL[1024],JH[1024]; char line[256]; while(fgets(line,256,f)){ if(line[0]=='#')continue; int t; long a,b,w; if(sscanf(line,"%d %ld %ld %ld",&t,&a,&b,&w)==4){JL[t+512]=a;JH[t+512]=b;} }
  int edges[]={1,4,8,16,32,64,128,256,100000}; int n[9]={0},hit[9]={0}; double sumd[9]={0},sumd2[9]={0};
  for(int t=-511;t<=511;t++){ if(!t)continue; i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18; long w=JH[t+512]-JL[t+512]+1; int b=0; while(w>edges[b])b++;
    n[b]++; if(J>=JL[t+512]&&J<=JH[t+512])hit[b]++; double c=0.5*(JL[t+512]+JH[t+512]); double d=J-c; sumd[b]+=d; sumd2[b]+=d*d; }
  puts("window width (fine J units)   offsets   model inside   hit rate    mean(model-centre)  rms(model-centre)");
  int lo=1; for(int b=0;b<9;b++){ if(n[b]) printf("   %6d .. %-6d          %5d      %5d        %5.1f%%        %+7.2f            %7.2f\n",lo,edges[b]>99999?99999:edges[b],n[b],hit[b],100.0*hit[b]/n[b],sumd[b]/n[b],sqrt(sumd2[b]/n[b])); lo=edges[b]+1; }
  // same with the EXACT real product KC t^2/2^15 (no chain at all) for comparison
  int n2[9]={0},hit2[9]={0}; for(int t=-511;t<=511;t++){ if(!t)continue; long double ex=(long double)KC*t*t/32768.0L; long J=(long)floorl(ex); long w=JH[t+512]-JL[t+512]+1; int b=0; while(w>edges[b])b++; n2[b]++; if(J>=JL[t+512]&&J<=JH[t+512])hit2[b]++; }
  puts("same, with J := floor(KC t^2 / 2^15) (the exact symmetric product, no chain):"); lo=1; for(int b=0;b<9;b++){ if(n2[b]) printf("   %6d .. %-6d          %5d      %5d        %5.1f%%\n",lo,edges[b]>99999?99999:edges[b],n2[b],hit2[b],100.0*hit2[b]/n2[b]); lo=edges[b]+1; }
  return 0; }
