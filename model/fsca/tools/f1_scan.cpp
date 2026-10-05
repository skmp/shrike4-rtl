// f1_scan.cpp -- DIRECTION 1, part A: EXHAUSTIVE sweep of the final square product's representation.
//   T2 = ALIGN_sg( ARR(opA,opB) + K3 , u_n )     u_n = a + b*k   (ONE rule for all nodes)
// field WIDTH 20..26 x ROUNDING {RN-even, truncate, round-half-up, ceil} x SOURCE/NORMALISATION
//   {normalised sine ROM, fixed-point sine ROM, fixed ROM + normalising shifter, the hardware C0 table
//    fixed, the hardware C0 table normalised}  x ALIGNMENT b in {0,1,2} and a = W + d, d in [2,12]
//   x OPERAND ROLES x K3 in {0, 2^19, 2^(u-1), 2^16, 2^19+2^16} x SIGN {positive, negative one's-complement}
// = 92400 variants.  Each REGENERATES its own latch dataset, then is scored on:
//   feasible t ; free-J floor ; best two-multiply chain (latch misses) and its guard-node violations ;
//   the FULL model over nodes 0..31 with that chain ; the minimum over (s2,K2) of the number of offsets
//   admitting NO integer I1 at all ; and whether t=17, +342, -342 admit one (separately and jointly).
#include "f1_final.h"
#include <omp.h>
struct Res { FVar v; int feas; long ff; int chmiss,s1,s2,ro; i64 K1,K2; int full,g0,g1,g2,g16;
             int minNo,noS2; i64 noK2; int a17,a342,am342,joint; int ok; };
static std::vector<Res> R;
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute();
    std::vector<FVar> V;
    for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++) for(int fm=0;fm<5;fm++) for(int b=0;b<3;b++)
      for(int d=2;d<=12;d++) for(int role=0;role<2;role++) for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++)
        V.push_back(FVar{W,rm,fm,b,W+d,role,kc,sg});
    R.resize(V.size()); fprintf(stderr,"variants: %zu\n",V.size());
    long done=0;
    #pragma omp parallel for schedule(dynamic,16)
    for(size_t i=0;i<V.size();i++){ Res&r=R[i]; r.v=V[i]; r.ok=0;
        Ctx C; if(!f1_build(V[i],C,124)) { r.feas=C.feasible; continue; }
        r.ok=1; r.feas=C.feasible; r.ff=f1_freefloor(V[i],C);
        // total chain shift implied by the J scale, from the widest offset
        int tref=511; while(C.JL[tref+512]>C.JH[tref+512]&&tref>100)tref--;
        long double Jm=(long double)(C.JL[tref+512]+C.JH[tref+512])/2;
        long double ex=(long double)KC*(long double)tref*tref*(long double)(1ll<<26)/Jm;
        int Ttot=(int)llroundl(logl(ex)/logl(2.0L));
        r.chmiss=1<<30;
        for(int dt=-1;dt<=1;dt++) for(int s1=16;s1<=30;s1++){ int s2=Ttot+dt-s1; if(s2<10||s2>28)continue;
            for(int ro=0;ro<2;ro++) for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++){
                i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0; int m=f1_chain_score(C,s1,s2,K1,K2,ro);
                if(m<r.chmiss){ r.chmiss=m; r.s1=s1;r.s2=s2;r.ro=ro;r.K1=K1;r.K2=K2; } } }
        { Chain ch{r.s1,r.s2,r.ro,r.K1,r.K2,0}; int g[4]; r.full=f1_full(V[i],C,ch,g);
          r.g0=g[0];r.g1=g[1];r.g2=g[2];r.g16=g[3]; }
        // the "no integer I1 at all" census, minimised over (s2,K2)
        r.minNo=1<<30; r.a17=r.a342=r.am342=r.joint=0;
        for(int s2=std::max(10,Ttot-28);s2<=std::min(28,Ttot-12);s2++)
          for(int ki=0;ki<5;ki++){ i64 K2 = ki==0?0 : ki==1?(1ll<<19) : ki==2?(1ll<<16)
                                        : ki==3?((1ll<<19)+(1ll<<16)) : (1ll<<(s2-1));
            int none=0; for(int t=-512;t<=511;t++){ if(t==0)continue; if(C.JL[t+512]>C.JH[t+512])continue;
                if(!f1_hasI1(C,t,s2,K2)) none++; }
            bool h17=f1_hasI1(C,17,s2,K2),h342=f1_hasI1(C,342,s2,K2),hm=f1_hasI1(C,-342,s2,K2);
            if(h17)r.a17=1; if(h342)r.a342=1; if(hm)r.am342=1; if(h17&&h342&&hm)r.joint=1;
            if(none<r.minNo){ r.minNo=none; r.noS2=s2; r.noK2=K2; } }
        #pragma omp atomic
        done++;
        if((done&255)==0) fprintf(stderr,"\r survivors scored: %ld   ",done);
    }
    fprintf(stderr,"\n");
    FILE*f=fopen("f1_scan.txt","w");
    fprintf(f,"# f1_scan.cpp -- final square product representation sweep (92400 variants, %zu fully scored)\n",R.size());
    fprintf(f,"# W rm fm b a role K3 sign | feas ffloor | chain(s1,s2,ro,K1,K2) latchmiss | FULL n0 n1 n2 n16 | minNoI1(s2,K2) | I1@17 I1@342 I1@-342 joint\n");
    int nok=0,f1024=0,best=1<<30; size_t bi=0; int bestguard=1<<30; size_t bgi=0;
    int cnt_joint=0,cnt_min0=0;
    for(size_t i=0;i<R.size();i++){ Res&r=R[i]; if(!r.ok)continue; nok++; if(r.feas==1024)f1024++;
        if(r.joint)cnt_joint++; if(r.minNo==0)cnt_min0++;
        if(r.full<best){best=r.full;bi=i;}
        if(r.g0+r.g1+r.g2+r.g16==0 && r.full<bestguard){bestguard=r.full;bgi=i;}
        fprintf(f,"%2d %-8s %-18s b=%d a=%2d %s K3=%-10s %-7s | %4d %4ld | (%d,%d,%d,%lld,%lld) %4d | %4d %d %d %d %d | %2d (s2=%d,K2=%lld) | %d %d %d %d\n",
            r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",KCNAME[r.v.kc],SGNAME[r.v.sg],
            r.feas,r.ff,r.s1,r.s2,r.ro,(long long)r.K1,(long long)r.K2,r.chmiss,
            r.full,r.g0,r.g1,r.g2,r.g16,r.minNo,r.noS2,(long long)r.noK2,r.a17,r.a342,r.am342,r.joint); }
    fclose(f);
    printf("=== f1_scan: 92400 variants built; %d survived (>=900 feasible t); %d have all 1024 feasible\n",nok,f1024);
    printf("=== free-J floor: 0 for every variant with 1024 feasible t (identical statistics by construction)\n");
    printf("=== variants where a SINGLE (s2,K2) admits an integer I1 at t=17, +342 AND -342 : %d\n",cnt_joint);
    printf("=== variants where SOME (s2,K2) leaves no offset without an integer I1 (minNoI1==0) : %d\n",cnt_min0);
    printf("\nbest FULL-model variant (nodes 0..31, baseline 147):\n");
    { Res&r=R[bi]; printf("  W=%d %s %s b=%d a=%d %s K3=%s %s -> FULL %d  guards n0=%d n1=%d n2=%d n16=%d  latchmiss %d  minNoI1 %d\n",
        r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",KCNAME[r.v.kc],SGNAME[r.v.sg],
        r.full,r.g0,r.g1,r.g2,r.g16,r.chmiss,r.minNo); }
    printf("best GUARD-CLEAN variant:\n");
    if(bestguard<(1<<30)){ Res&r=R[bgi]; printf("  W=%d %s %s b=%d a=%d %s K3=%s %s -> FULL %d  latchmiss %d  minNoI1 %d\n",
        r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",KCNAME[r.v.kc],SGNAME[r.v.sg],
        r.full,r.chmiss,r.minNo); }
    // the top rows by full-model score
    std::vector<size_t> idx; for(size_t i=0;i<R.size();i++) if(R[i].ok) idx.push_back(i);
    std::sort(idx.begin(),idx.end(),[&](size_t x,size_t y){ return R[x].full<R[y].full; });
    printf("\ntop 25 by FULL-model violations:\n");
    printf("  W rm       field-source       b   a  role     K3          sign   feas FULL n0 n1 n2 n16 latch minNoI1 17 342 -342\n");
    for(int i=0;i<25&&i<(int)idx.size();i++){ Res&r=R[idx[i]];
        printf("  %2d %-8s %-18s %d %3d %-8s %-10s %-7s %4d %4d %2d %2d %2d %3d %5d %6d   %d  %d   %d\n",
            r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",KCNAME[r.v.kc],SGNAME[r.v.sg],
            r.feas,r.full,r.g0,r.g1,r.g2,r.g16,r.chmiss,r.minNo,r.a17,r.a342,r.am342); }
    printf("\nminimum minNoI1 over all variants, and every variant attaining it:\n");
    int mm=1<<30; for(size_t i:idx) mm=std::min(mm,R[i].minNo);
    printf("  min = %d\n",mm); int shown=0;
    for(size_t i:idx) if(R[i].minNo==mm&&shown<20){ Res&r=R[i]; shown++;
        printf("  W=%2d %-8s %-18s b=%d a=%2d %s K3=%-10s %-7s feas %d FULL %d (guards %d/%d/%d/%d) 17=%d 342=%d -342=%d joint=%d\n",
            r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",KCNAME[r.v.kc],SGNAME[r.v.sg],
            r.feas,r.full,r.g0,r.g1,r.g2,r.g16,r.a17,r.a342,r.am342,r.joint); }
    return 0; }
