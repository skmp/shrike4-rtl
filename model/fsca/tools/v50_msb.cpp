// v50_msb.cpp -- independent, model-free test of "t has one more MSB" (= coarser node split).
// Certificate: for ANY cubic p and any h,  p(c-2h) - 4p(c-h) + 6p(c) - 4p(c+h) + p(c+2h) = 0.
// The dump pins every sample to an integer window [lo,hi] in units of 2^-26, so the achievable
// interval of that combination is computed EXACTLY with interval arithmetic. 0 outside it => no
// cubic (freely fitted REAL coefficients) can pass through all five windows => that span is not
// one cubic piece.  Slack = sum |coef_i| * width_i, so |D|/slackbound is the miss in units.
#include "v50_core.h"
#include <algorithm>
#include <string>
struct Res { int nexcl, ncent; i64 worst; int worstc; long double bound; long long maxslack; };
// stencil validity: all five offsets must lie inside one node of span N centred on a multiple of N
static Res cert(int N, int h, int centre_off){
    Res r{0,0,0,0,0.0L,0};
    int half = N/2;
    if (2*h > half || -2*h < -half) { /* t=+2h must satisfy 2h <= half-1 */ }
    bool valid = (2*h <= half-1) && (2*h <= half);
    for (int k = 0; k*N + centre_off < 65536; k++){
        int c = k*N + centre_off;
        static const int co[5] = {1,-4,6,-4,1};
        i64 dlo = 0, dhi = 0; long long slack = 0;
        for (int i = 0; i < 5; i++){
            int a = (c + (i-2)*h) & 0xffff; i64 lo, hi; v50_win(a, lo, hi);
            if (co[i] > 0){ dlo += co[i]*lo; dhi += co[i]*hi; } else { dlo += co[i]*hi; dhi += co[i]*lo; }
            slack += (long long)(co[i]<0?-co[i]:co[i]) * (hi - lo);
        }
        r.ncent++;
        i64 miss = 0;
        if (dlo > 0) miss = dlo; else if (dhi < 0) miss = -dhi;
        if (miss > 0) r.nexcl++;
        if (miss > r.worst){ r.worst = miss; r.worstc = c; }
        if (slack > r.maxslack) r.maxslack = slack;
    }
    r.bound = (long double)r.worst / 16.0L;
    (void)valid;
    return r;
}
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables();
    // window widths actually present in the dump
    { long long wmax=0, hist[8]={0}; for(int a=0;a<65536;a++){ i64 lo,hi; v50_win(a,lo,hi); long long w=hi-lo;
        if(w>wmax)wmax=w; if(w<8)hist[w]++; }
      printf("dump window widths (hi-lo): max %lld ; counts  w=0:%lld w=1:%lld w=2:%lld w=3:%lld\n\n",
             wmax,hist[0],hist[1],hist[2],hist[3]); }
    printf("%-6s %-5s %-6s %-9s %-10s %-9s %-11s %s\n",
           "Nspan","h","stencil","centres","excl(0 out)","worst|D|","miss(units)","worst centre");
    struct Cs { int N,h,off; const char* note; };
    Cs cs[] = {
        {1024,128,0,"published split, valid stencil"},
        {1024,170,0,"published split, valid stencil"},
        {1024,255,0,"published split, widest valid"},
        {1024,256,0,"published split, +2h=512 is OUTSIDE the node (invalid)"},
        {1024,512,0,"published split, spans 3 nodes (invalid)"},
        {2048,256,0,"2048 split, valid stencil (straddles 2 published nodes)"},
        {2048,384,0,"2048 split, valid stencil"},
        {2048,511,0,"2048 split, widest valid"},
        {2048,512,0,"2048 split, +2h=1024 is the NEXT node (INVALID stencil)"},
        {2048,256,1024,"2048 split, centres offset by 1024"},
        {2048,511,1024,"2048 split, centres offset by 1024"},
        {2048,512,1024,"2048 split, centres offset by 1024 (invalid stencil)"},
        {4096,1023,0,"4096 split, widest valid"},
        {4096,512,0,"4096 split, valid stencil"},
    };
    for (auto& s : cs){
        Res r = cert(s.N, s.h, s.off);
        printf("%-6d %-5d %-6s %-9d %-11s %-9lld %-11.3Lf %-7d  %s\n",
               s.N, s.h, (2*s.h <= s.N/2 - 1) ? "valid" : "INVAL",
               r.ncent, (std::to_string(r.nexcl) + "/" + std::to_string(r.ncent)).c_str(),
               (long long)r.worst, r.bound, r.worstc, s.note);
    }
    puts("\n-- scale check: the missing quartic's Chebyshev economisation, sin''''/4! * h^4 / 2^3 in 2^-26 units --");
    for (int hw : {256,512,1024,2048}){
        long double x = (long double)hw * 3.14159265358979323846264L / 32768.0L;   // half-width in radians
        long double e = powl(x,4)/24.0L/8.0L * 67108864.0L;   // |sin''''|<=1 ; Chebyshev economisation factor 1/2^(2*4-1)
        printf("   half-width %4d angles -> %.4Lf units of 2^-26\n", hw, e);
    }
    return 0; }
