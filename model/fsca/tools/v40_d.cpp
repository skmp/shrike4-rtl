// v40_d.cpp -- EXACT landmark solve for the two-multiply chain at every alignment (branch 40).
// For fixed (w,s1,s2,roles,KC): K1 = K1mod + c1*2^s1 (K1mod from the <=4 exact regions, c1 a free
// integer part shifting I1 uniformly); K2 is then solved as an INTERVAL with no upper bound at all,
// so the second rounding constant is completely free (its integer part is a shared offset on J).
// This space strictly contains any grid over (K1,K2).  SELF-TEST first: the solver must find the
// baseline model when the targets are set to the baseline model's own J values.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
static inline i64 fdiv(i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0))) q--; return q; }
static inline i64 A(i64 x,i64 y,int role){ return role? arr(y,x) : arr(x,y); }
static int TL[3]={17,342,-342};
static i64 WLO[3],WHI[3];
static long long g_pts, g_cfg, g_hit; static std::vector<std::string> hits;
static void run(int guard,int C1RANGE,bool allow_neg_K2){
    g_pts=g_cfg=g_hit=0; hits.clear();
    for(int w=2;w<=20;w++){
      i64 dx[3]; for(int i=0;i<3;i++) dx[i]=(i64)TL[i]<<w;
      if(guard && ((i64)512<<w)>=(1ll<<24)) continue;
      for(int s1=6;s1<=34;s1++) for(int s2=6;s2<=30;s2++){
        int E=s1+s2-2*w; if(E<0||E>28) continue;
        i64 slack=(i64)(342.0L*powl(2.0L,(long double)(w-s2)))+8+ (i64)(C1RANGE*342.0L*powl(2.0L,(long double)(w-s2)));
        long double f=powl(2.0L,(long double)E)/116964.0L;
        i64 klo=(i64)((18466836.0L-slack)*f)-4, khi=(i64)((18466836.0L+slack)*f)+4;
        if(klo<2) klo=2; if(khi>=(1ll<<28)) khi=(1ll<<28)-1; if(guard&&khi>=(1ll<<24)) khi=(1ll<<24)-1;
        if(khi<klo||khi-klo>8000000) continue;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
          for(i64 kc=klo;kc<=khi;kc++){ g_pts++;
            i64 q[3],th[3];
            for(int i=0;i<3;i++){ i64 a=A(kc,dx[i],r1); q[i]=fdiv(a,1ll<<s1); th[i]=(1ll<<s1)-(a-q[i]*(1ll<<s1)); }
            i64 bs[4]={0,th[0],th[1],th[2]}; std::sort(bs,bs+4);
            for(int b=0;b<4;b++){ i64 k1=bs[b]; if(k1>=(1ll<<s1)) continue; if(b&&bs[b]==bs[b-1]) continue;
              i64 I0[3]; for(int i=0;i<3;i++) I0[i]=q[i]+(k1>=th[i]?1:0);
              for(int c1=-C1RANGE;c1<=C1RANGE;c1++){
                i64 I1[3]; bool ok=true;
                for(int i=0;i<3;i++){ I1[i]=I0[i]+c1; if(I1[i]>=(1ll<<27)||I1[i]<=-(1ll<<27)) ok=false; }
                if(!ok) continue; g_cfg++;
                i64 lo=allow_neg_K2? -(1ll<<40):0, hi=(1ll<<40);
                for(int i=0;i<3&&lo<=hi;i++){ i64 P=A(I1[i],dx[i],r2);
                  i64 aa=(i64)WLO[i]*(1ll<<s2)-P, bb=(i64)(WHI[i]+1)*(1ll<<s2)-1-P; lo=std::max(lo,aa); hi=std::min(hi,bb); }
                if(lo<=hi){ g_hit++; if(hits.size()<25){ char bf[256];
                    snprintf(bf,sizeof bf,"w=%d s1=%d s2=%d r1=%d r2=%d KC=%lld K1=%lld(+%d*2^s1) K2=%lld I1=(%lld,%lld,%lld)",
                      w,s1,s2,r1,r2,(long long)kc,(long long)k1,c1,(long long)lo,(long long)I1[0],(long long)I1[1],(long long)I1[2]);
                    hits.push_back(bf);} } } } } } }
}
int main(int argc,char**argv){
    // ---------- SELF-TEST: targets = the baseline model's own J values ----------
    { i64 j[3]; for(int i=0;i<3;i++){ i64 dx=(i64)TL[i]<<13; i64 I1=(arr(KC,dx)+K)>>23; j[i]=(arr(I1,dx)+K)>>18; WLO[i]=WHI[i]=j[i]; }
      printf("[d-self] targets = baseline J = (%lld,%lld,%lld)\n",(long long)j[0],(long long)j[1],(long long)j[2]);
      run(1,2,false);
      printf("[d-self] %lld points, %lld configs, %lld HIT  (must be >0, and must include w=13 s1=23 s2=18 KC=5173573)\n",g_pts,g_cfg,g_hit);
      int found=0; for(auto&s:hits) if(s.find("w=13 s1=23 s2=18 r1=0 r2=0 KC=5173573")!=std::string::npos) found=1;
      for(size_t i=0;i<hits.size()&&i<6;i++) printf("         %s\n",hits[i].c_str());
      printf("[d-self] baseline config present among the first hits: %s\n",found?"YES":"(shown above or beyond the print cap)"); }
    // ---------- the real landmarks ----------
    WLO[0]=WHI[0]=45617; WLO[1]=WHI[1]=18466836; WLO[2]=18466858; WHI[2]=18466867;
    for(int guard=1;guard>=0;guard--){
      run(guard,2,false);
      printf("[d] REAL landmarks, %s, K2 free >=0, K1 integer part +-2: %lld (w,s1,s2,roles,KC) points, %lld exact configs, %lld hit all three\n",
        guard?"width-guarded (|dx|<2^24,KC<2^24)":"unguarded (|dx| up to 2^29, KC<2^28)",g_pts,g_cfg,g_hit);
      for(auto&s:hits) printf("    HIT %s\n",s.c_str()); }
    run(0,2,true);
    printf("[d] same, unguarded, K2 additionally allowed NEGATIVE: %lld configs, %lld hit all three\n",g_cfg,g_hit);
    for(auto&s:hits) printf("    HIT %s\n",s.c_str());
    return 0; }
