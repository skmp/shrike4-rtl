// rt46_joint.cpp -- FSRRA lesson (fsrra/tools rr10..rr14): TWO things were wrong at once there (a normalised field that is
// really fixed-point; a chain reading a different latch), and the second only showed after the first was fixed.  All FSCA
// even-path scans so far varied ONE thing against an otherwise fixed model.  Joint scan, FSRRA-proven primitives only
// (arr, K = 2^19, product register >> 20 then a feedback shift, sm aligner):
//   J-stage, form B (square first, the FSRRA form):  sq = (64 t^2) >> g0 ;  J = (arr(KCf<<pk, sq) | swapped  + K) >> (20+g2)
//   J-stage, form A (constant first, the model):     I1 = (arr(KCf<<pk, dx) | swapped + K) >> (20+g1) ; J = (arr(I1,dx) | swapped + K) >> (20+g2)
//        KCf = RN(0x4ef145 / 2^(18-fk)) + dk   (fk fraction bits, dk = -1..1)
//   S-stage:  S normalised 23-bit (the model) or FIXED-POINT RN(sin * 2^F), F = 18..26, at placement ps, either role;
//        (mode 99 = the hardware's own 26-bit C0 as the sine operand, the patent's merged form)
//        aligner shift a = F + ps + eJ - 14  (eJ = J's scale: J = 2 pi^2 t^2 2^eJ)
// Identical J arrays are merged.  Score = violations on the full model (nodes 1..16 + mirrors), guard = words 1,2,16.
#include "base2.h"
#include <algorithm>
#include <map>
#include <string>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { i64 base, lo, hi; }; static Cell CELL[32][1024];
struct JC { int eJ; std::vector<i64> J; std::string desc; };
struct SC { int mode; int ps; int r; };   // mode 0 = normalised, else F
struct Res { int viol, guard; int ji, si; };
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int m=0;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} CELL[m][t+512]={C0TAB[n]+s*T1-s*T3,lo,hi}; } }
    std::map<std::string,int> seen; std::vector<JC> JS; char buf[256];
    auto add=[&](int eJ,std::vector<i64>&J,const char*d){ std::string key((const char*)J.data(),J.size()*8); key+=(char)eJ; if(seen.count(key))return; seen[key]=1; JS.push_back({eJ,J,d}); };
    auto inr=[&](i64 v,bool rec){ i64 lim=rec?(1ll<<27):(1ll<<24); return v<lim&&v>=-lim; };
    for(int fk=6;fk<=18;fk++)for(int dk=-1;dk<=1;dk++){ i64 kcf=((0x4ef145ll+(1ll<<(18-fk)>>1))>>(18-fk))+dk;
        for(int pk=0;pk<=14;pk++){ i64 kc=kcf<<pk; if(kc>=(1ll<<27))break;
            for(int r=0;r<2;r++){ if(!inr(kc,r==0))continue;
                // form B
                for(int g0=0;g0<=6;g0++)for(int g2=0;g2<=6;g2++){ int eJ=fk+pk+6-g0-20-g2; if(eJ<-3||eJ>4)continue; std::vector<i64> J(1024); bool ok=true;
                    for(int t=-512;t<=511&&ok;t++){ i64 dx=(i64)t<<13; i64 sq=((arr(dx,dx)+K)>>20)>>g0; if(!inr(sq,r==1)){ok=false;break;} i64 P=r?arr(sq,kc):arr(kc,sq); J[t+512]=(P+K)>>(20+g2); }
                    if(ok){ snprintf(buf,sizeof buf,"B fk %2d dk %+d pk %2d sqrec %d g0 %d g2 %d",fk,dk,pk,r,g0,g2); add(eJ,J,buf); } }
                // form A
                for(int g1=0;g1<=6;g1++)for(int g2=0;g2<=6;g2++)for(int r2=0;r2<2;r2++){ int eJ=fk+pk-14-g1-g2; if(eJ<-3||eJ>4)continue; std::vector<i64> J(1024); bool ok=true;
                    for(int t=-512;t<=511&&ok;t++){ i64 dx=(i64)t<<13; i64 P=r?arr(dx,kc):arr(kc,dx); i64 I1=(P+K)>>(20+g1); if(!inr(I1,r2==0)){ok=false;break;} i64 P2=r2?arr(dx,I1):arr(I1,dx); J[t+512]=(P2+K)>>(20+g2); }
                    if(ok){ snprintf(buf,sizeof buf,"A fk %2d dk %+d pk %2d dxrec1 %d g1 %d dxrec2 %d g2 %d",fk,dk,pk,r,g1,r2,g2); add(eJ,J,buf); } } } } }
    std::vector<SC> SS; SS.push_back({0,0,0}); SS.push_back({0,0,1}); for(int ps=1;ps<=4;ps++)SS.push_back({0,ps,1});
    for(int Fb=18;Fb<=26;Fb++)for(int ps=0;Fb+ps<=27;ps++)for(int r=0;r<2;r++){ if(r==0&&Fb+ps>24)continue; SS.push_back({Fb,ps,r}); }
    for(int ps=0;ps<=1;ps++)SS.push_back({99,ps,1});
    printf("%zu distinct J arrays x %zu S forms\n",JS.size(),SS.size());
    static i64 SFIX[27][17]; for(int Fb=18;Fb<=26;Fb++)for(int n=1;n<=16;n++){ i128 s=sin_(PI_()*n/32); SFIX[Fb][n]=rn_scale(s,Fb); }
    std::vector<Res> R(JS.size()*SS.size());
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t ri=0;ri<R.size();ri++){ int ji=ri/SS.size(), si=ri%SS.size(); const JC&jc=JS[ji]; const SC&sc=SS[si]; int v=0,gd=0; bool bad=false;
        for(int n=1;n<=16&&!bad;n++){ i64 S; int Fb; if(sc.mode==0){ S=TB[n].S; Fb=23+(TB[n].u2-30); } else if(sc.mode==99){ S=C0TAB[n]; Fb=26; } else { S=SFIX[sc.mode][n]; Fb=sc.mode; } int a=Fb+sc.ps+jc.eJ-14; if(a<0||a>24){bad=true;break;} i64 sp=S<<sc.ps; if(sc.r? !(sp<(1ll<<27)) : !(sp<(1ll<<24)&&true)){ if(sp>=(1ll<<27)){bad=true;break;} }
            for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; for(int t=0;t<1024;t++){ i64 J=jc.J[t]; i64 P=sc.r?arr(sp,J):arr(J,sp); i64 T2=sm(P+K,20+a); const Cell&cl=CELL[m][t]; i64 V=cl.base-T2; if(V<cl.lo||V>cl.hi){ v++; if(n<=2||n==16)gd++; } } if(v>600){bad=true;break;} } }
        R[ri]={bad?(1<<30):v,gd,ji,si}; }
    std::sort(R.begin(),R.end(),[](const Res&x,const Res&y){ return x.viol!=y.viol?x.viol<y.viol:x.guard<y.guard; });
    puts("best 40:  viol guard | J stage                                            | S stage (mode 0 = normalised 23b, else fixed-point F; ps; S recoded)");
    for(int i=0;i<40;i++){ const Res&r=R[i]; if(r.viol>=(1<<30))break; printf("   %5d %5d | %-50s eJ %+d | F %2d ps %d rec %d\n",r.viol,r.guard,JS[r.ji].desc.c_str(),JS[r.ji].eJ,SS[r.si].mode,SS[r.si].ps,SS[r.si].r); }
    int shown=0; puts("best guard-clean:"); for(auto&r:R){ if(r.viol>=(1<<30))break; if(r.guard==0){ printf("   %5d %5d | %-50s eJ %+d | F %2d ps %d rec %d\n",r.viol,r.guard,JS[r.ji].desc.c_str(),JS[r.ji].eJ,SS[r.si].mode,SS[r.si].ps,SS[r.si].r); if(++shown>=15)break; } }
    return 0; }
