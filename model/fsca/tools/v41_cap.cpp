// v41_cap.cpp -- capability map: the free-loss envelope of J(17) at the CANONICAL split for each w,
// plus the role-swap check (does swapping the array's operand roles widen the envelope?).
#include "base2.h"
#include <cstdio>
#include <algorithm>
static inline i64 fdv(i64 a,i64 b){ i64 q=a/b,r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }
static const i64 LMIN=-655360, LMAX=65536;
static void env(int t,int w,int s1,int s2,i64 kc,i64& jlo,i64& jhi,i64& n){
    i64 dx=(i64)t<<w, ex1=kc*dx, P1=(i64)1<<s1, P2=(i64)1<<s2;
    i64 a=fdv(ex1+LMIN,P1), b=fdv(ex1+LMAX+P1-1,P1); n=b-a+1;
    jlo=(i64)1<<62; jhi=-((i64)1<<62);
    for(i64 I=a;I<=b;I++){ i64 e2=I*dx; jlo=std::min(jlo,fdv(e2+LMIN,P2)); jhi=std::max(jhi,fdv(e2+LMAX+P2-1,P2)); }
}
int main(){ build_tables();
    printf("CANONICAL SPLIT s=(23+(w-13), 18+(w-13)), KC=0x%llx, loss free in [%lld,%lld], K free:\n",(long long)KC,(long long)LMIN,(long long)LMAX);
    printf("   w  (s1,s2)   |dx|max@t=512   J(17) envelope        45617 inside?   model J(17)\n");
    for(int w=2;w<=20;w++){ int s1=23+(w-13), s2=18+(w-13); if(s1<1||s2<1) continue;
        i64 jl,jh,n; env(17,w,s1,s2,KC,jl,jh,n);
        i64 dx=(i64)17<<w; i64 I1=(arr(KC,dx)+K)>>s1; i64 Jm=(arr(I1,dx)+K)>>s2;
        printf("  %2d  (%2d,%2d)   %11lld   [%8lld,%8lld]   %-6s          %lld\n",
            w,s1,s2,(long long)((i64)512<<w),(long long)jl,(long long)jh,
            (jl<=45617&&45617<=jh)?"YES":"NO",(long long)Jm); }
    printf("\nROLE SWAP at w=13,(23,18): the exact product is the same and the loss bound is role-independent,\n");
    { int w=13,s1=23,s2=18; i64 dx=(i64)17<<w;
      i64 A=arr(KC,dx), B=arr(dx,KC); printf("  arr(KC,dx)=%lld  arr(dx,KC)=%lld  (differ by %lld)\n",(long long)A,(long long)B,(long long)(B-A));
      i64 I1a=(A+K)>>s1, I1b=(B+K)>>s1;
      printf("  I1 = %lld / %lld ; J = %lld / %lld / %lld / %lld  (4 role combinations)\n",
        (long long)I1a,(long long)I1b,
        (long long)((arr(I1a,dx)+K)>>s2),(long long)((arr(dx,I1a)+K)>>s2),
        (long long)((arr(I1b,dx)+K)>>s2),(long long)((arr(dx,I1b)+K)>>s2)); }
    // how many (s1,s2) pairs at w=13 are "capable" at t=17 (envelope contains 45617), with KC rescaled
    { int w=13,cap=0,tot=0,mins2=99;
      for(int s1=6;s1<=34;s1++) for(int s2=6;s2<=34;s2++){
        int e=s1+s2-2*w; if(e<0||e>40) continue;
        long double kcs=45617.0L*powl(2.0L,(long double)e)/289.0L; if(kcs<512.0L||kcs>1.6e7L) continue;
        i64 kc=(i64)llroundl(kcs); i64 jl,jh,n; env(17,w,s1,s2,kc,jl,jh,n); tot++;
        if(jl<=45617&&45617<=jh){ cap++; if(s2<mins2) mins2=s2; if(s2>=16) printf("  CAPABLE with s2>=16: (s1,s2)=(%d,%d) KC=%lld env [%lld,%lld]\n",s1,s2,(long long)kc,(long long)jl,(long long)jh); } }
      printf("\nat w=13 with KC rescaled to the split: %d of %d (s1,s2) pairs are capable at t=17; smallest capable s2 seen = %d\n",cap,tot,mins2); }
    return 0; }
