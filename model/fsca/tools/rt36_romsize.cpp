// rt36_romsize.cpp -- companion of rt35: (1) FEWER entries: a term's field shared between adjacent nodes (half-size ROM);
// (2) how finely does the data resolve each field?  violations when the field of EVERY node is moved by +-d, and when
// its low b bits are dropped (a narrower ROM word).  Baseline 148 / 0 guard.
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 Jc[1024],A3c[1024]; static i64 LO[32][1024],HI[32][1024];
struct Tb { i64 C0,C1,c3,S; };
static int score(const Tb* tb,int* guard){ int v=0,g=0; for(int m=1;m<32;m++){ int flip=m>=17; int n=flip?32-m:m; const Tab&T=TB[n]; i64 s=flip?-1:1; bool gd=(m<=2||m==16||m>=30);
        for(int i=0;i<1024;i++){ i64 dx=(i64)(i-512)<<13; i64 T1=tb[n].C1>0? sm(arr(tb[n].C1,dx)+K,23-T.e1):0; i64 T3=tb[n].c3>0? sm(arr(tb[n].c3<<5,A3c[i])+K,26):0; i64 T2=tb[n].S>0? sm(arr(Jc[i],tb[n].S)+K,T.u2):0;
            i64 V=tb[n].C0+s*T1-T2-s*T3; if(V<LO[m][i]||V>HI[m][i]){v++; if(gd)g++;} } } *guard=g; return v; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; A3c[t+512]=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; }
    for(int m=1;m<32;m++)for(int i=0;i<1024;i++){ int a=(m*1024+(i-512))&0xffff; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){LO[m][i]=w.lo;HI[m][i]=w.hi;} else {LO[m][i]=-w.hi;HI[m][i]=-w.lo;} }
    Tb base[17]; for(int n=0;n<=16;n++) base[n]={C0TAB[n],TB[n].C1f,TB[n].c3,TB[n].S}; int g; { int v=score(base,&g); printf("baseline: %d (guard %d)\n\n",v,g); }
    const char* tn[]={"C0","C1F","c3","S"};
    puts("(1) HALF-SIZE ROM: the field of node n taken from node n&~1 (even nodes only)");
    for(int term=0;term<4;term++){ Tb tb[17]; for(int n=0;n<=16;n++){ tb[n]=base[n]; int e=n&~1; if(term==0)tb[n].C0=base[e].C0; if(term==1)tb[n].C1=base[e].C1; if(term==2)tb[n].c3=base[e].c3; if(term==3)tb[n].S=base[e].S; } int v=score(tb,&g); printf("    %-4s shared by node pairs : %6d violations\n",tn[term],v); }
    puts("\n(2) RESOLVING POWER: every node's field moved by +d / -d");
    for(int term=0;term<4;term++){ printf("    %-4s:",tn[term]); for(int d: {1,2,4,8,16,32}){ int vv[2]; for(int sgn=0;sgn<2;sgn++){ Tb tb[17]; for(int n=0;n<=16;n++){ tb[n]=base[n]; i64 dd=sgn?-d:d; if(term==0&&n>0)tb[n].C0+=dd; if(term==1&&base[n].C1)tb[n].C1+=dd; if(term==2&&base[n].c3)tb[n].c3+=dd; if(term==3&&base[n].S)tb[n].S+=dd; } vv[sgn]=score(tb,&g); } printf("  +-%d: %d/%d",d,vv[0],vv[1]); } puts(""); }
    puts("\n(3) NARROWER WORD: low b bits of the field forced to zero (truncated ROM word)");
    for(int term=0;term<4;term++){ printf("    %-4s:",tn[term]); for(int b=1;b<=4;b++){ Tb tb[17]; i64 mk=~((1ll<<b)-1); for(int n=0;n<=16;n++){ tb[n]=base[n]; if(term==0)tb[n].C0&=mk; if(term==1)tb[n].C1&=mk; if(term==2)tb[n].c3&=mk; if(term==3)tb[n].S&=mk; } printf("  %d bit%s: %d",b,b>1?"s":"",score(tb,&g)); } puts(""); }
    return 0; }
