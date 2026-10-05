// f2_cross.cpp -- DIRECTION 2, part C.  The certificate crossed with the final multiply.
// The latch window itself is a consequence of T2 = ALIGN(ARR(J,S)+K3,u), so every claim about J has to be
// re-decided for each final-multiply variant in e_final.h (9 sine fields x 2 roles x 3 rounding constants
// x 2 sign encodings = 108).  For each variant, and every second-stage form (role, g, s2) with the rounding
// constant K2 = q*2^s2 + r EXHAUSTED over r in [0,2^s2) and q in [-4,8], we ask the headline question:
//     is there a K2 making t=342, t=-342 and t=17 simultaneously admissible -- and all 1024 offsets?
#include "f2_core.h"
#include <cstring>
#include <algorithm>
#include <omp.h>
#include <string>
static Fsca F;
static const i64 DSLACK = 1ll<<20;
struct BM { std::vector<uint64_t> w; int s;
  void init(int s_,bool one){ s=s_; w.assign((size_t)((1ull<<s)/64),one?~0ull:0ull);}
  void setr(i64 a,i64 b){ if(a<0)a=0; i64 top=(1ll<<s)-1; if(b>top)b=top; if(a>b)return;
    for(i64 i=a;i<=b;){ if((i&63)==0 && b-i>=63){ w[i>>6]=~0ull; i+=64; } else { w[i>>6]|=1ull<<(i&63); i++; } } }
  void andwith(const BM&o){ for(size_t i=0;i<w.size();i++) w[i]&=o.w[i]; }
  long long pop()const{ long long c=0; for(auto x:w) c+=__builtin_popcountll(x); return c; }
  long long first()const{ for(size_t i=0;i<w.size();i++) if(w[i]) return (long long)i*64+__builtin_ctzll(w[i]); return -1; } };
struct Job { int sv,r3,k3,sg; };
int main(){ if(!load_fsca(F))return 1; build_tables(); FIN_F=&F;
  std::vector<Job> jobs; for(int sv=0;sv<NSV;sv++)for(int r3=0;r3<2;r3++)for(int k3=0;k3<3;k3++)for(int sg=0;sg<2;sg++) jobs.push_back({sv,r3,k3,sg});
  std::vector<std::string> outl(jobs.size());
  #pragma omp parallel for schedule(dynamic)
  for(size_t ji=0;ji<jobs.size();ji++){ Job jb=jobs[ji]; Fin f{jb.sv,jb.r3,jb.k3,jb.sg};
    char buf[4096]; std::string s;
    FinCtx C; fin_build(f,C,2);           // fine latch scale: the one where every offset is feasible
    int nf=C.feasible; int npin=0; for(int t=-512;t<=511;t++) if(C.JL[t+512]<=C.JH[t+512]&&C.JH[t+512]-C.JL[t+512]<=3) npin++;
    snprintf(buf,sizeof buf,"sv=%d r3=%d k3=%d sg=%d | feas=%4d pinned(w<=4)=%3d | J(342)=[%lld,%lld] J(-342)=[%lld,%lld] J(17)=[%lld,%lld]\n",
      jb.sv,jb.r3,jb.k3,jb.sg,nf,npin,(long long)C.JL[342+512],(long long)C.JH[342+512],
      (long long)C.JL[-342+512],(long long)C.JH[-342+512],(long long)C.JL[17+512],(long long)C.JH[17+512]); s+=buf;
    if(nf<1024){ s+="   (not all offsets feasible for this final multiply -- skipped)\n"; outl[ji]=s; continue; }
    int bestpinS2=-1,bestpinG=-1,bestpinRole=-1,bestpinQ=0; long long bestpin=0;
    int bA_g=-1,bA_s2=-1,bA_role=-1,bA_q=0; long long bestall=0; long long bA_I1=-1; i64 bA_r=-1;
    int minsg_pin=99, minsg_all=99, minsg_all24=99; std::string g13;
    for(int role=0;role<2;role++) for(int g=8;g<=14;g++) for(int s2=g;s2<=std::min(g+10,22);s2++){
      for(int q=-4;q<=8;q++){
        auto uncon=[&](int t){ i64 dx=(i64)t<<g; i64 Plo,Phi; latch_inv(C.JL[t+512]-q,C.JH[t+512]-q,s2,LR_FLOOR,Plo,Phi);
            return Phi-Plo+1 >= (dx<0?-dx:dx)+DSLACK; };
        auto ufor=[&](int t,BM&U){ U.init(s2,false); i64 dx=(i64)t<<g; i64 Plo,Phi;
            latch_inv(C.JL[t+512]-q,C.JH[t+512]-q,s2,LR_FLOOR,Plo,Phi);
            i64 L=Plo-((1ll<<s2)-1)-EB,H=Phi+EB,lo,hi;
            auto fdv=[](i64 a,i64 b){ i64 x=a/b; if((a%b)&&((a<0)!=(b<0)))x--; return x; };
            auto cdv=[](i64 a,i64 b){ i64 x=a/b; if((a%b)&&((a<0)==(b<0)))x++; return x; };
            if(dx>0){lo=cdv(L,dx);hi=fdv(H,dx);}else{lo=cdv(H,dx);hi=fdv(L,dx);}
            if(lo<-VMAX)lo=-VMAX; if(hi>VMAX)hi=VMAX;
            for(i64 v=lo;v<=hi;v++){ i64 A=ARRof(role,v,dx); U.setr(Plo-A,Phi-A);} };
        BM P3,U; P3.init(s2,true);
        for(int t:{342,-342,17}) if(!uncon(t)){ ufor(t,U); P3.andwith(U); }
        long long np=P3.pop(); if(np>bestpin){bestpin=np;bestpinS2=s2;bestpinG=g;bestpinRole=role;bestpinQ=q;}
        if(np>0 && s2-g<minsg_pin) minsg_pin=s2-g;
        if(!np) continue;
        std::vector<int> ord; for(int t=-512;t<=511;t++) if(t) ord.push_back(t);
        std::sort(ord.begin(),ord.end(),[&](int a,int b){ i64 wa=C.JH[a+512]-C.JL[a+512],wb=C.JH[b+512]-C.JL[b+512];
            if(wa!=wb) return wa<wb; return std::abs(a)>std::abs(b); });
        BM M; M.init(s2,true);
        for(int t:ord){ if(uncon(t)) continue; ufor(t,U); M.andwith(U); if(M.pop()==0) break; }
        long long na=M.pop();
        if(na>0){ // physical width of I1 for the first surviving constant
          i64 r0=M.first(); long long mm=0; bool ok=true;
          for(int t=-512;t<=511;t++){ if(!t) continue; Cert c=certify(role,(i64)t<<g,r0,s2,LR_FLOOR,C.JL[t+512]-q,C.JH[t+512]-q);
            if(!c.hit){ok=false;break;} i64 a=c.vhit<0?-c.vhit:c.vhit; if(a>mm)mm=a; }
          if(ok){ if(s2-g<minsg_all) minsg_all=s2-g;
                  if(mm<16777216 && s2-g<minsg_all24) minsg_all24=s2-g;
                  if(g==13&&role==0){ char b2[128]; snprintf(b2,sizeof b2," s2=%d(q=%d,|K2|=%lld,I1<=%lld)",s2,q,na,mm); g13+=b2; } }
          if(ok && (bestall==0 || (bA_I1>=16777216 && mm<16777216) || (mm<16777216&&na>bestall))){
            bestall=na;bA_g=g;bA_s2=s2;bA_role=role;bA_q=q;bA_I1=mm;bA_r=r0; } }
      } }
    snprintf(buf,sizeof buf,"   best |K2| for the 3 pinned offsets: %lld  (role%d g=%d s2=%d q=%d)\n",
      bestpin,bestpinRole,bestpinG,bestpinS2,bestpinQ); s+=buf;
    if(bestall) snprintf(buf,sizeof buf,"   best |K2| for ALL 1024 offsets : %lld  (role%d g=%d s2=%d q=%d r=%lld) max|I1| needed %lld %s\n",
      bestall,bA_role,bA_g,bA_s2,bA_q,(long long)bA_r,bA_I1,bA_I1<16777216?"FITS a 24-bit operand":"(too wide for a 24-bit operand)");
    else snprintf(buf,sizeof buf,"   best |K2| for ALL 1024 offsets : 0  -- NO rounding constant of any form works\n");
    s+=buf;
    snprintf(buf,sizeof buf,"   min (s2-g): 3pinned %d | all-t %d | all-t with a 24-bit I1 %d\n",
       minsg_pin==99?-1:minsg_pin, minsg_all==99?-1:minsg_all, minsg_all24==99?-1:minsg_all24); s+=buf;
    s+="   published alignment g=13, role0, all-t solutions:"; s+= g13.empty()? std::string(" NONE"):g13; s+="\n";
    outl[ji]=s; }
  FILE*o=fopen("f2_cross_table.txt","w");
  fprintf(o,"# f2_cross: the t=342/-342/17 certificate crossed with every final-multiply variant of e_final.h\n");
  fprintf(o,"# fine latch scale (the scale at which a free J reaches 0 violations)\n");
  for(size_t i=0;i<outl.size();i++){ fputs(outl[i].c_str(),o); fputs(outl[i].c_str(),stdout);} fclose(o);
  return 0; }
