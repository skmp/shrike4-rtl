// v21_chk.cpp -- adversarial validity checks on the machinery the 147456 / 0 claim rests on.
//  (1) harness-vs-contract: does the DOCUMENTED baseline (w=13,s1=23,s2=18,KC=0x4EF145,K1=K2=2^19)
//      reproduce J(17)=45630, J(342)=18466846, J(-342)=18466857 and score 918/1024 on jwin18?
//  (2) operand domain: max |rec| and |mul| ever handed to arr() inside the w2_bf corner, vs the
//      2^27 signed-recoder limit -- a scan outside it would give spurious passes/failures.
//  (3) loss bound: is |arr(rec,mul) - rec*mul| <= 2^21 (the LMAX the bracket assumes) on every
//      operand pair the scan actually generates?  If not, the bracket can miss hits.
//  (4) is the joint test VACUOUS?  count J(17)-only hits per geometry -- if J(17) is never hit at
//      all in this corner, "0 joint" says nothing about jointness.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); if(!f){printf("NO jwin18.txt\n");exit(1);} char ln[256];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;} } fclose(f); }
static inline bool k2iv(i64 Q,i64 lo,i64 hi,int s2,i64&A,i64&B){ i64 per=1ll<<s2;
    A=lo*per-Q; B=(hi+1)*per-1-Q; if(A<0)A=0; if(B>per-1)B=per-1; return A<=B; }
static const i64 KCMOD=0x4ef145;
int main(){ load();
  // ---------- (1) baseline against jwin18 ----------
  { int sc18=0,sc17=0,sc20=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KCMOD,dx)+K)>>23;
      i64 J18=(arr(I1,dx)+K)>>18, J17=(arr(I1,dx)+K)>>17, J20=(arr(I1,dx)+K)>>20;
      int i=t+512; if(J18>=JL[i]&&J18<=JH[i])sc18++; if(J17>=JL[i]&&J17<=JH[i])sc17++; if(J20>=JL[i]&&J20<=JH[i])sc20++; }
    auto Jof=[&](int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KCMOD,dx)+K)>>23; return (arr(I1,dx)+K)>>18; };
    printf("(1) baseline model w=13 s1=23 s2=18 KC=0x4EF145 K1=K2=2^19\n");
    printf("    J(17)   = %lld   (claimed 45630, window [%lld,%lld])\n",(long long)Jof(17),(long long)JL[529],(long long)JH[529]);
    printf("    J(342)  = %lld   (claimed 18466846, window [%lld,%lld])\n",(long long)Jof(342),(long long)JL[854],(long long)JH[854]);
    printf("    J(-342) = %lld   (claimed 18466857, window [%lld,%lld])\n",(long long)Jof(-342),(long long)JL[170],(long long)JH[170]);
    printf("    jwin18 score at s2=18: %d/1024   (claimed baseline 918) ; s2=17: %d ; s2=20 (coarse, meaningless here): %d\n\n",sc18,sc17,sc20); }
  // ---------- (2)(3)(4) inside the w2_bf corner ----------
  i64 maxrec=0,maxmul=0,maxloss=0; long long badwidth=0, badloss=0;
  printf("(2/3/4) w2_bf corner:\n w  s1  s2 |   h342   h17only   h17any |  max|rec| max|mul| max|loss|\n");
  long long tot17=0,tot342=0,totboth=0;
  for(int w=6;w<=8;w++)for(int s1=12;s1<=14;s1++)for(int s2=15;s2<=16;s2++){
    long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)(1ll<<27))continue;
    long double d=342.0L*powl(2.0L,w); long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)(1ll<<21)/powl(2.0L,s2)+2.0L)+((long double)(1ll<<21)/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    i64 lo=(i64)floorl(((long double)JL[854]-slack)*sc)-8, hi=(i64)ceill(((long double)JH[854]+slack)*sc)+8;
    if(lo<1)lo=1; if(hi>(1ll<<27))hi=1ll<<27; if(hi<lo||hi-lo>60000) continue;
    const i64 d17=(i64)17<<w, dp=(i64)342<<w, dm=-(i64)342<<w, per1=1ll<<s1;
    long long h342=0,h17=0,hb=0; i64 mr=0,mm=0,ml=0; long long bw=0,bl=0;
    auto A=[&](i64 rec,i64 mul)->i64{ i64 r=arr(rec,mul); i64 ar=rec<0?-rec:rec, am=mul<0?-mul:mul;
        if(ar>mr)mr=ar; if(am>mm)mm=am; if(ar>=(1ll<<27))bw++;
        i64 L=r-rec*mul; i64 aL=L<0?-L:L; if(aL>ml)ml=aL; if(aL>(1ll<<21))bl++; return r; };
    for(i64 kc=lo;kc<=hi;kc++){
      i64 P17=A(kc,d17), Pp=A(kc,dp), Pm=A(kc,dm);
      i64 v17=P17>>s1,r17=P17-(v17<<s1), vp=Pp>>s1,rp=Pp-(vp<<s1), vm=Pm>>s1,rm=Pm-(vm<<s1);
      i64 T17=per1-r17,Tp=per1-rp,Tm=per1-rm; i64 cuts[5]={0,T17,Tp,Tm,per1}; std::sort(cuts,cuts+5);
      for(int c=0;c<4;c++){ i64 a=cuts[c],b=cuts[c+1]; if(b<=a)continue; i64 cnt=b-a;
        i64 i17=v17+(a>=T17), ip=vp+(a>=Tp), im=vm+(a>=Tm); (void)im;
        i64 Qp=A(ip,dp), Q17=A(i17,d17), Qm=A(im,dm); (void)Qm;
        i64 X,Y; bool okp=k2iv(Qp,JL[854],JH[854],s2,X,Y); i64 Xp=X,Yp=Y;
        bool ok17=k2iv(Q17,JL[529],JH[529],s2,X,Y);
        if(okp)h342+=cnt; if(ok17)h17+=cnt;
        if(okp&&ok17&&std::max(Xp,X)<=std::min(Yp,Y)) hb+=cnt; } }
    printf("%2d %3d %3d | %7lld %8lld %8lld | %9lld %8lld %9lld%s%s\n",w,s1,s2,h342,hb?hb:0,h17,
        (long long)mr,(long long)mm,(long long)ml, bw?"  <<OVERWIDE REC>>":"", bl?"  <<LOSS>2^21>>":"");
    tot17+=h17; tot342+=h342; totboth+=hb;
    if(mr>maxrec)maxrec=mr; if(mm>maxmul)maxmul=mm; if(ml>maxloss)maxloss=ml; badwidth+=bw; badloss+=bl; }
  printf("\ntotals: h342 %lld   h17 %lld   hboth %lld\n",tot342,tot17,totboth);
  printf("max|rec| %lld (limit 2^27 = %lld, over-width cases %lld)\n",(long long)maxrec,(long long)(1ll<<27),badwidth);
  printf("max|mul| %lld ; max|loss| %lld (bracket assumes <= 2^21 = %lld, violations %lld)\n",
      (long long)maxmul,(long long)maxloss,(long long)(1ll<<21),badloss);
  return 0; }
