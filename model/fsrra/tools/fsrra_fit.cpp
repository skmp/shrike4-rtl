// FSRRA model fitter/verifier. Data: m14.bin = 2^24 little-endian u32 results for inputs x in [1,4):
// index i<2^23: x bits = 0x3f800000+i ; i>=2^23: 0x40000000+(i-2^23)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
using namespace std;
typedef int64_t i64; typedef uint64_t u64;
struct Coef{ i64 C0,C1,C2,C3; };   // C0 units(2^-26), C1 1/16ULP per u (negative), C2 quarter-ULP per u^2, C3 quarter-ULP per u^3 (|c3|)
// model: a=|t'|, s=sign ; W = C0 - s*fl(|C1|a/2^20) + fl(4*C2*sq/2^38) - s*fl(4*C3*cube/2^56)
static int SQS=15, AQ=4, CUBEMODE=1; static int E1=0,E2=0,E3=0; static int MODEL=0, EH1=0, EH2=0; static i64 SQK=0;
static inline i64 model_W(i64 tp,const Coef&c){
  i64 a = tp<0?-tp:tp; int s = tp<0?-1:1;
  if(MODEL==1){ // Horner, magnitudes: h1 = c2 - c3*u ; h2 = |c1| - h1*u ; W = C0 - s*h2*u   (all positive for right side)
    // units: C2 quarter-ULP/u^2 scaled by 2^E2 ; C3 quarter-ULP/u^3 scaled 2^E3 ; C1 1/16ULP/u scaled 2^E1
    i64 c3a = ((c.C3*a)>>(18+E3-EH1));              // c3*u in quarter-ULP/u^2 with EH1 extra bits
    i64 h1 = ((c.C2<<EH1)>>E2) - s*c3a;              // for left side (s=-1) cubic adds: c2 + c3*u  (odd term sign)
    i64 h1a = (h1*a)>>(18+EH1-EH2);                  // h1*u in quarter-ULP/u, EH2 extra bits
    i64 h2 = (((-c.C1)<<EH2)>>(2+E1)) - s*h1a;
    // W = C0 - s*h2*u  (h2 in quarter-ULP/u = units/u with EH2 bits)
    i64 h2a = (h2*a)>>(18+EH2);
    return c.C0 - s*h2a;
  }
  i64 sq = (((a*a)>>SQS)<<SQS)+SQK;
  i64 cube;                                  // scaled as a^3 / 2^8
  if(CUBEMODE==0) cube=(a*a*a)>>8;
  else if(CUBEMODE==1){ i64 at=(a>>AQ)<<AQ; cube=(sq*at)>>8; }
  else if(CUBEMODE==2){ i64 a3=a*a*a; cube=((a3>>AQ)<<AQ)>>8; }
  else if(CUBEMODE==3){ i64 at=(a>>AQ)<<AQ; cube=(at*at*at)>>8; }
  else { i64 at=(a>>AQ)<<AQ; cube=((a*a)*at)>>8; }
  i64 lin=((-c.C1)*a)>>(20+E1);
  i64 quad=(4*c.C2*sq)>>(38+E2);
  i64 cub=(4*c.C3*cube)>>(48+E3);
  return c.C0 - s*lin + quad - s*cub;
}
static inline u64 rne2(i64 W){ u64 q=W>>2; int r=W&3; return q + ((r>2)||(r==2&&(q&1))); }
static vector<uint32_t> M;
static inline u64 want(i64 idx){ uint32_t v=M[idx]; u64 m=(v&0x7fffff)|0x800000; if(((v>>23)&0xff)==0x7f) m*=2; return m; }
static int count_mism_r(int h,int n,const Coef&c,int limit,i64 R){
  i64 center=(i64)h*(1<<23)+(i64)n*(1<<19); i64 lo=max(center-R,(i64)h*(1<<23)); i64 hi=min(center+R,(i64)(h+1)*(1<<23));
  int bad=0;
  for(i64 idx=lo; idx<hi; idx++){ i64 tp=idx-center; if(rne2(model_W(tp,c))!=want(idx)){ if(++bad>=limit) return bad; } }
  return bad;
}
static int count_mism(int h,int n,const Coef&c,int limit){ return count_mism_r(h,n,c,limit,1<<18); }
int main(int argc,char**argv){
  if(getenv("SQK")) SQK=atol(getenv("SQK"));
  FILE*f=fopen("m14.bin","rb"); M.resize(1<<24); if(fread(M.data(),4,1<<24,f)!=(1<<24)) return 1; fclose(f);
  if(argc>1&&!strcmp(argv[1],"fit")){
    int r1=atoi(argv[2]), r2=atoi(argv[3]), r3=atoi(argv[4]);
    const char* initf = argc>5? argv[5] : "init.txt";
    if(argc>8){ SQS=atoi(argv[6]); AQ=atoi(argv[7]); CUBEMODE=atoi(argv[8]); }
    if(argc>11){ E1=atoi(argv[9]); E2=atoi(argv[10]); E3=atoi(argv[11]); }
    if(argc>14){ MODEL=atoi(argv[12]); EH1=atoi(argv[13]); EH2=atoi(argv[14]); }
    FILE*fi=fopen(initf,"r"); vector<Coef> init(34); for(auto&c:init) if(fscanf(fi,"%ld %ld %ld %ld",&c.C0,&c.C1,&c.C2,&c.C3)!=4) return 2; fclose(fi);
    const char* fon=getenv("FITOUT"); FILE*fo=fopen(fon?fon:"fit_out.txt","w"); i64 tot=0;
    const char* ns=getenv("NODES");
    for(int i=0;i<34;i++){ int h=i/17,n=i%17; if(ns){ char b[8]; snprintf(b,8," %d ",i); char s2[256]; snprintf(s2,256," %s ",ns); if(!strstr(s2,b)){ fprintf(fo,"%ld %ld %ld %ld\n",init[i].C0,init[i].C1,init[i].C2,init[i].C3); continue;} }
      Coef c=init[i]; c.C1<<=E1; c.C2<<=E2; c.C3<<=E3; Coef cb; int bb;
      // stage A: |t'|<2^16 : C0,C1 (C1 step = 1<<E1 i.e. 1/16 ULP/u)
      cb=c; bb=count_mism_r(h,n,c,1<<30,1<<16);
      for(int d1=-r1;d1<=r1;d1++) for(int d0=-3;d0<=3;d0++){ Coef c2=c; c2.C1+=d1<<E1; c2.C0+=d0; int v=count_mism_r(h,n,c2,bb,1<<16); if(v<bb){bb=v;cb=c2;} }
      c=cb;
      for(int rep=0;rep<2;rep++){
      // stage B: full: C2,C1,C0 (C3 fixed)
      bb=count_mism(h,n,c,1<<30);
      for(int d2=-r2;d2<=r2;d2++) for(int d1=-8;d1<=8;d1++) for(int d0=-2;d0<=2;d0++){ Coef c2=c; c2.C2+=d2<<E2; c2.C1+=d1<<E1; c2.C0+=d0; int v=count_mism(h,n,c2,bb); if(v<bb){bb=v;cb=c2;} }
      c=cb;
      // stage C: full : C3,C2,C1,C0
      for(int d3=-r3;d3<=r3;d3++) for(int d2=-2;d2<=2;d2++) for(int d1=-2;d1<=2;d1++) for(int d0=-1;d0<=1;d0++){ Coef c2=c; c2.C3+=d3<<E3; c2.C2+=d2<<E2; c2.C1+=d1<<E1; c2.C0+=d0; int v=count_mism(h,n,c2,bb); if(v<bb){bb=v;cb=c2;} }
      c=cb; }
      // stage E: fine refinement in the extra bits
      if(E1|E2|E3){ int R1=(1<<E1), R2=(1<<E2), R3=(1<<E3);
        for(int d1=-R1;d1<=R1;d1++) for(int d2=-R2;d2<=R2;d2++) for(int d3=-R3;d3<=R3;d3++) for(int d0=-1;d0<=1;d0++){ Coef c2=c; c2.C1+=d1; c2.C2+=d2; c2.C3+=d3; c2.C0+=d0; int v=count_mism(h,n,c2,bb); if(v<bb){bb=v;cb=c2;} }
        c=cb; }
      tot+=bb;
      printf("node %d %d: mism %d  C0=%ld C1=%ld C2=%ld C3=%ld\n",h,n,bb,cb.C0,cb.C1,cb.C2,cb.C3); fflush(stdout);
      fprintf(fo,"%ld %ld %ld %ld\n",cb.C0,cb.C1,cb.C2,cb.C3); fflush(fo);
    }
    printf("total %ld\n",tot); fclose(fo);
  } else if(argc>1&&!strcmp(argv[1],"verify")){
    FILE*fi=fopen(argv[2],"r"); vector<Coef> C(34); for(auto&c:C) if(fscanf(fi,"%ld %ld %ld %ld",&c.C0,&c.C1,&c.C2,&c.C3)!=4) return 2; fclose(fi);
    if(argc>5){ SQS=atoi(argv[3]); AQ=atoi(argv[4]); CUBEMODE=atoi(argv[5]); }
    if(argc>8){ E1=atoi(argv[6]); E2=atoi(argv[7]); E3=atoi(argv[8]); }
    if(argc>11){ MODEL=atoi(argv[9]); EH1=atoi(argv[10]); EH2=atoi(argv[11]); }
    i64 tot=0; for(int i=0;i<34;i++){ int h=i/17,n=i%17; int v=count_mism(h,n,C[i],1<<30); tot+=v; printf("node %d %d: %d\n",h,n,v);} printf("total %ld\n",tot);
  }
  return 0;
}
