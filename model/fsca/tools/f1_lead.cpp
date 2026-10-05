// f1_lead.cpp -- DIRECTION 1: is "RN22 normalised, u=29+k" (the only e7 variant where t=342 admits an
// integer I1) a STRUCTURAL finding or merely a looser constraint?
// Decisive test: RN22 and RN23 have the SAME dT2/dJ (S halves, u decrements), so their J windows are on the
// same scale and are directly comparable.  If RN22's window at t=342 is a SUPERSET of RN23's pinned value,
// the "yes" is pure loosening.  If it excludes it, the variant predicts a different J and is structural.
#include "e_final.h"
static Fsca Fdump;

static void stats(const char* tag,int sv,int r3,int k3,int sg){
    Fin f{sv,r3,k3,sg}; FinCtx C; fin_build(f,C,2);
    long long sw=0; int tight=0,w1=0;
    for(int t=-512;t<=511;t++){ int i=t+512; if(C.JL[i]<=C.JH[i]){ long long w=C.JH[i]-C.JL[i]+1; sw+=w;
        if(w<=4)tight++; if(w==1)w1++; } }
    printf("\n=== %s :: %s  r3=%d k3=%d sg=%d\n",tag,SVNAME[sv],r3,k3,sg);
    printf("    feasible %d/1024   mean window %.2f   #width==1 %d   #width<=4 %d\n",
        C.feasible,(double)sw/C.feasible,w1,tight);
    printf("    field:"); for(int n=1;n<=16;n++) printf(" %lld/%d",(long long)C.S[n],C.u[n]); printf("\n");
    const int ts[8]={17,-17,342,-342,385,-385,100,-100};
    for(int q=0;q<8;q++){ int t=ts[q],i=t+512;
        printf("    t=%4d  J in [%12lld,%12lld]  width %lld\n",t,(long long)C.JL[i],(long long)C.JH[i],
            (long long)(C.JH[i]-C.JL[i]+1)); }
    // reachability of the window by the standard second stage, at t = 17, 342, -342
    const int tr[3]={17,342,-342};
    for(int q=0;q<3;q++){ int t=tr[q],i=t+512; i64 dx=(i64)t<<13;
        for(int s2=16;s2<=20;s2++) for(int k2=0;k2<2;k2++){ i64 K2=k2?(1ll<<19):0;
            // arr(v,dx) monotone in v; direction from dx sign
            auto Jof=[&](i64 v){ return (arr(v,dx)+K2)>>s2; };
            bool inc = Jof(1000)>Jof(0); i64 lim=200000000ll, a1,b1,a2,b2;
            if(inc){ a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)>=C.JL[i])b1=m;else a1=m+1;}
                     a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)<=C.JH[i])a2=m;else b2=m-1;} }
            else   { a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)<=C.JH[i])b1=m;else a1=m+1;}
                     a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)>=C.JL[i])a2=m;else b2=m-1;} }
            bool any=(a1<=a2);
            printf("      t=%4d s2=%2d K2=%-7lld : %s", t,s2,(long long)K2, any?"I1 EXISTS":"no I1     ");
            if(any) printf("  I1 in [%lld,%lld]  J=%lld",(long long)a1,(long long)a2,(long long)Jof(a1));
            else { // print the two reachable J straddling the window
                i64 v = inc? a2 : a1; printf("  nearest reachable J: %lld and %lld (window [%lld,%lld])",
                    (long long)Jof(v),(long long)Jof(v+1),(long long)C.JL[i],(long long)C.JH[i]); }
            printf("\n"); } }
}
int main(){ if(!load_fsca(Fdump))return 1; build_tables(); FIN_F=&Fdump;
    puts("Comparing the published field (RN23,u=30+k) with the lead (RN22,u=29+k) at matched r3/k3/sg.");
    stats("BASE",0,0,1,0);
    stats("LEAD",8,0,1,0);
    stats("BASE-k3=0",0,0,0,0);
    stats("LEAD-k3=0",8,0,0,0);
    return 0; }
