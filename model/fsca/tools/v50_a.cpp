// v50_a.cpp -- HYPOTHESIS A: t computed in 2s complement but READ as 1s complement (or converse),
// plus the uniform "missing carry-in" reading and sign-magnitude reads.  Independent of w5_*.
#include "v50_core.h"
// reading modes for the t field
//  mode 0: t_eff = t + (t<0 ? d : 0)      d=+1 : 2s computed / 1s read ; d=-1 : converse
//  mode 1: t_eff = t + e                  uniform (missing carry-in etc.)
//  mode 2: t_eff = (t>=0) ? t : -( t + (1<<(n-1)) )   sign-magnitude read of the 2s pattern
//  mode 3: t_eff = (t>=0) ? t : ~t   (= |t|-1, the "1s magnitude" NOTES.md claims was tested)
//  mode 4: t_eff = |t|  (2s magnitude, what m5_mag.cpp actually computed)
static inline int teff(int t,int mode,int d,int n){
    switch(mode){
        case 0: return t + (t<0 ? d : 0);
        case 1: return t + d;
        case 2: return t>=0 ? t : -(t + (1<<(n-1)));
        case 3: return t>=0 ? t : ~t;
        case 4: return t<0 ? -t : t;
    } return t; }
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables(); if(!v50_load_jwin())return 1;
    VCfg c; c.s2=18;   // FINE latch for jwin18
    VCfg c20; c20.s2=20;
    // ---------- part 1: jwin18 over placements (dxa / dxb independently) ----------
    puts("== A.1 square path, jwin18 (baseline 918; pa/pb = t_eff used at 1st/2nd square multiply) ==");
    printf("%-34s %s\n","config","score  (t>=0 / t<0)   J(17)      J(342)     J(-342)");
    int best=0; char bestcfg[128]={0};
    for(int mode=0;mode<=4;mode++){
      int dlo = (mode==0||mode==1)? -4 : 0, dhi = (mode==0||mode==1)? 4 : 0;
      int nlo = (mode==2)? 8:10, nhi=(mode==2)?16:10;
      for(int d=dlo; d<=dhi; d++) for(int n=nlo;n<=nhi;n++)
        for(int pa=0;pa<2;pa++) for(int pb=0;pb<2;pb++){
          if(!pa&&!pb) { if(!(mode==0&&d==0)&&!(mode==1&&d==0)) continue; }
          auto jf=[&](int t){ i64 da = dx_id(pa?teff(t,mode,d,n):t), db = dx_id(pb?teff(t,mode,d,n):t);
                              return v50_J(da,db,c); };
          int p,q; int sc=v50_score(jf,&p,&q);
          char nm[128]; snprintf(nm,sizeof nm,"mode%d d=%+d n=%d pa=%d pb=%d",mode,d,n,pa,pb);
          bool interesting = (sc>=900) || (mode==0&&d!=0&&pa&&pb) || (mode==1&&d==-1) || (mode>=3&&pa&&pb) || (mode==2&&pa&&pb&&n<=12);
          if(interesting) printf("%-34s %4d/1024 (%3d/%3d)  %8lld %10lld %10lld\n",nm,sc,p,q,
              (long long)jf(17),(long long)jf(342),(long long)jf(-342));
          if(sc>best){best=sc; strncpy(bestcfg,nm,127);} }
    }
    printf("-> best over the whole A scan: %d/1024 at [%s]\n\n",best,bestcfg);
    // ---------- part 2: RIGIDITY, measured not argued ----------
    puts("== A.2 rigidity: is chainJ(t) bit-identical to baseline for every t>=0, over every sign-gated config? ==");
    { long long cfgs=0, bad=0;
      auto base=[&](int t){ return v50_J(dx_id(t),dx_id(t),c); };
      for(int mode=0;mode<1;mode++) for(int d=-8;d<=8;d++) for(int pa=0;pa<2;pa++) for(int pb=0;pb<2;pb++){
          cfgs++;
          for(int t=0;t<=511;t++){ i64 da=dx_id(pa?teff(t,0,d,10):t), db=dx_id(pb?teff(t,0,d,10):t);
              if(v50_J(da,db,c)!=base(t)) bad++; } }
      // sign-magnitude and 1s-magnitude modes too
      for(int mode=2;mode<=4;mode++) for(int n=8;n<=16;n++) for(int pa=0;pa<2;pa++) for(int pb=0;pb<2;pb++){
          cfgs++;
          for(int t=0;t<=511;t++){ i64 da=dx_id(pa?teff(t,mode,0,n):t), db=dx_id(pb?teff(t,mode,0,n):t);
              if(v50_J(da,db,c)!=base(t)) bad++; } }
      printf("   %lld sign-gated configs x 512 non-negative t : %lld deviations from baseline\n",cfgs,bad);
      printf("   => J(17)=%lld and J(342)=%lld are INVARIANT; pinned values 45617 / 18466836 unreachable.\n\n",
             (long long)base(17),(long long)base(342)); }
    // ---------- part 3: full model over 65536 angles, t_eff applied to the WHOLE datapath ----------
    puts("== A.3 whole datapath (t_eff replaces t in linear, cubic AND square), full model 65536 angles ==");
    printf("   baseline arr(KC,dx): %d\n", v50_full64([&](int t){return v50_J(dx_id(t),dx_id(t),c20);},20,dx_id,dx_id,nullptr));
    for(int mode=0;mode<=4;mode++){
      int dlo=(mode<=1)?-2:0,dhi=(mode<=1)?2:0, nlo=(mode==2)?10:10,nhi=(mode==2)?16:10;
      for(int d=dlo;d<=dhi;d++) for(int n=nlo;n<=nhi;n++){
        if(mode<=1&&d==0) continue;
        auto te=[&](int t){ return teff(t,mode,d,n); };
        auto jf=[&](int t){ i64 dd=dx_id(te(t)); return v50_J(dd,dd,c20); };
        auto dl=[&](int t){ return dx_id(te(t)); };
        int pv; int v=0;
        { int du=0; int viol=0,p=0;
          for(int node=0;node<=63;node++){ int hb=node>=32,nd=node&31,flip=nd>=17,nn=flip?32-nd:nd;
            i64 s=flip?-1:1,g=hb?-1:1; const Tab&T=TB[nn];
            for(int t=-512;t<=511;t++){ int te2=te(t); i64 dxe=dx_id(te2);
              i64 T1=T.C1f? sm(arr(T.C1f,dxe)+K,23-T.e1):0;
              i64 A3=((arr(((i64)te2*te2)<<3,dxe)+K)>>20)>>3;
              i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
              i64 T2=T.S? sm(arr(jf(t),T.S)+K,V_U2TAB[nn]+du):0;
              i64 V=g*(V_C0TAB[nn]+s*T1-T2-s*T3);
              int a=(node*1024+t)&0xffff; i64 lo,hi; v50_win(a,lo,hi);
              if(V<lo||V>hi){viol++; if(t>=0)p++;} } }
          v=viol; pv=p; (void)dl; }
        printf("   mode%d d=%+d n=%2d : %6d violations (pos-t %d)\n",mode,d,n,v,pv);
        if(mode!=2) break;
      } }
    return 0; }
