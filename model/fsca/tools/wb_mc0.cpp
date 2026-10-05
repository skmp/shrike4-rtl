// wb_mc0.cpp -- the merged form's prediction for C0.  If the even part is ONE product
// E = floor((arr(2^w - Y, Sfield) + Kr)/2^u) then at t=0 (Y=0) it is Sfield << (w-u), i.e. the table field
// shifted by its octave.  Test that against the exact t=0 windows for sine fields of width 23..26.
#include "wb_even.h"
#include <string>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(){ if(!load_fsca(F))return 1; build_tables();
    i64 LO[17],HI[17];
    for(int n=0;n<=16;n++){ i64 l1,h1,l2,h2;bool e1,e2; win_of((n*1024)&0xffff,l1,h1,e1);
        if(n>0&&n<16) win_of(((32-n)*1024)&0xffff,l2,h2,e2); else {l2=-1ll<<60;h2=1ll<<60;}
        LO[n]=std::max(l1,l2); HI[n]=std::min(h1,h2); }
    puts("width  fails/16  node:field<<sh vs window");
    for(int W=23;W<=26;W++){ int fails=0; std::string det;
        for(int n=1;n<=16;n++){ i128 a=PI_()*n/32; i128 s=sin_(a);
            int k=0; while(s*((i128)1<<(k+1))<QONE) k++;         // sin in [2^-(k+1),2^-k)
            i64 fld=rn_scale(s,W+k); if(fld>=(1ll<<W)) fld=(1ll<<W)-1;   // normalised W-bit field
            int sh=26-W-k; i64 v = sh>=0? (fld<<sh) : (fld>>(-sh));
            bool ok = v>=LO[n]&&v<=HI[n]; if(!ok){ fails++; char b[64];
                snprintf(b,64,"%d:%lld/%lld..%lld ",n,(long long)v,(long long)LO[n],(long long)HI[n]); det+=b; } }
        printf("%3d   %4d      %s\n",W,fails,det.c_str()); }
    // and the 26-bit C0 operand: E(0) = C0 exactly, so it always matches by construction
    puts("(a 26-bit table field used directly as the multiplicand reproduces C0 at t=0 by construction)");
    return 0; }
