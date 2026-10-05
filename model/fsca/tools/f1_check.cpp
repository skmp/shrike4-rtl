// f1_check.cpp -- validate f1_final.h against the published baseline before trusting any sweep from it.
//   (a) the current model (W=23 RN normalised, u=30+k, arr(J,S), K3=2^19, positive) with chain (s1,s2)=(23,20)
//       must reproduce 147 / 148 window violations and guard nodes 0,1,2,16 exact;
//   (b) the fine-scale dataset (a=32) must reproduce jwin18.txt line for line;
//   (c) f1_T2 must be monotone in J (the binary-search inversion assumes it);
//   (d) for the fixed-point sine ROM, is the field's own leading-zero count equal to the sine's octave k?
#include "f1_final.h"
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute();
    FVar cur{23,0,0,1,30,0,1,0}; Ctx C; f1_build(cur,C,2000);
    for(int ro=0;ro<2;ro++){ Chain ch{23,20,ro,1ll<<19,1ll<<19,0}; int g[4];
        int tot=f1_full(cur,C,ch,g);
        printf("(a) current model, first multiply %s : %d window violations over nodes 0..31"
               "   guards n0=%d n1/31=%d n2/30=%d n16=%d\n", ro?"arr(dx,KC)":"arr(KC,dx)",tot,g[0],g[1],g[2],g[3]); }
    printf("    coarse dataset: %d/1024 t feasible\n",C.feasible);

    FVar fine{23,0,0,1,32,0,1,0}; Ctx F2; f1_build(fine,F2,2000);
    FILE*f=fopen("jwin18.txt","r"); char line[256]; int nmatch=0,nline=0,ndiff=0;
    while(fgets(line,sizeof line,f)){ if(line[0]=='#')continue; long long t,lo,hi,w;
        if(sscanf(line,"%lld %lld %lld %lld",&t,&lo,&hi,&w)!=4)continue; nline++;
        if(F2.JL[t+512]==lo&&F2.JH[t+512]==hi)nmatch++; else if(ndiff++<5)
            printf("    DIFF t=%lld  mine [%lld,%lld]  file [%lld,%lld]\n",t,(long long)F2.JL[t+512],(long long)F2.JH[t+512],lo,hi); }
    fclose(f);
    printf("(b) fine-scale (a=32) dataset vs jwin18.txt: %d/%d lines identical, %d feasible\n",nmatch,nline,F2.feasible);

    // (c) monotonicity of the final product in J, over every variant shape at a sample of nodes
    long viol=0,tested=0;
    for(int fm=0;fm<5;fm++) for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++) for(int role=0;role<2;role++)
      for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++){
        FVar v{W,rm,fm,1,30,role,kc,sg}; i64 S[17];int u[17],kk[17]; f1_field(v,S,u,kk);
        for(int n=1;n<=16;n+=3){ i64 prev=f1_T2(v,0,S[n],u[n]);
            for(i64 J=1;J<(1ll<<27);J+=9973){ i64 c=f1_T2(v,J,S[n],u[n]); tested++; if(c<prev)viol++; prev=c; } } }
    printf("(c) monotonicity of ALIGN(ARR(.)+K3) in J: %ld non-monotone steps out of %ld sampled\n",viol,tested);

    // (d) leading-zero count of the fixed-point field vs the sine's octave
    int mism=0; for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++){ FVar v{W,rm,1,0,30,0,1,0};
        i64 S[17];int u[17],kk[17]; f1_field(v,S,u,kk);
        for(int n=1;n<=16;n++){ int lz=(W-1)-(63-__builtin_clzll((u64)S[n])); if(lz!=kk[n])mism++; } }
    printf("(d) fixed-point sine ROM: lz(field) != sine octave k in %d of %d (W,rm,node) cases\n",mism,7*4*16);
    return 0; }
