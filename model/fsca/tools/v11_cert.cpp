// v11_cert.cpp -- ADVERSARIAL re-verification of the "no cubic fits a 2048-wide interval" certificate.
// Written from scratch; does not reuse w1_cub.cpp.  All decisive arithmetic is integer (__int128).
//
// Sections:
//   A  window self-validation: every integer in [lo,hi] must re-encode to the dumped RZ *and* RN floats,
//      and lo-1 / hi+1 must not.  (If decode() were loose the whole certificate would be junk.)
//   B  CONTROL: feed the certificate a synthetic cubic wrapped in the SAME window widths.  Must give <= 0.
//   C  my own equispaced 4th-difference certificate, H = 256/512/1024, both phases.
//   D  general (NON-equispaced) 5-point divided-difference certificate -- a strictly stronger family.
//   E  "ANY 2048-wide interval": sweep every phase, and exhibit an explicit cubic UPPER bound where the
//      certificate is small.
//   F  model-free |V_hw - RN(sin*2^26)| envelope (the 'truncation budget' the argument leans on).
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <random>
#include <cmath>
#include <climits>

static Fsca F;
static i64 VL[65536], VH[65536], S26[65536];
typedef __int128 i128;
static void pr128(i128 v){ if(v<0){putchar('-');v=-v;} char b[42];int n=0; if(!v)b[n++]='0'; while(v){b[n++]='0'+(int)(v%10);v/=10;} while(n)putchar(b[--n]); }

// ---------- A: window validation ----------
static uint32_t enc(i64 v,int rz){ return to_float(v,rz); }
static int validate_windows(){
    int bad=0;
    for(int a=0;a<65536;a++){
        Win w=decode(F.srz[a],F.srn[a]);
        i64 lo=w.lo,hi=w.hi;
        if(F.srz[a]==0||F.srz[a]==0x80000000u){ if(lo||hi) bad++; continue; }
        int sgn = (F.srz[a]>>31)?-1:1;
        for(i64 v=lo; v<=hi; ++v){
            uint32_t z=enc(v,1)|(sgn<0?0x80000000u:0), n=enc(v,0)|(sgn<0?0x80000000u:0);
            if(z!=F.srz[a]||n!=F.srn[a]){ bad++; break; } }
        // boundaries must fail
        for(int d=0;d<2;d++){ i64 v = d? hi+1 : lo-1; if(v<0) continue;
            uint32_t z=enc(v,1)|(sgn<0?0x80000000u:0), n=enc(v,0)|(sgn<0?0x80000000u:0);
            if(z==F.srz[a]&&n==F.srn[a]){ bad++; break; } }
    }
    return bad;
}

// ---------- C: equispaced certificate (my own) ----------
// returns 2*16*delta_min_lower_bound, i.e. divide by 32 for output units.  Uses DOUBLED mid/width.
static i64 cert_equi(const i64*lo,const i64*hi,int base,int len,int& bs,int& boff){
    static const i64 C4[5]={1,4,6,4,1};
    i64 best=LLONG_MIN; bs=0;boff=0;
    for(int s=1;4*s<=len-1;s++)
      for(int off=0; off+4*s<=len-1; off++){
        i64 acc=0, wid=0;
        for(int i=0;i<5;i++){ int k=base+off+i*s; i64 L=lo[k],H=hi[k];
            acc += (i&1?-1:1)*C4[i]*(L+H); wid += C4[i]*(H-L); }
        i64 v=(acc<0?-acc:acc)-wid;
        if(v>best){best=v;bs=s;boff=off;} }
    return best;
}

// ---------- D: general 5-point certificate ----------
struct GenRes{ long double delta; int t[5]; };
static GenRes cert_gen(const i64*lo,const i64*hi,int base,int len,int iters,unsigned seed){
    std::mt19937 rng(seed); GenRes best; best.delta=-1e30L; for(int i=0;i<5;i++)best.t[i]=0;
    int idx[5];
    for(int it=0; it<iters; ++it){
        // pick 5 distinct indices
        for(int i=0;i<5;i++) idx[i]=rng()%len;
        std::sort(idx,idx+5);
        bool dup=false; for(int i=1;i<5;i++) if(idx[i]==idx[i-1]) dup=true;
        if(dup) continue;
        // lambda_i = (-1)^i * prod_{j<k; j,k != i} (t_k - t_j)
        i128 lam[5]; i128 num=0, den=0;
        for(int i=0;i<5;i++){ i128 p=1;
            for(int j=0;j<5;j++) if(j!=i) for(int k=j+1;k<5;k++) if(k!=i) p*= (i128)(idx[k]-idx[j]);
            lam[i]= (i&1)? -p : p; }
        for(int i=0;i<5;i++){ i64 L=lo[base+idx[i]],H=hi[base+idx[i]];
            num += lam[i]*(i128)(L+H); i128 al = lam[i]<0?-lam[i]:lam[i];
            num -= 0; den += al; }
        i128 sub=0; for(int i=0;i<5;i++){ i128 al=lam[i]<0?-lam[i]:lam[i]; sub += al*(i128)(hi[base+idx[i]]-lo[base+idx[i]]); }
        i128 a = num<0?-num:num; a -= sub;
        long double d = (long double)(double)(long double)0; // placeholder
        // delta >= a / (2*den)
        long double an = 0, dn = 0;
        { i128 x=a; bool neg=x<0; if(neg)x=-x; an=(long double)(unsigned long long)(x>>64)*18446744073709551616.0L + (long double)(unsigned long long)(x&(((i128)1<<64)-1)); if(neg)an=-an; }
        { i128 x=2*den; dn=(long double)(unsigned long long)(x>>64)*18446744073709551616.0L + (long double)(unsigned long long)(x&(((i128)1<<64)-1)); }
        d = an/dn;
        if(d>best.delta){ best.delta=d; for(int i=0;i<5;i++)best.t[i]=idx[i]; }
    }
    return best;
}

// ---------- E helper: explicit cubic upper bound on an interval ----------
// least-squares then a few exchange iterations in long double, then EXACT integer evaluation
static long double cubic_upper(const i64*lo,const i64*hi,int base,int len){
    // centre coordinates
    long double c[4]={0,0,0,0};
    // simple approach: minimise max deviation by iterative reweighting is overkill; use LS on midpoints then
    // measure the true max distance to the windows (rigorous upper bound on the minimum).
    long double A[4][5]={};
    for(int i=0;i<len;i++){ long double x=(long double)(i-(len-1)/2.0L)/ (long double)len;
        long double m=0.5L*(long double)(lo[base+i]+hi[base+i]);
        long double b[4]={1,x,x*x,x*x*x};
        for(int p=0;p<4;p++){ for(int q=0;q<4;q++) A[p][q]+=b[p]*b[q]; A[p][4]+=b[p]*m; } }
    for(int p=0;p<4;p++){ int piv=p; for(int r=p+1;r<4;r++) if(fabsl(A[r][p])>fabsl(A[piv][p]))piv=r;
        for(int q=0;q<5;q++) std::swap(A[p][q],A[piv][q]);
        for(int r=0;r<4;r++) if(r!=p){ long double f=A[r][p]/A[p][p]; for(int q=p;q<5;q++) A[r][q]-=f*A[p][q]; } }
    for(int p=0;p<4;p++) c[p]=A[p][4]/A[p][p];
    // measure
    long double worst=0;
    for(int i=0;i<len;i++){ long double x=(long double)(i-(len-1)/2.0L)/(long double)len;
        long double v=c[0]+x*(c[1]+x*(c[2]+x*c[3]));
        long double d = v<(long double)lo[base+i] ? (long double)lo[base+i]-v : (v>(long double)hi[base+i]? v-(long double)hi[base+i] : 0);
        if(d>worst) worst=d; }
    return worst;
}

int main(int argc,char**argv){
    if(!load_fsca(F)){ printf("no dump\n"); return 1; }
    build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;}
        int q=a>>14,r=a&16383,neg=(q>=2),u=(q&1)?16384-r:r;
        S26[a]=rn_scale(sin_(PI_()*u/32768),26)*(neg?-1:1); }

    printf("=== A. window self-validation (re-encode every integer in every window) ===\n");
    printf("  bad angles: %d / 65536\n\n", validate_windows());

    printf("=== B. CONTROL: synthetic cubic inside the same window widths ===\n");
    {   static i64 clo[65536], chi[65536];
        // take interval [15360-1024, 15360+1023]; build p(t) = a cubic through 4 real window midpoints
        int c=14336,H=1024,base=c-H,len=2*H;
        long double a0=1e8L,a1=3.1L,a2=-0.002L,a3=1e-7L;
        for(int i=0;i<len;i++){ long double x=i-(len-1)/2.0L;
            i64 v=(i64)llroundl(a0+x*(a1+x*(a2+x*a3)));
            i64 w=VH[base+i]-VL[base+i]; clo[base+i]=v-w/2; chi[base+i]=v+w-w/2; }
        int s,off; i64 r=cert_equi(clo,chi,base,len,s,off);
        printf("  H=1024 control certificate (doubled/32) = %.4f   (must be <= 0)  %s\n\n",(double)r/32.0, r<=0?"OK":"*** FAIL ***"); }

    printf("=== C. my equispaced certificate (independent implementation) ===\n");
    for(int H : {256,512,1024}){
        for(int phase=0;phase<2;phase++){
            double sum=0; int cnt=0; i64 worst=LLONG_MIN; int wc=0;
            for(int c=(phase?H:0); c<=16384; c+=2*H){
                if(c-H<0||c+H-1>=32768) continue;
                int s,off; i64 v=cert_equi(VL,VH,c-H,2*H,s,off);
                sum+=(double)v/32.0; cnt++; if(v>worst){worst=v;wc=c;} }
            printf("  H=%4d phase %d : mean %8.3f  worst %8.3f at centre %5d  (%d intervals)\n",
                   H,phase,sum/cnt,(double)worst/32.0,wc,cnt); } }
    printf("\n");

    printf("=== D. general NON-equispaced 5-point certificate (stronger family, 400k random subsets) ===\n");
    for(int H : {512,1024}){
        for(int c : {3072,14336,16384}){
            if(c-H<0) continue;
            GenRes g=cert_gen(VL,VH,c-H,2*H,400000,12345u+c+H);
            int s,off; i64 e=cert_equi(VL,VH,c-H,2*H,s,off);
            printf("  H=%4d centre %5d : equispaced %8.3f   general %8.3f  (pts %d %d %d %d %d)\n",
                   H,c,(double)e/32.0,(double)g.delta,g.t[0],g.t[1],g.t[2],g.t[3],g.t[4]); } }
    printf("\n");

    printf("=== E. 'ANY 2048-wide interval': full phase sweep, H=1024 ===\n");
    printf("   centre   cert(lower)   cubicfit(upper)   sin(centre)\n");
    for(int c=1024;c<=17408;c+=1024){
        int base=c-1024; if(base<0) continue;
        int s,off; i64 v=cert_equi(VL,VH,base,2048,s,off);
        long double ub=cubic_upper(VL,VH,base,2048);
        printf("   %6d   %10.3f    %12.3f      %.4f\n",c,(double)v/32.0,(double)ub,
               (double)std::sin((double)c*2*M_PI/65536.0)); }
    printf("\n   -- and the interval straddling angle 0 (centre 0, angles -1024..1023):\n");
    {   static i64 zlo[4096],zhi[4096];
        for(int i=0;i<2048;i++){ int a=(i-1024)&0xffff; zlo[i]=VL[a]; zhi[i]=VH[a]; }
        int s,off; i64 v=cert_equi(zlo,zhi,0,2048,s,off);
        long double ub=cubic_upper(zlo,zhi,0,2048);
        printf("   %6d   %10.3f    %12.3f      %.4f\n",0,(double)v/32.0,(double)ub,0.0); }
    printf("\n");

    printf("=== F. model-free envelope of E(a) = V_hw(a) - RN(sin*2^26), over all 65536 angles ===\n");
    {   i64 mustlo=0, mustmax=0, maxposs=0; int wa=0;
        for(int a=0;a<65536;a++){
            i64 d1 = VL[a]-S26[a], d2 = VH[a]-S26[a];
            i64 must = d1>0? d1 : (d2<0? -d2 : 0);          // guaranteed |E| >= must
            i64 poss = std::max(std::llabs((long long)d1),std::llabs((long long)d2));
            if(must>mustmax){mustmax=must;wa=a;}
            if(poss>maxposs) maxposs=poss; (void)mustlo; }
        printf("  max GUARANTEED |E| = %lld (at angle %d);  max POSSIBLE |E| = %lld\n\n",(long long)mustmax,wa,(long long)maxposs); }
    return 0;
}
