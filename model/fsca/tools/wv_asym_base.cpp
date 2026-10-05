// wv_asym_base.cpp -- adversarial verification, part 1:
//   (a) rebuild the per-t feasible latch set independently (no monotonicity assumption)
//       and compare it with the shipped jwin.txt;
//   (b) score the baseline chain and the CLAIMED best rule against it;
//   (c) run the full model end-to-end with each J rule and count window violations
//       over nodes 0..31, both with the fixed baseline table and with a b2-style refit.
#include "wv_asym_win.h"
#include <cstring>
#include <cstdio>

typedef i64 (*Jrule)(int);

// --- rules -----------------------------------------------------------------
static i64 J_baseline(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static i64 J_baseline_nok2(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return arr(I1,dx)>>20; }
// CLAIM, transcribed from the description alone:
//   I1 = (arr(KC, t<<14) + 2^20) >> 24 ; J = (arr(I1, t<<15) + 2^20) >> 22 ; KC = 0x4EF145
static i64 J_claim(int t){ i64 I1=(arr(KC,(i64)t<<14)+(1ll<<20))>>24; return (arr(I1,(i64)t<<15)+(1ll<<20))>>22; }

// --- full model with a substituted J rule -----------------------------------
static inline i64 V_of(int node,int t,Jrule Jf,i64 C0,int u2,int extra){
    int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 dx=(i64)t<<13,s=flip?-1:1;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
    i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(Jf(t),T.S)+K,u2+extra):0;
    return C0+s*T1-T2-s*T3; }

static int full_viol(Jrule Jf,int extra,int per_node[32],bool refit,i64 outC0[17],int outU2[17]){
    int tot=0; for(int i=0;i<32;i++) per_node[i]=0;
    // group node instances by table n (n and 32-n share C0/u2), exactly as b2.cpp does
    for(int n=0;n<=16;n++){
        int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        int a0=(n*1024)&0xffff; DWin w0=wv_dumpwin(a0);
        i64 bc=WV_C0[n]; int bu=WV_U2[n]; int bv=1<<30; int bper[2]={0,0};
        i64 clo = refit? w0.lo-3 : WV_C0[n], chi = refit? w0.hi+3 : WV_C0[n];
        int ulo = refit? TB[n].u2-1 : WV_U2[n], uhi = refit? TB[n].u2+1 : WV_U2[n];
        if(n==0){ clo=chi=WV_C0[0]; ulo=uhi=WV_U2[0]; }   // node 0 has no square term
        for(i64 c=clo;c<=chi;c++) for(int u=ulo;u<=uhi;u++){
            int v=0,per[2]={0,0};
            for(int i=0;i<nn;i++){ int node=nodes[i];
                for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; DWin w=wv_dumpwin(a);
                    i64 V=V_of(node,t,Jf,c,u,extra); if(V<w.lo||V>w.hi){v++;per[i]++;} } }
            if(v<bv){bv=v;bc=c;bu=u;bper[0]=per[0];bper[1]=per[1];} }
        tot+=bv; per_node[nodes[0]]=bper[0]; if(nn==2) per_node[nodes[1]]=bper[1];
        if(outC0)outC0[n]=bc; if(outU2)outU2[n]=bu; }
    return tot; }

static void report(const char*name,Jrule Jf,int extra){
    int sc=wv_score(Jf);
    int pn[32]; i64 c0[17]; int u2[17];
    int vfix=full_viol(Jf,extra,pn,false,nullptr,nullptr);
    int pn2[32]; int vref=full_viol(Jf,extra,pn2,true,c0,u2);
    printf("%-26s  jwin=%4d/1022   full(fixed tables)=%4d   full(b2 refit)=%4d   guard(0,1,2,16)= %d %d %d %d (fixed) / %d %d %d %d (refit)\n",
        name,sc,vfix,vref,pn[0],pn[1],pn[2],pn[16],pn2[0],pn2[1],pn2[2],pn2[16]);
    bool moved=false; for(int n=0;n<=16;n++) if(c0[n]!=WV_C0[n]||u2[n]!=WV_U2[n]) moved=true;
    if(moved){ printf("    refit moved tables:"); for(int n=0;n<=16;n++) if(c0[n]!=WV_C0[n]||u2[n]!=WV_U2[n])
            printf(" n%d(C0 %+lld,u2 %+d)",n,(long long)(c0[n]-WV_C0[n]),u2[n]-WV_U2[n]); puts(""); }
}

int main(){ if(!load_fsca(WVF)){puts("no dump");return 1;} build_tables();
    // -- 1. independent window build, compared to the shipped jwin.txt
    wv_build(0);
    int clipped=0,noncontig=0,nfeas=0;
    for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; nfeas++;
        i64 Jc=(KC*(i64)t*(i64)t)>>17; if(WV_LO[t+512]<=std::max((i64)0,Jc-600)+1||WV_HI[t+512]>=Jc+599) clipped++;
        if(!WV_CONTIG[t+512]) noncontig++; }
    printf("independent build: feasible t = %d/1024, window clipped by the search margin = %d, non-contiguous feasible sets = %d\n",
        nfeas,clipped,noncontig);
    FILE*f=fopen("jwin.txt","r"); int mism=0,lines=0; if(f){ int t;long long a,b;int ok;
        while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)==4){ lines++;
            bool myok=WV_OK[t+512]; if((int)myok!=ok||(myok&&(a!=WV_LO[t+512]||b!=WV_HI[t+512]))){
                if(mism<8) printf("  MISMATCH t=%d file[%lld,%lld,%d] mine[%lld,%lld,%d]\n",t,a,b,ok,
                    (long long)WV_LO[t+512],(long long)WV_HI[t+512],(int)myok); mism++; } }
        fclose(f); }
    printf("jwin.txt: %d lines, %d disagreements with my independent build\n",lines,mism);
    int infeas=0; for(int t=-512;t<=511;t++) if(!WV_OK[t+512]){ if(infeas<8) printf("  infeasible t=%d\n",t); infeas++; }
    printf("infeasible t (mine): %d\n",infeas);

    puts("");
    puts("--- scoring (shift-20 latch scale, the claim's contract) ---");
    report("baseline chain (23,20)",J_baseline,0);
    report("baseline, K2 dropped",J_baseline_nok2,0);
    report("CLAIM best rule",J_claim,0);

    // free-J floor control (the 'theoretical floor 2' number) with fixed tables
    { int pn[32]; (void)pn; int tot=0;
      for(int t=-512;t<=511;t++) if(!WV_OK[t+512]) tot++;   // each infeasible t costs >=1 node
      printf("\nt with no feasible J at shift 20: %d  (these are the 'floor 2' samples)\n",tot); }
    return 0; }
