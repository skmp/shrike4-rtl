// v51_sq.cpp -- ADVERSARIAL VERIFY of branch-5's square-path claims, written from scratch.
// Scores J at the fine latch (shift 18) against jwin18.txt.
#include "base2.h"
#include <cstring>
#include <cstdio>
static i64 JL[1024],JH[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); if(!f){perror("jwin18");exit(1);} char ln[512]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,w;
    if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&w)!=4)continue; JL[t+512]=l; JH[t+512]=h; n++; }
  fclose(f); if(n!=1024){fprintf(stderr,"bad jwin18 (%d lines)\n",n);exit(1);} }
// generic chain: tv = value of t actually used; dxoff = extra dx offset; r = I1 offset; conv = shift rule
static inline i64 shr_(i64 v,int u,int conv){ return conv? sm(v,u) : (v>>u); }
static inline i64 chain(i64 tva,i64 tvb,i64 dxoffa,i64 dxoffb,i64 r,int c1,int c2,int order,int w,i64 kc,int s1,int s2){
  i64 dxa=(tva<<w)+dxoffa, dxb=(tvb<<w)+dxoffb;
  i64 I1 = order? shr_(arr(dxa,kc)+K,s1,c1) : shr_(arr(kc,dxa)+K,s1,c1);
  I1 += r;
  return shr_(arr(I1,dxb)+K,s2,c2); }
struct Res{int tot,pos,neg;};
template<class F> static Res sc(F f){ Res r{0,0,0};
  for(int t=-512;t<=511;t++){ i64 j=f(t); if(j>=JL[t+512]&&j<=JH[t+512]){r.tot++; if(t>=0)r.pos++; else r.neg++;} }
  return r; }
int main(){ build_tables(); load();
  auto base=[&](int t){ return chain(t,t,0,0,0,0,0,0,13,KC,23,18); };
  Res b=sc(base);
  printf("BASELINE arr(KC,dx): %d/1024 (pos %d neg %d)  J(17)=%lld J(342)=%lld J(-342)=%lld\n",
    b.tot,b.pos,b.neg,(long long)base(17),(long long)base(342),(long long)base(-342));
  auto base1=[&](int t){ return chain(t,t,0,0,0,0,0,1,13,KC,23,18); };
  Res b1=sc(base1); printf("BASELINE arr(dx,KC): %d/1024 (pos %d neg %d)\n",b1.tot,b1.pos,b1.neg);
  printf("  => sign-gated ceilings: arr(KC,dx) %d+512=%d ; arr(dx,KC) %d+512=%d\n\n",b.pos,b.pos+512,b1.pos,b1.pos+512);

  // ---- 1. RIGIDITY, my own exhaustive re-check over the same 5508-config space AND wider
  printf("--- rigidity of t>=0 under every sign-gated config (da,db in -8..8, r in -16..16, 2x2 conv, both orders) ---\n");
  long long ncfg=0; int ndiff=0; int firstdiff=-1;
  for(int order=0;order<2;order++) for(int c1=0;c1<2;c1++) for(int c2=0;c2<2;c2++)
   for(int da=-8;da<=8;da++) for(int db=-8;db<=8;db++) for(int r=-16;r<=16;r++){ ncfg++;
     for(int t=0;t<=511;t++){
       i64 jm=chain(t+(t<0?da:0),t+(t<0?db:0),0,0,(t<0?r:0),c1,c2,order,13,KC,23,18);
       i64 jb=chain(t,t,0,0,0,0,0,order,13,KC,23,18);
       if(jm!=jb){ ndiff++; if(firstdiff<0)firstdiff=t; break; } } }
  printf("  configs %lld ; configs where some t>=0 differs from baseline: %d\n",ncfg,ndiff);
  // separately: does the shift CONVENTION alone ever matter for t>=0?  (is arr()+K ever negative there?)
  { int negP1=0,negP2=0,diff=0;
    for(int t=0;t<=511;t++){ i64 dx=(i64)t<<13; i64 P1=arr(KC,dx)+K; if(P1<0)negP1++;
      i64 I1=P1>>23; i64 P2=arr(I1,dx)+K; if(P2<0)negP2++;
      if((P1>>23)!=sm(P1,23) || (P2>>18)!=sm(P2,18)) diff++; }
    printf("  t>=0: first-product+K negative %d/512, second %d/512, sm!=>> at %d/512\n",negP1,negP2,diff); }
  // and for t<0 the conventions DO differ (sanity that the scan had a live parameter)
  { int diff=0; for(int t=-512;t<0;t++){ i64 dx=(i64)t<<13; i64 P1=arr(KC,dx)+K;
      i64 I1=P1>>23; i64 P2=arr(I1,dx)+K; if((P1>>23)!=sm(P1,23)||(P2>>18)!=sm(P2,18))diff++; }
    printf("  t<0 : sm!=>> at %d/512 (so the convention parameter is live on the negative side)\n\n",diff); }

  // ---- 2. hypothesis A: value used for t<0 is t+d
  printf("--- A: t' = t+d for t<0 (d=+1 is '2s computed, 1s read') ---\n");
  for(int d=-4;d<=4;d++){ auto f=[&](int t){ return chain(t+(t<0?d:0),t+(t<0?d:0),0,0,0,0,0,0,13,KC,23,18); };
    Res r=sc(f); printf("  d=%+d : %4d/1024 (pos %d neg %d)  J(-342)=%lld%s\n",d,r.tot,r.pos,r.neg,
      (long long)f(-342), d==1?"   <- the named case":(d==-1?"   <- converse":"")); }

  // ---- 3. sign-magnitude reading, field widths 8..16 (branch 5 only did 10..16)
  printf("\n--- A2: sign-magnitude read of the 2s pattern, field width n (n<10 ALSO hits t>=0) ---\n");
  for(int nb=8;nb<=16;nb++){
    auto f=[&](int t)->i64{ i64 pat=(i64)((unsigned)t & ((1u<<nb)-1)); i64 tv;
      if(pat>>(nb-1)) tv = -(pat & (((i64)1<<(nb-1))-1)); else tv=pat;
      return chain(tv,tv,0,0,0,0,0,0,13,KC,23,18); };
    Res r=sc(f); printf("  n=%2d : %4d/1024 (pos %d neg %d)  J(17)=%lld J(342)=%lld\n",nb,r.tot,r.pos,r.neg,
      (long long)f(17),(long long)f(342)); }
  printf("\n--- A2b: 1s-complement read of the 2s pattern, field width n (n<10 hits t>=0) ---\n");
  for(int nb=8;nb<=16;nb++){
    auto f=[&](int t)->i64{ i64 pat=(i64)((unsigned)t & ((1u<<nb)-1)); i64 tv;
      if(pat>>(nb-1)) tv = -((((i64)1<<nb)-1)-pat); else tv=pat;
      return chain(tv,tv,0,0,0,0,0,0,13,KC,23,18); };
    Res r=sc(f); printf("  n=%2d : %4d/1024 (pos %d neg %d)  J(17)=%lld J(342)=%lld\n",nb,r.tot,r.pos,r.neg,
      (long long)f(17),(long long)f(342)); }

  // ---- 4. the NON-sign-gated reading: subtraction as 1s add, missing carry-in -> uniform t-1
  printf("\n--- A3: uniform t+u (missing carry-in = u=-1) ---\n");
  for(int u=-2;u<=2;u++){ auto f=[&](int t){ return chain(t+u,t+u,0,0,0,0,0,0,13,KC,23,18); };
    Res r=sc(f); printf("  u=%+d : %4d/1024  J(17)=%lld J(342)=%lld\n",u,r.tot,(long long)f(17),(long long)f(342)); }

  // ---- 5. m5_mag documentation bug: what does ~t vs -t actually score?
  printf("\n--- m5_mag.cpp doc-bug check: t-level magnitude forms ---\n");
  { auto f_not=[&](int t){ i64 m = t>=0? t : ~(i64)t; return chain(m,m,0,0,0,0,0,0,13,KC,23,18); };
    auto f_neg=[&](int t){ i64 m = t>=0? t : -(i64)t; return chain(m,m,0,0,0,0,0,0,13,KC,23,18); };
    auto f_p1 =[&](int t){ i64 m = t>=0? t : (i64)t+1; return chain(m,m,0,0,0,0,0,0,13,KC,23,18); };
    printf("  mag = ~t  (1s magnitude, = |t|-1) : %4d/1024\n",sc(f_not).tot);
    printf("  mag = -t  (2s magnitude, = |t|)   : %4d/1024   <- what m5_mag actually computes at e=0\n",sc(f_neg).tot);
    printf("  value = t+1 for t<0 (true 1s read): %4d/1024\n",sc(f_p1).tot); }

  // ---- 6. the p-scan: uniform dx offset p
  printf("\n--- uniform dx-level offset p (the lattice/ratio argument) ---\n");
  { int bp=0,bs=-1; for(int p=-64;p<=64;p++){ auto f=[&](int t){ return chain(t,t,p,p,0,0,0,0,13,KC,23,18); };
      Res r=sc(f); if(r.tot>bs){bs=r.tot;bp=p;} }
    printf("  best p in [-64,64]: p=%d score %d\n",bp,bs);
    for(int p : {-20,-19,-18,-1,0,1}){ auto f=[&](int t){ return chain(t,t,p,p,0,0,0,0,13,KC,23,18); };
      Res r=sc(f); printf("  p=%+4d : %4d/1024  J(17)=%lld (need 45617)  J(342)=%lld (need 18466836)\n",
        p,r.tot,(long long)f(17),(long long)f(342)); } }
  // exhaustively: is 18466836 EVER in the image at t=342 under any dx offset?
  { bool hit17=false,hit342=false; long long p17=0,p342=0;
    for(long long p=-2000000;p<=2000000;p++){ i64 j17=chain(17,17,p,p,0,0,0,0,13,KC,23,18);
      if(j17==45617&&!hit17){hit17=true;p17=p;}
      i64 j342=chain(342,342,p,p,0,0,0,0,13,KC,23,18); if(j342==18466836&&!hit342){hit342=true;p342=p;} }
    printf("  over p in +-2e6: J(17)==45617 reachable? %s (p=%lld)   J(342)==18466836 reachable? %s (p=%lld)\n",
      hit17?"YES":"NO",p17,hit342?"YES":"NO",p342); }
  return 0; }
