// rt35_subrom.cpp -- USER HYPOTHESIS: the ROMs of the different terms do not all have the same number of entries.
// Same-centre variant: a term's ROM is addressed by EXTRA angle bits, i.e. each node is split into G = 2^j CONTIGUOUS
// sub-ranges of t and the term's field may differ (in its low bits) per sub-range.  DIAGNOSTIC FIT with a CONTROL:
// the same number of free values assigned to a RANDOM partition of t into G groups.  Extra freedom always helps; a real
// breakpoint structure shows as contiguous >> random.
//   which term gets the finer ROM:   S  (quadratic field, +-24 LSB)   |  C0 (+-2 units)   |  C1F (+-6 LSB)   |  c3 (+-4)
// Each physical node (1..31) is fitted on its own; J is the chain.  Baseline 148.
#include "base2.h"
#include <random>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables(); std::mt19937 rng(7);
    static i64 Jc[1024],A3c[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; A3c[t+512]=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; }
    static i64 LO[32][1024],HI[32][1024]; for(int m=1;m<32;m++)for(int i=0;i<1024;i++){ int a=(m*1024+(i-512))&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){LO[m][i]=w.lo;HI[m][i]=w.hi;} else {LO[m][i]=-w.hi;HI[m][i]=-w.lo;} }
    const char* tn[]={"S   (quadratic field)","C0  (constant)","C1F (linear field)","c3  (cubic field)"}; int rad[]={24,2,6,4};
    puts("term with the finer ROM      sub-ranges |  contiguous   random partition (mean of 5)   | guard-node violations (contiguous)");
    for(int term=0;term<4;term++) for(int j=0;j<=4;j++){ int G=1<<j; double rnd_mean=0; int contig=0,cg=0;
        for(int rep=0;rep<=5;rep++){ if(j==0&&rep>0)break; int total=0,gd=0; std::vector<int> grp(1024); if(rep==0) for(int i=0;i<1024;i++)grp[i]=i>>(10-j); else { for(int i=0;i<1024;i++)grp[i]=i%G; std::shuffle(grp.begin(),grp.end(),rng); }
            for(int m=1;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1; if(!T.S&&term==0)continue;
                for(int g=0;g<G;g++){ int best=1<<30; for(int d=-rad[term];d<=rad[term];d++){ int v=0;
                        for(int i=0;i<1024;i++){ if(grp[i]!=g)continue; int t=i-512; i64 dx=(i64)t<<13; i64 C1=T.C1f+(term==2?d:0); i64 c3=T.c3+(term==3?d:0); i64 Sv=T.S+(term==0?d:0); i64 C0=C0TAB[n]+(term==1?d:0);
                            if(term==2&&!T.C1f)C1=0; if(term==3&&!T.c3)c3=0;
                            i64 T1=C1? sm(arr(C1,dx)+K,23-T.e1):0; i64 T3=c3? sm(arr(c3<<5,A3c[i])+K,26):0; i64 T2=Sv>0? sm(arr(Jc[i],Sv)+K,T.u2):0; i64 V=C0+s*T1-T2-s*T3; if(V<LO[m][i]||V>HI[m][i])v++; }
                        if(v<best)best=v; }
                    total+=best; if(m<=2||m==16||m>=30)gd+=best; } }
            if(rep==0){contig=total;cg=gd;} else rnd_mean+=total/5.0; }
        if(j==0) printf("%-28s     1     |    %4d            --                          |   %d\n",tn[term],contig,cg);
        else     printf("%-28s   %3d     |    %4d          %6.1f                        |   %d\n",tn[term],G,contig,rnd_mean,cg); }
    return 0; }
