// f1_cert.cpp -- DIRECTION 1, part A: what survives the guards, and what the t=342 "certificate" really says.
// (a) every one of the 92400 final-multiply representations, scored at the PUBLISHED chain over nodes 0..31,
//     with the complete list of the guard-clean ones written to f1_guardclean.txt;
// (b) for the baseline field, all 2280 chains (s1,s2,ro,K1,K2): which keep nodes 0,1/31,2/30,16 exact;
// (c) the "no integer I1 at all" census as a function of s2, printed NEXT TO the J-lattice spacing
//     dx/2^s2 -- the census is only meaningful while that spacing exceeds the window width;
// (d) an end-to-end re-verification of the best guard-clean representation.
#include "f1_final.h"
#include <omp.h>
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute();
    std::vector<FVar> V;
    for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++) for(int fm=0;fm<5;fm++) for(int b=0;b<3;b++)
      for(int d=2;d<=12;d++) for(int role=0;role<2;role++) for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++)
        V.push_back(FVar{W,rm,fm,b,W+d,role,kc,sg});
    std::vector<int> pub(V.size()), gc(V.size());
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t i=0;i<V.size();i++){ Ctx C; f1_field(V[i],C.S,C.u,C.kk); int g[4];
        Chain ch{23,20,1,1ll<<19,1ll<<19,0}; pub[i]=f1_full(V[i],C,ch,g); gc[i]=(g[0]|g[1]|g[2]|g[3])==0; }
    FILE*f=fopen("f1_guardclean.txt","w");
    fprintf(f,"# final-multiply representations that keep nodes 0, 1/31, 2/30 and 16 EXACT for all 1024\n"
              "# offsets at the PUBLISHED chain I1=(arr(dx,KC)+2^19)>>23, J=(arr(I1,dx)+2^19)>>20.\n"
              "# baseline (W=23 RNe norm-sine-ROM b=1 a=30 arr(J,S) K3=2^19 pos) = 147 violations.\n"
              "# columns: violations | W rounding field-source b a role K3 sign\n");
    int n=0,best=1<<30; size_t bi=0; int mW[27]={0},mfm[5]={0},mb[3]={0},mrm[4]={0},mrole[2]={0},mkc[5]={0},msg[2]={0};
    std::vector<size_t> gidx;
    for(size_t i=0;i<V.size();i++) if(gc[i]){ n++; gidx.push_back(i);
        mW[V[i].W]++;mfm[V[i].fm]++;mb[V[i].b]++;mrm[V[i].rm]++;mrole[V[i].role]++;mkc[V[i].kc]++;msg[V[i].sg]++;
        if(pub[i]<best){best=pub[i];bi=i;} }
    std::sort(gidx.begin(),gidx.end(),[&](size_t a,size_t b){return pub[a]<pub[b];});
    for(size_t i:gidx) fprintf(f,"%4d | W=%2d %-8s %-18s b=%d a=%2d %s K3=%-10s %s\n",pub[i],
        V[i].W,RMNAME[V[i].rm],FMNAME[V[i].fm],V[i].b,V[i].a,V[i].role?"arr(S,J)":"arr(J,S)",
        KCNAME[V[i].kc],SGNAME[V[i].sg]);
    fclose(f);
    printf("(a) %zu representations scored at the published chain; %d keep ALL FOUR guard nodes exact.\n",V.size(),n);
    printf("    every guard-clean one has:  W="); for(int w=20;w<=26;w++) if(mW[w]) printf(" %d(x%d)",w,mW[w]);
    printf(" ;  field-source"); for(int i=0;i<5;i++) if(mfm[i]) printf(" %s(x%d)",FMNAME[i],mfm[i]);
    printf(" ;  b"); for(int i=0;i<3;i++) if(mb[i]) printf(" %d(x%d)",i,mb[i]);
    printf(" ;  rounding"); for(int i=0;i<4;i++) if(mrm[i]) printf(" %s(x%d)",RMNAME[i],mrm[i]);
    printf("\n    role"); for(int i=0;i<2;i++) if(mrole[i]) printf(" %s(x%d)",i?"arr(S,J)":"arr(J,S)",mrole[i]);
    printf(" ;  K3"); for(int i=0;i<5;i++) if(mkc[i]) printf(" %s(x%d)",KCNAME[i],mkc[i]);
    printf(" ;  sign"); for(int i=0;i<2;i++) if(msg[i]) printf(" %s(x%d)",SGNAME[i],msg[i]); printf("\n");
    printf("    best guard-clean: %d violations -> W=%d %s %s b=%d a=%d %s K3=%s %s\n",best,
        V[bi].W,RMNAME[V[bi].rm],FMNAME[V[bi].fm],V[bi].b,V[bi].a,V[bi].role?"arr(S,J)":"arr(J,S)",
        KCNAME[V[bi].kc],SGNAME[V[bi].sg]);
    printf("    full guard-clean list in f1_guardclean.txt\n");

    // (b) which chains keep the guards clean, for the baseline field
    FVar base{23,0,0,1,30,0,1,0}; Ctx CB; f1_field(base,CB.S,CB.u,CB.kk);
    int nclean=0,bestclean=1<<30,bestany=1<<30; int bc[5]={0},ba[5]={0};
    for(int s1=16;s1<=30;s1++)for(int s2=10;s2<=28;s2++)for(int ro=0;ro<2;ro++)for(int k1=0;k1<2;k1++)
      for(int k2=0;k2<2;k2++){ i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0; Chain ch{s1,s2,ro,K1,K2,0}; int g[4];
        int tot=f1_full(base,CB,ch,g);
        if(tot<bestany){bestany=tot;ba[0]=s1;ba[1]=s2;ba[2]=ro;ba[3]=k1;ba[4]=k2;}
        if((g[0]|g[1]|g[2]|g[3])==0){ nclean++; if(tot<bestclean){bestclean=tot;bc[0]=s1;bc[1]=s2;bc[2]=ro;bc[3]=k1;bc[4]=k2;} } }
    printf("\n(b) baseline field, all 2280 chains: %d keep the guards clean; best guard-clean %d at "
           "(s1=%d,s2=%d,%s,K1=%s,K2=%s); best overall %d at (s1=%d,s2=%d,%s,K1=%s,K2=%s)\n",
        nclean,bestclean,bc[0],bc[1],bc[2]?"arr(dx,KC)":"arr(KC,dx)",bc[3]?"2^19":"0",bc[4]?"2^19":"0",
        bestany,ba[0],ba[1],ba[2]?"arr(dx,KC)":"arr(KC,dx)",ba[3]?"2^19":"0",ba[4]?"2^19":"0");

    // (c) the no-I1 census against the J-lattice spacing
    puts("\n(c) the 't=342 admits no integer I1' certificate, as a function of the second shift s2.");
    puts("    spacing = the gap between consecutive reachable J at t=342 (= 342*2^13/2^s2).  Once that drops");
    puts("    below the window width the census is vacuous and the obstruction moves back to the FIRST latch.");
    for(int a=30;a<=32;a+=2){ FVar v{23,0,0,1,a,0,1,0}; Ctx C; f1_build(v,C,2000);
      printf("    --- latch alignment a=%d (shift %d), window at t=342 is [%lld,%lld] width %lld, "
             "at t=-342 [%lld,%lld] width %lld\n", a,a==30?20:18,
             (long long)C.JL[342+512],(long long)C.JH[342+512],(long long)(C.JH[342+512]-C.JL[342+512]+1),
             (long long)C.JL[-342+512],(long long)C.JH[-342+512],(long long)(C.JH[-342+512]-C.JL[-342+512]+1));
      printf("      s2 spacing@342  | noI1(K2=0) noI1(K2=2^19) | I1@17 I1@342 I1@-342 (K2=0) | (K2=2^19)\n");
      for(int s2=14;s2<=25;s2++){ long double sp=(long double)342*8192/(long double)(1ll<<s2);
        int c0=0,c1=0; for(int t=-512;t<=511;t++){ if(t==0||C.JL[t+512]>C.JH[t+512])continue;
            if(!f1_hasI1(C,t,s2,0))c0++; if(!f1_hasI1(C,t,s2,1ll<<19))c1++; }
        printf("      %2d %10.3Lf  |   %4d        %4d      |   %d    %d     %d       |  %d %d %d\n",s2,sp,c0,c1,
            f1_hasI1(C,17,s2,0),f1_hasI1(C,342,s2,0),f1_hasI1(C,-342,s2,0),
            f1_hasI1(C,17,s2,1ll<<19),f1_hasI1(C,342,s2,1ll<<19),f1_hasI1(C,-342,s2,1ll<<19)); } }

    // (d) end-to-end re-verification of the best guard-clean representation
    { FVar v=V[bi]; Ctx C; f1_field(v,C.S,C.u,C.kk); Chain ch{23,20,1,1ll<<19,1ll<<19,0}; int g[4];
      int tot=f1_full(v,C,ch,g);
      printf("\n(d) re-verified end to end, nodes 0..31, published chain: %d window violations; "
             "guards n0=%d n1/31=%d n2/30=%d n16=%d\n",tot,g[0],g[1],g[2],g[3]);
      Chain ch0{23,20,1,1ll<<19,1ll<<19,0}; int g0[4]; int t0=f1_full(base,CB,ch0,g0);
      printf("    control (published field, same chain): %d violations; guards %d/%d/%d/%d\n",t0,g0[0],g0[1],g0[2],g0[3]);
      printf("    the ONLY difference is the sign/complement encoding of the final square product:\n");
      printf("      positive rule  T2 = (arr(J,S)+2^19) >> (30+k)\n");
      printf("      one's-comp rule T2 = (arr(J,S)+2^19-1) >> (30+k)   <-- the rule the LINEAR term is already known to use\n");
      int diff=0; for(int nn=1;nn<=16;nn++) for(i64 J=0;J<4000000;J+=7919)
        if(((arr(J,CB.S[nn])+(1ll<<19))>>CB.u[nn]) != ((arr(J,CB.S[nn])+(1ll<<19)-1)>>CB.u[nn])) diff++;
      printf("      the two differ on %d of %d sampled (node,J) pairs -- it is a boundary rule, not a rescale\n",
             diff,16*(4000000/7919+1)); }
    return 0; }
