// wd_sq.cpp -- generalise the square path over the multiplication ORDER and the operand ROLES.
// Chain always starts with Kc x dx (established).  Scale law: s1+s2+u = 73+k  (verified below).
//   ord 0 :  I1=(arr1(Kc,dx)+K)>>s1 ; I2=(arr2(I1,dx)+K)>>s2 ; T2=sm(arr3(I2,S)+K,u)   <- one shared latch J
//   ord 1 :  I1=(arr1(Kc,dx)+K)>>s1 ; Wn=(arr2(I1,S )+K)>>s2 ; T2=sm(arr3(Wn,dx)+K,u)  <- per-table latch
// Roles r0,r1,r2 pick which operand is Booth-recoded in each multiply.
#include "base2.h"
#include <cstring>
#include <vector>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{hi=-w.lo;lo=-w.hi;}ex=w.exact;}
struct SP { int ord,s1,s2,r0,r1,r2; i64 kk; };
static inline i64 A(i64 a,i64 b,int role){ return role? arr(b,a) : arr(a,b); }
static inline i64 sq_of(const SP&p,int t,i64 S,int k){
    i64 dx=(i64)t<<13;
    i64 I1=(A(KC,dx,p.r0)+p.kk)>>p.s1;
    int u = 73+k-p.s1-p.s2;
    if(p.ord==0){ i64 I2=(A(I1,dx,p.r1)+p.kk)>>p.s2; return S? sm(A(I2,S,p.r2)+p.kk,u):0; }
    i64 W=(A(I1,S,p.r1)+p.kk)>>p.s2; return S? sm(A(W,dx,p.r2)+p.kk,u):0; }
static i64 C0V[17];
static inline i64 Vof(int n,int t,int flip,const SP&p,int k){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 s=flip?-1:1; return C0V[n]+s*T1-sq_of(p,t,T.S,k)-s*T3; }
static int KK[17];
static int score(const SP&p,int*pernode=nullptr,int*maxW=nullptr){
    int tot=0; if(maxW)*maxW=0;
    for(int n=0;n<=16;n++){ i64 lo0,hi0;bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2; int bv=1<<30; i64 bc=lo0;
        for(i64 cc=lo0-4;cc<=hi0+4;cc++){ C0V[n]=cc; bool ok=true;
            for(int i=0;i<nn;i++){ int m=nodes[i]; i64 l,h;bool e; win_of((m*1024)&0xffff,l,h,e);
                i64 V=Vof(n,0,m>=17,p,KK[n]); if(V<l||V>h)ok=false; }
            if(!ok)continue; int v=0;
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=m>=17;
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 l,h;bool e; win_of(a,l,h,e);
                    i64 V=Vof(n,t,flip,p,KK[n]); if(V<l||V>h)v++; } }
            if(v<bv){bv=v;bc=cc;} }
        C0V[n]=bc; tot+=bv; if(pernode)pernode[n]=bv; }
    if(maxW){ for(int n=1;n<=16;n++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=(A(KC,dx,p.r0)+p.kk)>>p.s1; i64 w = p.ord? ((A(I1,TB[n].S,p.r1)+p.kk)>>p.s2) : ((A(I1,dx,p.r1)+p.kk)>>p.s2);
        i64 aw=w<0?-w:w; if(aw>*maxW)*maxW=(int)aw; } }
    return tot; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int n=0;n<=16;n++){ i128 a=PI_()*n/32,s=sin_(a); int k=0; if(n>0){while(s*((i128)1<<(k+1))<QONE)k++;} KK[n]=k; }
    // sanity: reproduce the baseline
    SP base{0,23,20,0,0,0,1ll<<19}; int pn[17]; int mw;
    printf("baseline (ord0 s1=23 s2=20 roles 000): %d\n",score(base,pn,&mw));
    printf("scan: ord s1 s2 r0 r1 r2 -> total   (u = 73+k-s1-s2)\n");
    int best=1<<30; SP bp=base;
    for(int ord=0;ord<2;ord++) for(int s1=21;s1<=25;s1++) for(int s2=14;s2<=28;s2++){
        int u0=73+0-s1-s2; if(u0<8||u0>45) continue;
        for(int r0=0;r0<2;r0++) for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++){
            SP p{ord,s1,s2,r0,r1,r2,1ll<<19}; int pv[17]; int t=score(p,pv);
            bool guard = pv[0]==0&&pv[1]==0&&pv[2]==0&&pv[16]==0;
            if(guard) printf("  %d %2d %2d  %d %d %d -> %5d   GUARD-OK\n",ord,s1,s2,r0,r1,r2,t);
            if(guard&&t<best){best=t;bp=p;} } }
    printf("best: ord=%d s1=%d s2=%d roles %d%d%d -> %d\n",bp.ord,bp.s1,bp.s2,bp.r0,bp.r1,bp.r2,best);
    int pn2[17]; int mw2; score(bp,pn2,&mw2);
    printf("per-table: "); for(int n=0;n<=16;n++)printf("%d:%d ",n,pn2[n]); printf("\n");
    printf("max |intermediate| = %d\n",mw2);
    return 0; }
