// rt51_sym.cpp -- MODEL-FREE: is the even term symmetric in t?  T2(n,t) interval from the mirror pair:
//   2 (C0_n - T2) in [l1 + l2, h1 + h2]   (windows of (n,t) and (32-n,t));   word 16: single window.
// Count the (word, |t|) whose intervals at +t and -t are DISJOINT (-> T2(-t) != T2(t), a signed dx reaches the even path
// through a truncation), and the size of the forced difference.  C0 is the hardware's own t = 0 value, so it cancels.
#include "base2.h"
static Fsca F;
int main(){ if(!load_fsca(F))return 1; build_tables(); int disj=0,tot=0; int byband[8]={0},nband[8]={0};
    for(int n=1;n<=16;n++)for(int t=1;t<=511;t++){ i64 lo[2],hi[2];
        for(int s=0;s<2;s++){ int tt=s?-t:t; auto win=[&](int m,i64&l,i64&h){ int a=(m*1024+tt)&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){l=w.lo;h=w.hi;} else {l=-w.hi;h=-w.lo;} };
            i64 l1,h1,l2,h2; win(n,l1,h1); if(n<16)win(32-n,l2,h2); else {l2=l1;h2=h1;} lo[s]=l1+l2; hi[s]=h1+h2; }   // interval of 2(C0 - T2)
        tot++; nband[t/64]++; if(hi[0]<lo[1]||hi[1]<lo[0]){ disj++; byband[t/64]++; i64 gap=hi[0]<lo[1]?lo[1]-hi[0]:-(lo[0]-hi[1]); if(disj<=40)printf("   word %2d |t| %3d : 2(C0-T2) at +t in [%lld,%lld], at -t in [%lld,%lld]  -> T2(-t) - T2(+t) %s %.1f\n",n,t,(long long)lo[0],(long long)hi[0],(long long)lo[1],(long long)hi[1],gap>0?"<= -":">= +",fabs((double)gap)/2); } }
    printf("%d of %d (word,|t|) pairs have DISJOINT even-part intervals\n",disj,tot); for(int b=0;b<8;b++)printf("   |t| in [%3d,%3d): %d of %d\n",b*64,b*64+64,byband[b],nband[b]); return 0; }
