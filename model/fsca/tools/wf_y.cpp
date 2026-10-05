// wf_y.cpp -- the sharpest certificate (|t|=37 vs |t|=59, both pinned by EXACT table-5 samples) demands that
// the square latch's deviation span >= 4.237 units, while the FIPR array cut at column 16 spans at most 1.737
// there.  The only structural knob that changes the span is the column at which the partial-product rows are
// truncated.  Sweep that column (and the matching hot-one / group-rule columns) for the square multiply only.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OK[1024];
// generalised array: rows cut below column C, hot ones kept from column C+2, group rules at C-1 / C
static i64 arrC(i64 rec,i64 mul,int C){ i64 R[14]; i64 T=0;
  for(int k=0;k<14;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
    int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
    i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k));
    T += (R[k]>>C)<<C; if(d<0 && 2*k>=C+2) T += 1ll<<(2*k); }
  int m=((R[0]>>(C-1))&1)+((R[1]>>(C-1))&1)+((R[2]>>(C-1))&1); T += (1ll<<C)*(m>=2);
  int p=((R[6]>>C)^(R[7]>>C)^(R[8]>>C))&1; T -= (1ll<<C)*p; return T; }
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;} fclose(f);
  printf(" C  g role   loss span   d(37)-d(59) max   best score /1022   Kq        Kr\n");
  int gbest=-1; char gd[200]="";
  for(int C=14;C<=22;C++) for(int g=0;g<=3;g++) for(int role=0;role<2;role++){
    i64 Kq0=(i64)KC<<g; double mn=1e30,mx=-1e30,l37=0,l59=0;
    for(int x=0;x<=511;x++){ i64 sq=(i64)x*x; i64 P=role?arrC(sq,Kq0,C):arrC(Kq0,sq,C);
      double L=(double)(Kq0*sq-P)/(double)(1ll<<(17+g));
      if(L<mn)mn=L; if(L>mx)mx=L; if(x==37)l37=L; if(x==59)l59=L; }
    int best=-1; i64 bKq=0,bKr=0;
    for(i64 dk=-40;dk<=40;dk++) for(int kr=0;kr<14;kr++){
      i64 Kq=Kq0+dk; int sh=17+g; i64 Kr=kr==0?0:(1ll<<(kr+7)); if(kr&&Kr>=(1ll<<sh))continue; int sc=0;
      for(int tt=-512;tt<=511;tt++){ if(!OK[tt+512])continue; i64 sq=(i64)tt*tt;
        i64 J=((role?arrC(sq,Kq,C):arrC(Kq,sq,C))+Kr)>>sh; if(J>=JL[tt+512]&&J<=JH[tt+512])sc++; }
      if(sc>best){best=sc;bKq=Kq;bKr=Kr;} }
    printf("%3d %2d %4d   %9.3f   %14.3f   %7d          %9lld %9lld\n",C,g,role,mx-mn,1.0+(l59-l37),best,(long long)bKq,(long long)bKr);
    if(best>gbest){gbest=best;snprintf(gd,200,"C=%d g=%d role=%d Kq=%lld Kr=%lld",C,g,role,(long long)bKq,(long long)bKr);} }
  printf("\nbest over the row-cut column sweep: %d / 1022   %s\n",gbest,gd);
  return 0; }
