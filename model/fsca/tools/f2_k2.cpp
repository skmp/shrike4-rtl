// f2_k2.cpp -- DIRECTION 2, part B.  The strongest form of the certificate: the rounding constant is EXHAUSTED.
//
// For every second-stage form (role, g, s2) we compute the exact set of rounding constants
//        K2 = q*2^s2 + r ,   r in [0,2^s2) exhausted,  q in [-4,8]   (q is a plain bias on the latch output)
// -- ONE constant shared by all 1024 offsets, as a physical constant must be -- for which every offset admits
// SOME integer I1.  |K2 set| = 0 refutes the form for every rounding constant, not just the published ones.
//
// For a fixed offset, the admissible r are  U_t = union over I1 of [Plo-ARR, Phi-ARR] cap [0,2^s2).
// An offset constrains nothing when its P-interval is at least |dx|+D wide (D = 2^20 >= the measured max
// unit-step deviation of the array, f2_step.cpp: 131072 role0, 305920 role1): consecutive I1 intervals then
// overlap and U_t is everything.  Those offsets are skipped rigorously, not heuristically.
// Latch rule is not a parameter here: f2_cert_table.txt shows floor / toward-zero / one's-complement give
// IDENTICAL results in all 2104 cells (the second product is positive wherever the window lives).
#include "f2_core.h"
#include <cstring>
#include <algorithm>
static Fsca F;
static const i64 DSLACK = 1ll<<20;
struct BM { std::vector<uint64_t> w; int s;
  void init(int s_,bool one){ s=s_; w.assign((size_t)((1ull<<s)/64),one?~0ull:0ull);}
  void setr(i64 a,i64 b){ if(a<0)a=0; i64 top=(1ll<<s)-1; if(b>top)b=top; if(a>b)return;
    for(i64 i=a;i<=b;){ if((i&63)==0 && b-i>=63){ w[i>>6]=~0ull; i+=64; } else { w[i>>6]|=1ull<<(i&63); i++; } } }
  void andwith(const BM&o){ for(size_t i=0;i<w.size();i++) w[i]&=o.w[i]; }
  long long pop()const{ long long c=0; for(auto x:w) c+=__builtin_popcountll(x); return c; }
  long long first()const{ for(size_t i=0;i<w.size();i++) if(w[i]) return (long long)i*64+__builtin_ctzll(w[i]); return -1; }
  bool get(i64 k)const{ if(k<0||k>=(1ll<<s))return false; return (w[k>>6]>>(k&63))&1; } };

static FinCtx* GC; static int GROLE,GG,GS2,GQ;
static void union_for(int t,BM&Ub){ Ub.init(GS2,false); i64 dx=(i64)t<<GG; i64 Plo,Phi;
  latch_inv(GC->JL[t+512]-GQ,GC->JH[t+512]-GQ,GS2,LR_FLOOR,Plo,Phi);
  i64 L=Plo-((1ll<<GS2)-1)-EB, H=Phi+EB; i64 lo,hi;
  auto fdiv=[](i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0)))q--; return q; };
  auto cdiv=[](i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)==(b<0)))q++; return q; };
  if(dx>0){lo=cdiv(L,dx);hi=fdiv(H,dx);} else {lo=cdiv(H,dx);hi=fdiv(L,dx);}
  if(lo<-VMAX)lo=-VMAX; if(hi>VMAX)hi=VMAX;
  for(i64 v=lo;v<=hi;v++){ i64 A=ARRof(GROLE,v,dx); Ub.setr(Plo-A,Phi-A); } }
static bool unconstraining(int t){ i64 dx=(i64)t<<GG; i64 Plo,Phi;
  latch_inv(GC->JL[t+512]-GQ,GC->JH[t+512]-GQ,GS2,LR_FLOOR,Plo,Phi);
  return Phi-Plo+1 >= (dx<0?-dx:dx)+DSLACK; }
// smallest |I1| that works at t for a given r, and whether one exists
static i64 minI1(int t,i64 r,int&ok){ i64 dx=(i64)t<<GG; ok=0;
  Cert c=certify(GROLE,dx,r,GS2,LR_FLOOR,GC->JL[t+512]-GQ,GC->JH[t+512]-GQ);
  if(c.hit){ok=1; return c.vhit<0?-c.vhit:c.vhit;} return -1; }

int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F;
  int SVSEL = argc>1?atoi(argv[1]):0;  Fin f{SVSEL,0,1,0};
  FILE*out=fopen(argc>2?argv[2]:"f2_k2_table.txt","w");
  fprintf(out,"# f2_k2: exact set of second-latch rounding constants K2 = q*2^s2 + r admitting SOME integer I1\n");
  fprintf(out,"# at EVERY offset simultaneously.  r exhausted over [0,2^s2), q over [-4,8].\n");
  fprintf(out,"# cols: |K2 all-t| over all q ; best q ; |K2 for the 3 pinned offsets 342,-342,17| ; max over t of\n");
  fprintf(out,"# the smallest |I1| that works (a physical operand needs < 2^24 = 16777216)\n");
  for(int du=2; du>=0; du-=2){ FinCtx C; fin_build(f,C,du); GC=&C;
    int nf=0; for(int t=-512;t<=511;t++) if(C.JL[t+512]<=C.JH[t+512]) nf++;
    fprintf(out,"\n=== final variant sv=%d, latch scale shift %d, feasible t = %d ===\n",SVSEL,20-du,nf);
    for(int role=0;role<2;role++) for(int g=8;g<=14;g++) for(int s2=g;s2<=std::min(g+12,22);s2++){
      GROLE=role;GG=g;GS2=s2;
      std::vector<int> ord; for(int t=-512;t<=511;t++) if(t&&C.JL[t+512]<=C.JH[t+512]) ord.push_back(t);
      std::sort(ord.begin(),ord.end(),[&](int a,int b){ i64 wa=C.JH[a+512]-C.JL[a+512], wb=C.JH[b+512]-C.JL[b+512];
          if(wa!=wb) return wa<wb; return std::abs(a)>std::abs(b); });
      long long bestall=0; int bestq=99; long long bestpin=0; int bestpq=99; BM BEST;
      for(int q=-4;q<=8;q++){ GQ=q; BM M,U; M.init(s2,true);
        // the three pinned offsets on their own
        BM P3; P3.init(s2,true);
        for(int t: {342,-342,17}){ int i=t+512; if(C.JL[i]>C.JH[i]) continue; if(unconstraining(t)) continue;
          union_for(t,U); P3.andwith(U); }
        long long np3=P3.pop(); if(np3>bestpin){bestpin=np3;bestpq=q;}
        if(np3==0) continue;                              // all-t set is a subset of the pinned set
        for(int t: ord){ if(unconstraining(t)) continue; union_for(t,U); M.andwith(U); if(M.pop()==0) break; }
        long long na=M.pop(); if(na>bestall){bestall=na;bestq=q;BEST=M;} }
      // physical size of I1 for the best surviving cell
      long long maxmin=-1; i64 r0=-1;
      if(bestall>0){ GQ=bestq; r0=BEST.first(); maxmin=0;
        for(int t=-512;t<=511;t++){ if(!t||C.JL[t+512]>C.JH[t+512])continue; int ok; i64 v=minI1(t,r0,ok);
          if(!ok){maxmin=-2;break;} if(v>maxmin) maxmin=v; } }
      fprintf(out,"role%d g=%2d s2=%2d s2-g=%2d : |K2 all-t|=%9lld/%-9lld q=%-3d   |K2 3pinned|=%9lld q=%-3d",
        role,g,s2,s2-g,bestall,(long long)(1ll<<s2),bestq==99?0:bestq,bestpin,bestpq==99?0:bestpq);
      if(bestall>0) fprintf(out,"   r=%lld maxminI1=%lld %s",(long long)r0,maxmin,maxmin>=0&&maxmin<16777216?"FITS24":"wide");
      fprintf(out,"\n"); }
    fflush(out); }
  fclose(out); puts("done"); return 0; }
