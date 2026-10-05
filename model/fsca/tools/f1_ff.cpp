// f1_ff.cpp -- DIRECTION 1, part A: the patent's FRACTION FEEDBACK form of the final square product.
// US6038582 FIG.28 step 3 computes the even half as ONE product (1 - 2*pi^2*dx^2) * sin, the "1" arriving
// free because only the fraction bits of the NEGATIVE square product are fed back ("upper bit is set zero
// ... obtaining the same effect as +1").  So C0 and T2 are not two adder inputs but one truncated product:
//     Fop  = fraction field of the complemented square latch, width m   (one's complement: 2^m-1-J)
//     EVEN = ALIGN( ARR(Fop,S) + K3 , u )        replaces  C0_n - T2
// Stage 1 is the cheapest and sharpest test in the whole problem: at t = 0 the latch is 0, so EVEN must
// reproduce the 17 EXACT hardware table values C0_n with no freedom at all.
// Stage 2 builds the full latch dataset for anything that survives stage 1.
#include "f1_final.h"
static const char* FFNAME[2]={"normalised sine ROM","fixed-point sine ROM"};
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute();
    struct Cand{int W,rm,fm,b,a,m,role,ki,comp,hit;};
    std::vector<Cand> pass; int tried=0, best=0;
    int histo[18]={0};
    for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++) for(int fm=0;fm<2;fm++) for(int m=18;m<=36;m++)
      for(int b=0;b<3;b++) for(int a=W+m-26-4;a<=W+m-26+4;a++) for(int role=0;role<2;role++)
        for(int ki=0;ki<7;ki++) for(int comp=0;comp<2;comp++){
          if(a<1||a>58) continue;
          FVar v{W,rm,fm,b,a,role,1,0}; i64 S[17]; int u[17],kk[17]; f1_field(v,S,u,kk);
          if(role==0 && m>=27) continue;                 // Fop cannot be the recoded operand above 2^27
          tried++;
          i64 Fop0 = comp==0 ? ((1ll<<m)-1) : 0;         // t=0: latch 0 -> one's complement all-ones, two's 0
          int hit=0;
          for(int n=1;n<=16;n++){ int uu=u[n]; if(uu<1||uu>58){hit=-1;break;}
            i64 K3; switch(ki){ case 0:K3=0;break; case 1:K3=1ll<<19;break; case 2:K3=1ll<<(uu-1);break;
                case 3:K3=1ll<<16;break; case 4:K3=(1ll<<19)+(1ll<<16);break;
                case 5:K3=(1ll<<uu)-1-(1ll<<19);break; default:K3=(1ll<<uu)-1; }
            i64 P = role ? arr(S[n],Fop0) : arr(Fop0,S[n]);
            i64 E=(P+K3)>>uu;
            // honest test: the merged form has no stored C0, so ask only that the OUTPUT lands in the
            // dump's own window at t = 0 (V(n,0) = EVEN exactly: T1 and T3 both vanish at dx = 0)
            if(E>=VLO[512][n] && E<=VHI[512][n]) hit++; }
          if(hit<0) continue; histo[hit]++;
          if(hit>best) best=hit;
          if(hit>=14) pass.push_back(Cand{W,rm,fm,b,a,m,role,ki,comp,hit}); }
    printf("=== fraction-feedback merge of C0 and T2 (patent FIG.28) ===\n");
    printf("stage 1: %d structures tested against the 16 t=0 OUTPUT WINDOWS straight from the dump\n",tried);
    printf("  best = %d of 16 windows hit;  distribution of hits:\n   ",best);
    for(int h=0;h<=16;h++) if(histo[h]) printf(" %d:%d",h,histo[h]); printf("\n");
    printf("  structures reaching >= 14 of 16: %zu\n",pass.size());
    // the two's-complement branch, separately: it must fail at t=0 by construction (Fop = 0)
    { int tc=0; for(auto&c:pass) if(c.comp==1) tc++; printf("  of those, two's complement: %d (one's complement: %zu)\n",tc,pass.size()-tc); }
    if(pass.empty()){ puts("  -> the merged form cannot reproduce the C0 table at t = 0 under ANY of these\n"
                          "     widths, roundings, alignments, fraction widths, roles, constants or complements."); }
    // stage 2 for anything that survived
    int shown=0;
    for(auto&c:pass){ if(shown++>=12) break;
        printf("  survivor: W=%d %s %s b=%d a=%d m=%d %s K3[%d] %s -> %d/16 at t=0\n",
            c.W,RMNAME[c.rm],FFNAME[c.fm],c.b,c.a,c.m,c.role?"arr(S,Fop)":"arr(Fop,S)",c.ki,
            c.comp?"two's":"one's",c.hit); }
    // For the record: the SEPARATE form reads C0 straight from the table, so it is exact at t = 0 for all 17
    // nodes by construction.  Quantify how much resolution the merge would cost at the demanded fraction width.
    puts("\nwhy the merge cannot carry the demanded resolution:");
    printf("  the square latch must resolve J(17) = 45617 at the fine scale, i.e. 2*pi^2*dx^2 * 2^35 at t=17;\n");
    for(int m=20;m<=36;m+=2){ long double q = (long double)(1ll<<m)*19.7392088L/ (long double)(1ll<<32);
        printf("  fraction width m=%2d : 2*pi^2*dx^2 * 2^m at t=17 is %.3Lf latch steps, at t=512 it is %.1Lf; "
               "operand width needed %d bits%s\n", m, q*17*17, q*512*512, m, m>=27?"  (> the 14-digit Booth recoder)":""); }
    return 0; }
