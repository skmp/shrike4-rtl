// f2_cert.cpp -- DIRECTION 2, part A.  The t=342 certificate, re-decided EXHAUSTIVELY and then swept over
// the whole second-stage form space.
//   J = LATCH( ARR(.,.) + K2 , s2 )     ARR = arr(I1,dx) or arr(dx,I1),  dx = t<<g,  g in 8..14
//   LATCH in {floor, toward-zero, one's-complement magnitude}
// For every form, list EXACTLY which offsets admit no integer I1 at all.
// Windows regenerated from the published final multiply at both latch scales (coarse shift 20, fine 18).
#include "f2_core.h"
#include <cstdio>
#include <cstring>
static Fsca F;
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F;
  Fin f{0,0,1,0};                             // the published final multiply
  const char* RN[3]={"floor","toward0","onescompl"};
  FILE* out=fopen("f2_cert_table.txt","w");
  fprintf(out,"# f2_cert: offsets with NO integer I1 at all, exhaustive bracket scan (no monotonicity assumed)\n");
  fprintf(out,"# J = LATCH(ARR + K2, s2); ARR = role0 arr(I1,dx) / role1 arr(dx,I1); dx = t<<g\n");
  fprintf(out,"# maxI1 = largest |I1| the window needs at any t (physical 24-bit operand => must be < 16777216)\n");
  for(int du=2; du>=0; du-=2){
    FinCtx C; fin_build(f,C,du);
    int nf=0; for(int t=-512;t<=511;t++) if(C.JL[t+512]<=C.JH[t+512]) nf++;
    printf("=== latch scale: shift %d (u2=30+k+%d), feasible t = %d ===\n",20-du,du,nf);
    fprintf(out,"\n=== latch scale shift %d (u2 = 30+k+%d), feasible t = %d ===\n",20-du,du,nf);
    for(int role=0;role<2;role++)
     for(int g=8;g<=14;g++)
      for(int s2=g; s2<=g+12; s2++){
       i64 K2s[6]={0,1ll<<19,1ll<<(s2-1),1ll<<18,1ll<<20,(1ll<<s2)-1};
       for(int ki=0;ki<6;ki++){ i64 K2=K2s[ki]; if(ki==2&&s2<1) continue;
        for(int rule=0;rule<3;rule++){
          int none=0,capped=0; i64 maxv=0; int bad[1100]; int nb=0;
          for(int t=-512;t<=511;t++){ int i=t+512; if(t==0) continue; if(C.JL[i]>C.JH[i]) continue;
            i64 dx=(i64)t<<g;
            Cert c=certify(role,dx,K2,s2,rule,C.JL[i],C.JH[i]);
            if(c.need>maxv) maxv=c.need;
            if(c.capped||(c.clamped&&!c.hit)) capped++;
            else if(!c.hit){ if(nb<1100) bad[nb]=t; nb++; none++; } }
          fprintf(out,"role%d g=%2d s2=%2d K2=%-9lld %-10s none=%4d undec=%3d maxI1=%lld",
                  role,g,s2,(long long)K2,RN[rule],none,capped,(long long)maxv);
          if(none){ fprintf(out," :"); for(int j=0;j<nb;j++) fprintf(out," %d",bad[j]); }   // complete list, nothing elided
          fprintf(out,"\n");
        } } }
  }
  fclose(out); puts("wrote f2_cert_table.txt"); return 0; }
