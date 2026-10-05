// v21_exh.cpp -- NO BRACKET AT ALL.  For a given geometry, sweep KCw over the whole signed-recoder
// range [1,2^27) and count, exactly, the (KCw,K1) pairs (K1 in [0,2^s1), K2 solved as an interval in
// [0,2^s2)) that hit J(342), that hit J(17), and that hit both with one (K1,K2).
// This removes the bracket from the argument entirely: if w2_bf's bracketed 147456 / 0 are right,
// this must return the same 147456 for J(342) and the same 0 for the joint count.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); if(!f){printf("NO jwin18.txt\n");exit(1);} char ln[256];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;} } fclose(f); }
static inline bool k2iv(i64 Q,i64 lo,i64 hi,int s2,i64&A,i64&B){ i64 per=1ll<<s2;
    A=lo*per-Q; B=(hi+1)*per-1-Q; if(A<0)A=0; if(B>per-1)B=per-1; return A<=B; }
int main(int argc,char**argv){ load();
  int w=atoi(argv[1]), s1=atoi(argv[2]), s2=atoi(argv[3]);
  i64 KCLO=1, KCHI=(1ll<<27)-1;
  const i64 d17=(i64)17<<w, dp=(i64)342<<w, dm=-(i64)342<<w, per1=1ll<<s1;
  long long h342=0,h17=0,hb=0,hall=0; i64 lo342=-1,hi342=-1,lo17=-1,hi17=-1;
  #pragma omp parallel for schedule(static,4096) reduction(+:h342,h17,hb,hall)
  for(i64 kc=KCLO;kc<=KCHI;kc++){
    i64 P17=arr(kc,d17), Pp=arr(kc,dp), Pm=arr(kc,dm);
    i64 v17=P17>>s1,r17=P17-(v17<<s1), vp=Pp>>s1,rp=Pp-(vp<<s1), vm=Pm>>s1,rm=Pm-(vm<<s1);
    i64 T17=per1-r17,Tp=per1-rp,Tm=per1-rm; i64 cuts[5]={0,T17,Tp,Tm,per1}; std::sort(cuts,cuts+5);
    for(int c=0;c<4;c++){ i64 a=cuts[c],b=cuts[c+1]; if(b<=a)continue; i64 cnt=b-a;
      i64 i17=v17+(a>=T17), ip=vp+(a>=Tp), im=vm+(a>=Tm);
      i64 Xp,Yp,X,Y,X3,Y3;
      bool okp=k2iv(arr(ip,dp),JL[854],JH[854],s2,Xp,Yp);
      bool ok17=k2iv(arr(i17,d17),JL[529],JH[529],s2,X,Y);
      if(okp){h342+=cnt;
        #pragma omp critical
        { if(lo342<0||kc<lo342)lo342=kc; if(kc>hi342)hi342=kc; } }
      if(ok17){h17+=cnt;
        #pragma omp critical
        { if(lo17<0||kc<lo17)lo17=kc; if(kc>hi17)hi17=kc; } }
      if(okp&&ok17){ i64 L=std::max(Xp,X),H=std::min(Yp,Y); if(L<=H){ hb+=cnt;
        if(k2iv(arr(im,dm),JL[170],JH[170],s2,X3,Y3)&&std::max(L,X3)<=std::min(H,Y3)) hall+=cnt; } } } }
  printf("w=%d s1=%d s2=%d  KCw in [1,2^27)  pairs=%lld\n",w,s1,s2,(long long)(KCHI-KCLO+1)*per1);
  printf("  h342 = %lld   (KCw range with a J(342) hit: %lld..%lld)\n",h342,(long long)lo342,(long long)hi342);
  printf("  h17  = %lld   (KCw range with a J(17)  hit: %lld..%lld)\n",h17,(long long)lo17,(long long)hi17);
  printf("  hboth= %lld   hall3 = %lld\n",hb,hall);
  return 0; }
