// precision.cpp -- data for the blog plots: how far each SH-4 approximation instruction is from the exact mathematical
// result, in ULPs of the correctly rounded single-precision answer.  Hardware results come from the bit-exact model
// (../../model/sh4_fpu_approx.h, verified against the console).  Illustrative: long double / __int128 references.
// build: g++ -O2 -std=c++17 -ffp-contract=off -fopenmp precision.cpp -o /tmp/precision && /tmp/precision ../data
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../../../model/sh4_fpu_approx.h"
typedef __int128 i128; typedef long double ld;
static float f32(uint32_t b){ float f; memcpy(&f,&b,4); return f; } static uint32_t bits(float f){ uint32_t b; memcpy(&b,&f,4); return b; }
static uint64_t rng=0x9e3779b97f4a7c15ull; static uint64_t rnd(){ rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; return rng; }
static ld ulp_of(ld v){ v=fabsl(v); if(v==0) return ldexpl(1,-149); int e; frexpl(v,&e); return ldexpl(1,e-24); }   // ulp of the float nearest v
struct Hist { int nb; ld lo,hi; std::vector<long> h; long under=0,over=0,n=0; ld maxabs=0; long exact=0;
    Hist(int nb_,ld lo_,ld hi_):nb(nb_),lo(lo_),hi(hi_),h(nb_,0){} void add(ld e){ n++; if(fabsl(e)>maxabs)maxabs=fabsl(e); if(fabsl(e)<=0.5L)exact++; if(e<lo)under++; else if(e>=hi)over++; else h[(int)((e-lo)/(hi-lo)*nb)]++; } };
static void dump(const std::string&p,const Hist&a,const Hist&b,const Hist&c){ FILE*f=fopen(p.c_str(),"w"); fprintf(f,"bin_lo,bin_hi,hw,ieee,fused\n"); for(int i=0;i<a.nb;i++) fprintf(f,"%.4Lf,%.4Lf,%.6f,%.6f,%.6f\n",a.lo+(a.hi-a.lo)*i/a.nb,a.lo+(a.hi-a.lo)*(i+1)/a.nb,100.0*a.h[i]/a.n,100.0*b.h[i]/b.n,100.0*c.h[i]/c.n); fclose(f); }
// exact dot product of float bit patterns as (integer, exponent): sum = S * 2^E
static bool exact_dot(const uint32_t*a,const uint32_t*b,i128&S,int&E){ int e[4]; i128 m[4]; int emin=1<<30; for(int i=0;i<4;i++){ int ea=(a[i]>>23)&255, eb=(b[i]>>23)&255; if(!ea||!eb){m[i]=0;e[i]=1<<29;continue;} i128 p=(i128)((a[i]&0x7fffff)|0x800000)*((b[i]&0x7fffff)|0x800000); if((a[i]^b[i])>>31)p=-p; m[i]=p; e[i]=ea+eb-254-46; if(e[i]<emin)emin=e[i]; }
    S=0; for(int i=0;i<4;i++){ if(m[i]==0)continue; int sh=e[i]-emin; if(sh>70)return false; S+=m[i]*((i128)1<<sh); } E=emin; return true; }
static ld to_ld(i128 S,int E){ bool n=S<0; if(n)S=-S; ld v=(ld)(uint64_t)(S>>64)*18446744073709551616.0L+(ld)(uint64_t)S; v=ldexpl(v,E); return n?-v:v; }
// "fused full" inner product: four EXACT 48-bit products, aligned to the largest exponent into a (48+3)-bit window with a
// sticky (jam) bit, one exact four-input add, ONE round-to-nearest-even -- what the SH-4 block would be without truncation.
static float fused_full(const uint32_t*a,const uint32_t*b){ int e[4],n=0,emax=-1<<30; i128 p[4]; int sg[4];
    for(int i=0;i<4;i++){ int ea=(a[i]>>23)&255, eb=(b[i]>>23)&255; if(!ea||!eb)continue; p[n]=((i128)((a[i]&0x7fffff)|0x800000)*((b[i]&0x7fffff)|0x800000))<<3; e[n]=ea+eb; sg[n]=((a[i]^b[i])>>31)&1; if(e[n]>emax)emax=e[n]; n++; }
    i128 S=0; for(int i=0;i<n;i++){ int d=emax-e[i]; i128 x; if(d>=51)x=1; else { x=p[i]>>d; if(p[i]&(((i128)1<<d)-1))x|=1; } S+=sg[i]?-x:x; }
    if(S==0)return 0.0f; bool neg=S<0; if(neg)S=-S; int top=0; while((S>>(top+1))!=0)top++; int sh=top-23; i128 q; if(sh>0){ i128 r=S&(((i128)1<<sh)-1),h=(i128)1<<(sh-1); q=S>>sh; if(r>h||(r==h&&(q&1)))q++; if(q>>24){q>>=1;top++;} } else q=S<<(-sh);
    ld v=ldexpl((ld)(uint64_t)q,(top-23)+(emax-254-46-3)); return (float)(neg?-v:v); }
static void dots(const char*dir,const char*name,int kind,long N){ Hist H(64,-4,4),I(64,-4,4),Fu(64,-4,4); long differ=0,used=0;
    for(long k=0;k<N;k++){ uint32_t a[4],b[4]; float fa[4],fb[4];
        for(int i=0;i<4;i++){ ld x,y; if(kind==0){ x=(ld)(rnd()%2000001)/1e6L; y=(ld)(rnd()%2000001)/1e6L; }                                   // all products positive, O(1)
            else if(kind==1){ x=(ld)(rnd()%2000001)/1e6L-1; y=(ld)(rnd()%2000001)/1e6L-1; }                                                       // random signs in [-1,1]
            else { static ld M[4][4]; static ld V[4]; if(i==0){ ld th=(ld)(rnd()%6283185)/1e6L, ph=(ld)(rnd()%6283185)/1e6L; ld c=cosl(th),s=sinl(th),cp=cosl(ph),sp=sinl(ph); ld R[4][4]={{c*cp,-s,c*sp,(ld)(rnd()%200001)/1e3L-100},{s*cp,c,s*sp,(ld)(rnd()%200001)/1e3L-100},{-sp,0,cp,(ld)(rnd()%200001)/1e3L+10},{0,0,0,1}}; memcpy(M,R,sizeof R); int row=rnd()%3; for(int j=0;j<4;j++)M[0][j]=R[row][j]; V[0]=(ld)(rnd()%200001)/1e3L-100; V[1]=(ld)(rnd()%200001)/1e3L-100; V[2]=(ld)(rnd()%200001)/1e3L-100; V[3]=1; }
                   x=V[i]; y=M[0][i]; }                                                                                                       // one row of a rotate+translate matrix times a vertex
            fa[i]=(float)x; fb[i]=(float)y; a[i]=bits(fa[i]); b[i]=bits(fb[i]); }
        i128 S; int E; if(!exact_dot(a,b,S,E)||S==0)continue; ld ex=to_ld(S,E), u=ulp_of(ex);
        float hw=f32(sh4_fipr(a,b,0)); volatile float t0=fa[0]*fb[0],t1=fa[1]*fb[1],t2=fa[2]*fb[2],t3=fa[3]*fb[3]; volatile float s=t0+t1; s=s+t2; s=s+t3; float ie=s;
        H.add(((ld)hw-ex)/u); I.add(((ld)ie-ex)/u); Fu.add(((ld)fused_full(a,b)-ex)/u); differ+=bits(hw)!=bits(ie); used++; }
    dump(std::string(dir)+"/"+name+".csv",H,I,Fu);
    printf("%-10s %ld vectors: HW within 0.5 ULP (= correctly rounded) %.2f%%, max |err| %.2Lf ULP, beyond +-4: %.4f%% | IEEE sequential float: %.2f%%, max %.2Lf | FUSED-FULL (exact multipliers): %.2f%%, max %.2Lf | HW != IEEE-sequential bit pattern in %.2f%%\n",name,used,100.0*H.exact/H.n,H.maxabs,100.0*(H.under+H.over)/H.n,100.0*I.exact/I.n,I.maxabs,100.0*Fu.exact/Fu.n,Fu.maxabs,100.0*differ/used); }
static bool g_exact=false; static inline int64_t MUL(int64_t r,int64_t m){ return g_exact? r*m : sh4_fpu__arr(r,m); }
static int64_t fsca_V(uint32_t angle){ const int64_t K=1ll<<19; uint32_t a=angle&0xffffu,node=((a+512u)>>10)&63u,r=node&15u,quad=node>>4; int t=(int)((a+512u)&1023u)-512; int swap=quad&1,m=swap?16-(int)r:(int)r;
    const sh4_fsca_word_t*W=&sh4_fsca_rom[m],*X=&sh4_fsca_rom[16-m]; int64_t dx=(int64_t)t*8192, sq=sh4_fpu__asr(MUL(dx,dx)+K,23), T1=sh4_fpu__sm(MUL(X->l,dx)+K,23+X->e), A3=sh4_fpu__asr(MUL(sq,dx)+K,23), T3=sh4_fpu__sm(MUL((int64_t)X->q*32,A3)+K,26);
    int64_t I1=sh4_fpu__asr(MUL(SH4_FSCA_KC,dx)+K,23), J=sh4_fpu__asr(MUL(dx,I1)+K,23), T2=sh4_fpu__sm(MUL(W->l,J)+K,27+W->e), V=(int64_t)W->c0+(swap?T3-T1:T1-T3)-T2; return (quad&2u)?-V:V; }
static int64_t fsrra_W(uint32_t man,int parity){ const int64_t K=1ll<<19; int64_t m=man,node=(m+(1<<18))>>19,t=m-node*(1ll<<19); const sh4_fsrra_word_t*w=&sh4_fsrra_rom[(parity?17:0)+(int)node]; int64_t dx=t*16, sq=(MUL(dx,dx)+K)>>23;
    int64_t T1=sh4_fpu__sm(MUL(w->rec1,dx)+K,26-w->e1), T2=sh4_fpu__sm(MUL((int64_t)w->c2f*512,sq)+K,29), I=(MUL((int64_t)w->c3f*256,sq)+K)>>23, T3=sh4_fpu__sm(MUL(dx,I)+K,27); return (int64_t)w->c0-T1+T2-T3; }
int main(int argc,char**argv){ const char*dir=argc>1?argv[1]:"."; const ld PI=3.14159265358979323846264338327950288L;
    { FILE*f=fopen((std::string(dir)+"/fsca.csv").c_str(),"w"); fprintf(f,"angle,err_sin_min,err_sin_max\n"); long exact=0; ld mx=0; int amx=0; ld cur_lo=9,cur_hi=-9;
      for(int a=0;a<65536;a++){ ld tv=sinl(2*PI*a/65536.0L); uint32_t hb=sh4_fsca_sin(a,0); ld hv=f32(hb); ld e24=(hv-tv)*16777216.0L; ld eu=(hv-tv)/ulp_of(tv); if(fabsl(eu)<=0.5L||a%16384==0&&hv==tv)exact++; if(fabsl(e24)>mx){mx=fabsl(e24);amx=a;}
          if(a<16384){ cur_lo=std::min(cur_lo,e24); cur_hi=std::max(cur_hi,e24); if(a%32==31){ fprintf(f,"%d,%.4Lf,%.4Lf\n",a-16,cur_lo,cur_hi); cur_lo=9;cur_hi=-9; } } }
      fclose(f); printf("FSCA  sine, all 65536 angles (RN): correctly rounded %.2f%%, max |hw - true| = %.3Lf x 2^-24 (at angle 0x%04x)\n",100.0*exact/65536,mx,amx);
      for(int a=0;a<65536;a++) if(fsca_V(a)!=sh4_fsca_internal(a)){ printf("fsca twin mismatch\n"); return 1; }
      g_exact=true; long ex2=0; ld mx2=0; for(int a=0;a<65536;a++){ ld tv=sinl(2*PI*a/65536.0L); ld hv=f32(sh4_fsca__to_float(fsca_V(a),0)); ld eu=(hv-tv)/ulp_of(tv); if(fabsl(eu)<=0.5L||(a%16384==0&&hv==tv))ex2++; mx2=std::max(mx2,fabsl((hv-tv)*16777216.0L)); } g_exact=false;
      printf("FSCA  same microprogram and ROM with EXACT multipliers: correctly rounded %.2f%%, max %.3Lf x 2^-24\n",100.0*ex2/65536,mx2); }
    { const int NB=768; std::vector<ld> lo(NB,9),hi(NB,-9); std::vector<ld> sum(NB,0); long exact=0; ld mx=0; long hist[81]={0};
      for(uint32_t i=0;i<(1u<<24);i++){ uint32_t x=0x3f800000u+i; ld xv=f32(x); ld tv=1/sqrtl(xv); ld hv=f32(sh4_fsrra(x,0)); ld eu=(hv-tv)/ulp_of(tv); int b=(int)((ld)i*NB/(1u<<24)); lo[b]=std::min(lo[b],eu); hi[b]=std::max(hi[b],eu); sum[b]+=eu; if(fabsl(eu)<=0.5L)exact++; if(fabsl(eu)>mx)mx=fabsl(eu); int hb=(int)floorl((eu+2)*20); if(hb>=0&&hb<80)hist[hb]++; }
      FILE*f=fopen((std::string(dir)+"/fsrra.csv").c_str(),"w"); fprintf(f,"x,err_min,err_max,err_mean\n"); for(int b=0;b<NB;b++){ ld x=(b+0.5L)/NB; ld xv=x<0.5L?1+2*x:2+4*(x-0.5L); fprintf(f,"%.5Lf,%.4Lf,%.4Lf,%.4Lf\n",xv,lo[b],hi[b],sum[b]/((1u<<24)/NB)); } fclose(f);
      f=fopen((std::string(dir)+"/fsrra_hist.csv").c_str(),"w"); fprintf(f,"bin_lo,bin_hi,hw\n"); for(int b=0;b<80;b++)fprintf(f,"%.3f,%.3f,%.6f\n",-2+b/20.0,-2+(b+1)/20.0,100.0*hist[b]/(double)(1u<<24)); fclose(f);
      printf("FSRRA all 2^24 inputs of [1,4) (RN): correctly rounded %.2f%%, max |err| = %.3Lf ULP\n",100.0*exact/(1u<<24),mx);
      long ex2=0; ld mx2=0; for(uint32_t i=0;i<(1u<<24);i++){ g_exact=false; if((i&0xfff)==0&&fsrra_W(i&0x7fffff,i>>23)!=sh4_fsrra_internal(i&0x7fffff,(int)(i>>23))){ printf("fsrra twin mismatch\n"); return 1; }
          g_exact=true; int64_t Wv=fsrra_W(i&0x7fffff,i>>23); uint64_t q=(uint64_t)Wv; int top=(q>>26)?26:25,sh=top-23; uint64_t r=q&((1ull<<sh)-1),h=1ull<<(sh-1),qq=q>>sh; qq+=(r>h)||(r==h&&(qq&1)); if(qq>>24){qq>>=1;top++;}
          ld hv=ldexpl((ld)qq,top-23-26); ld xv=f32(0x3f800000u+i); ld tv=1/sqrtl(xv); ld eu=(hv-tv)/ulp_of(tv); if(fabsl(eu)<=0.5L)ex2++; mx2=std::max(mx2,fabsl(eu)); } g_exact=false;
      printf("FSRRA same microprogram and ROM with EXACT multipliers: correctly rounded %.2f%%, max %.3Lf ULP\n",100.0*ex2/(1u<<24),mx2); }
    dots(dir,"fipr_pos",0,2000000); dots(dir,"fipr_signed",1,2000000); dots(dir,"ftrv_xform",2,2000000);
    return 0; }
