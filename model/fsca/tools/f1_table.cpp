#include <string>
// f1_table.cpp -- DIRECTION 1, part A: THE DELIVERABLE TABLE.
// One row per (field width, field rounding, field source/normalisation), best over the remaining free
// parameters (alignment a and b, operand roles, rounding constant K3, sign encoding).  Columns:
//   feasible t ; free-J floor ; FULL model at the PUBLISHED chain with its guard-node count ; best chain
//   latch misses ; and whether t=17, +342, -342 admit an integer I1 -- evaluated ONLY at second shifts where
//   the reachable-J lattice is genuinely coarse (spacing dx/2^s2 >= 1, i.e. s2 <= 21); above that every
//   offset is trivially reachable and the obstruction moves back to the first latch.
// Writes the complete 92400-row table to f1_table.txt.
#include "f1_final.h"
#include <omp.h>
struct Rw { FVar v; int feas; long ff; int pub,pg[4],latch; int a17,a342,am342,joint,ok; };
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute();
    std::vector<FVar> V;
    for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++) for(int fm=0;fm<5;fm++) for(int b=0;b<3;b++)
      for(int d=2;d<=12;d++) for(int role=0;role<2;role++) for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++)
        V.push_back(FVar{W,rm,fm,b,W+d,role,kc,sg});
    std::vector<Rw> R(V.size());
    #pragma omp parallel for schedule(dynamic,32)
    for(size_t i=0;i<V.size();i++){ Rw&r=R[i]; r.v=V[i]; Ctx C0; f1_field(V[i],C0.S,C0.u,C0.kk);
        { Chain ch{23,20,1,1ll<<19,1ll<<19,0}; r.pub=f1_full(V[i],C0,ch,r.pg); }
        Ctx C; r.ok=f1_build(V[i],C,124); r.feas=C.feasible; r.ff=0; r.latch=1024;
        r.a17=r.a342=r.am342=r.joint=0; if(!r.ok) continue;
        r.ff=f1_freefloor(V[i],C);
        int tref=511; while(C.JL[tref+512]>C.JH[tref+512]&&tref>100)tref--;
        long double Jm=(long double)(C.JL[tref+512]+C.JH[tref+512])/2;
        int Ttot=(int)llroundl(logl((long double)KC*(long double)tref*tref*(long double)(1ll<<26)/Jm)/logl(2.0L));
        for(int dt=-1;dt<=1;dt++) for(int s1=16;s1<=30;s1++){ int s2=Ttot+dt-s1; if(s2<10||s2>28)continue;
            for(int ro=0;ro<2;ro++) for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++){
                i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0; int m=f1_chain_score(C,s1,s2,K1,K2,ro);
                if(m<r.latch)r.latch=m; } }
        for(int s2=14;s2<=21;s2++) for(int k2=0;k2<2;k2++){ i64 K2=k2?(1ll<<19):0;
            bool h17=f1_hasI1(C,17,s2,K2),h3=f1_hasI1(C,342,s2,K2),hm=f1_hasI1(C,-342,s2,K2);
            if(h17)r.a17=1; if(h3)r.a342=1; if(hm)r.am342=1; if(h17&&h3&&hm)r.joint=1; } }
    FILE*f=fopen("f1_table.txt","w");
    fprintf(f,"# every final-multiply representation, 92400 rows.  '-' = fewer than 900 feasible offsets.\n"
              "# W rounding field-source b a role K3 sign | feasible ffloor | FULL@published-chain (n0/n1/n2/n16)"
              " | best-chain latch misses | I1 reachable at t=17 +342 -342 (s2<=21) | all three at one (s2,K2)\n");
    for(size_t i=0;i<R.size();i++){ Rw&r=R[i];
        fprintf(f,"%2d %-8s %-18s b=%d a=%2d %s K3=%-10s %-7s | %4d %4ld | %4d (%d/%d/%d/%d) | %4d | %d %d %d | %d\n",
            r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",KCNAME[r.v.kc],
            SGNAME[r.v.sg],r.ok?r.feas:-1,r.ff,r.pub,r.pg[0],r.pg[1],r.pg[2],r.pg[3],r.ok?r.latch:-1,
            r.a17,r.a342,r.am342,r.joint); }
    fclose(f);
    printf("field  round    source              | best feas  ffloor | best FULL@pub (guards) | guard-clean? | best latch | I1@17 @342 @-342 joint\n");
    for(int W=20;W<=26;W++) for(int fm=0;fm<5;fm++) for(int rm=0;rm<4;rm++){
        int bf=-1; long bff=0; int bp=1<<30,bg[4]={0},bl=1<<30,c17=0,c342=0,cm=0,cj=0,gclean=0,bgp=1<<30;
        for(size_t i=0;i<R.size();i++){ Rw&r=R[i]; if(r.v.W!=W||r.v.rm!=rm||r.v.fm!=fm)continue;
            if(r.ok&&r.feas>bf){bf=r.feas;bff=r.ff;} if(r.ok&&r.latch<bl)bl=r.latch;
            if(r.pub<bp){bp=r.pub;memcpy(bg,r.pg,sizeof bg);}
            if((r.pg[0]|r.pg[1]|r.pg[2]|r.pg[3])==0){gclean++; if(r.pub<bgp)bgp=r.pub;}
            c17|=r.a17;c342|=r.a342;cm|=r.am342;cj|=r.joint; }
        printf("W=%-3d %-8s %-18s | %4d %6ld | %4d (%d/%d/%d/%d) | %11s | %4d | %d %d %d %d\n",
            W,RMNAME[rm],FMNAME[fm],bf,bff,bp,bg[0],bg[1],bg[2],bg[3],
            gclean?(std::to_string(gclean)+" @ "+std::to_string(bgp)).c_str():"none",
            bl==(1<<30)?-1:bl,c17,c342,cm,cj); }
    return 0; }
