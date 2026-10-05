// v32b_nd.cpp -- branch 32: is the ND=13 claim real?  Three separate questions, kept apart:
//   (a) does ND change the MODEL's numbers at all (I1, J, V)?
//   (b) what does ND=13 score against the PUBLISHED jwin18.txt (the fixed scoring contract)?
//   (c) what does it score against windows REGENERATED at ND=13 -- by bisection (as w3_common.h does)
//       and by an HONEST rule that does not assume the array is monotone in the latch?
#include "v32_common.h"
#include <random>
#include <vector>
static const i64 LIMW = 12000000ll<<2;         // 48,000,000, b39's search bound
int main(){ if(!load_fsca(FD)){puts("no dump");return 1;} build_tables(); if(!load_jwin()){puts("no jwin18");return 1;}

  // ---------- 0. where does the fine-scale latch J exceed a 13-digit array's signed range? ----------
  {   int ge25=0, wge25=0, mxdig=0;
      for(int t=-512;t<=511;t++){ i64 J=J_of(t,14,18,0); if(J>=(1ll<<25))ge25++;
          if(FL[t+512]>=(1ll<<25))wge25++;
          u64 a=(u64)(J<0?-J:J); int b=a?64-__builtin_clzll(a):0; int dg=b/2+1; if(dg>mxdig)mxdig=dg; }
      printf("0. fine-scale J >= 2^25 at %d/1024 offsets ; window LOWER BOUND >= 2^25 at %d ; max Booth digits for J = %d\n",
             ge25,wge25,mxdig);
      int i1big=0,kcbig=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
          if(I1>=(1ll<<25)||I1<-(1ll<<25))i1big++; }
      if(KC>=(1ll<<25))kcbig=1;
      printf("   recoded operands of the OTHER square-path multiplies outside +-2^25: arr(KC,dx) %d, arr(I1,dx) %d\n",kcbig,i1big); }

  // ---------- 1. does ND move anything the model computes? ----------
  printf("\n1. ND applied to EVERY array call; differences against ND=14 over all 1024 offsets / 32768 samples\n");
  printf("   ND   I1 diff   J18 diff   J20 diff   V(coarse) diff   V(fine) diff\n");
  for(int nd=10;nd<=20;nd++){
      int d1=0,d18=0,d20=0; long long dvc=0,dvf=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
          i64 I1a=(arrN(KC,dx,nd)+K)>>23, I1b=(arr(KC,dx)+K)>>23; if(I1a!=I1b)d1++;
          if(((arrN(I1a,dx,nd)+K)>>18)!=((arr(I1b,dx)+K)>>18))d18++;
          if(((arrN(I1a,dx,nd)+K)>>20)!=((arr(I1b,dx)+K)>>20))d20++; }
      for(int node=0;node<=31;node++) for(int t=-512;t<=511;t++){
          if(V_of(node,t,J_of(t,nd,20,0),nd,0)!=V_of(node,t,J_of(t,14,20,0),14,0)) dvc++;
          if(V_of(node,t,J_of(t,nd,18,0),nd,2)!=V_of(node,t,J_of(t,14,18,0),14,2)) dvf++; }
      printf("   %2d   %6d   %8d   %8d   %14lld   %12lld\n",nd,d1,d18,d20,dvc,dvf); }

  // ---------- 2. coarse full model + guard, and the PUBLISHED-contract jwin18 score ----------
  printf("\n2. per ND: coarse full model (guard measured separately, not in one printf), and jwin18.txt score\n");
  printf("   ND   coarseViol  guard   fineViol  guard   jwin18.txt(fixed contract)   J(17)      J(342)     J(-342)\n");
  for(int nd=10;nd<=18;nd++){
      int vc=0,gc=0,vf=0,gf=0;
      for(int node=0;node<=31;node++) for(int t=-512;t<=511;t++){
          int n=node>=17?32-node:node; int gd=(n==0||n==1||n==2||n==16);
          int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
          i64 Vc=V_of(node,t,J_of(t,nd,20,0),nd,0); if(Vc<lo||Vc>hi){vc++; if(gd)gc++;}
          i64 Vf=V_of(node,t,J_of(t,nd,18,0),nd,2); if(Vf<lo||Vf>hi){vf++; if(gd)gf++;} }
      int sc=0; for(int t=-512;t<=511;t++){ i64 J=J_of(t,nd,18,0); if(J>=FL[t+512]&&J<=FH[t+512])sc++; }
      printf("   %2d   %8d   %5d   %8d   %5d   %19d   %-10lld %-10lld %-10lld\n",
             nd,vc,gc,vf,gf,sc,(long long)J_of(17,nd,18,0),(long long)J_of(342,nd,18,0),(long long)J_of(-342,nd,18,0)); }

  // ---------- 3. regenerated windows: bisection (w3_common.h's method) vs honest ----------
  printf("\n3. windows REGENERATED at each ND.  bis = b39/w3 binary search ; low = [JL, min(JH, 2^(2ND-1)-1)]\n");
  printf("   ND   bisFeas  bisScore   honestFeas  honestScore   (honest assumes only the low region; see 4)\n");
  for(int nd=11;nd<=14;nd++){
      i64 gl[1024],gh[1024];
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 L=0,H=LIMW; i64 A3=A3_of(t,dx,nd);
          for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
              i64 s=flip?-1:1; i64 T1=T1_of(n,dx,nd),T3=T3_of(n,A3,nd);
              i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
              i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+2;
              i64 a1=0,b1=LIMW+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arrN(m,TB[n].S,nd)+K,u)>=t2lo)b1=m;else a1=m+1;}
              i64 a2=-1,b2=LIMW;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arrN(m,TB[n].S,nd)+K,u)<=t2hi)a2=m;else b2=m-1;}
              if(a1>L)L=a1; if(a2<H)H=a2; }
          gl[t+512]=L; gh[t+512]=H; }
      int bf=0,bs=0; for(int t=-512;t<=511;t++){ if(gl[t+512]<=gh[t+512])bf++; i64 J=J_of(t,nd,18,0);
          if(J>=gl[t+512]&&J<=gh[t+512])bs++; }
      i64 cut=(1ll<<(2*nd-1))-1; int hf=0,hs=0;
      for(int t=-512;t<=511;t++){ i64 L=FL[t+512],H=std::min(FH[t+512],cut); if(L<=H)hf++;
          i64 J=J_of(t,nd,18,0); if(J>=L&&J<=H)hs++; }
      printf("   %2d   %7d  %8d   %10d  %11d\n",nd,bf,bs,hf,hs); }

  // ---------- 4. is the HIGH region (latch >= 2^(2ND-1)) really empty at ND=13? exact, not assumed ----------
  {   const int nd=13; const i64 CUT=1ll<<25;
      i64 fmax[17],fmin[17];
      for(int n=1;n<=16;n++){ if(!TB[n].S){fmax[n]=0;fmin[n]=0;continue;}
          i64 mx=-(1ll<<62), mn=(1ll<<62); int u=U2TAB[n]+2;
          #pragma omp parallel for reduction(max:mx) reduction(min:mn) schedule(static)
          for(i64 m=CUT;m<=LIMW;m++){ i64 v=sm(arrN(m,TB[n].S,nd)+K,u); if(v>mx)mx=v; if(v<mn)mn=v; }
          fmax[n]=mx; fmin[n]=mn; }
      int tHighPossible=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 A3=A3_of(t,dx,nd); bool poss=true;
          for(int node=1;node<=31&&poss;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
              i64 s=flip?-1:1; i64 T1=T1_of(n,dx,nd),T3=T3_of(n,A3,nd);
              i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
              i64 t2lo=base-hi,t2hi=base-lo;
              if(fmax[n]<t2lo||fmin[n]>t2hi) poss=false; }
          if(poss)tHighPossible++; }
      printf("\n4. ND=13 high region [2^25, %lld]: T2 range per node computed exhaustively.\n",(long long)LIMW);
      printf("   node 16: T2 in [%lld,%lld] ; node 1: [%lld,%lld]\n",(long long)fmin[16],(long long)fmax[16],(long long)fmin[1],(long long)fmax[1]);
      printf("   offsets where ANY latch >= 2^25 could satisfy all 31 node instances: %d/1024\n",tHighPossible); }

  // ---------- 5. low region identity: ND=13 == ND=14 for latch < 2^25 ----------
  {   std::mt19937_64 rg(999); long long bad=0,n=0;
      for(int i=0;i<4000000;i++){ i64 m=(i64)(rg()%(1ull<<25)); int nn=1+(int)(rg()%16);
          if(!TB[nn].S)continue; if(arrN(m,TB[nn].S,13)!=arrN(m,TB[nn].S,14))bad++; n++; }
      printf("\n5. arrN(m,S,13) == arrN(m,S,14) for 0<=m<2^25: %lld random checks, %lld mismatches\n",n,bad); }
  return 0; }
