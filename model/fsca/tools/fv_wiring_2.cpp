// fv_wiring_2.cpp -- audits of the claimed floor-0 variant.
//  (a) is the final-multiply value MONOTONE in J?  (the binary-search window is void otherwise)
//  (b) exhaustive-in-J windows at the pinned offsets, no monotonicity assumption
//  (c) the complement field width w: is 2^w - J a legal w-bit field for every J the latch must carry?
//  (d) overlap with jwin18 >> 2 ; (e) the full model end to end.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static int kof(int n){ if(n==0)return 0; i128 s=sin_(PI_()*n/32); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; return k; }
// sv: 0 published RN23 u=30+k ; 1 RN21 u=28+k
static void sfield(int sv,int n,i64&S,int&u){ int k=kof(n); i128 s=sin_(PI_()*n/32);
    if(sv==0){S=std::min(rn_scale(s,23+k),(i64)((1ll<<23)-1));u=30+k;}
    else      {S=std::min(rn_scale(s,21+k),(i64)((1ll<<21)-1));u=28+k;}
    if(n==0){S=0;u=30;} }
// form: kind 0 published ; 1 feedback.  wrap 0 = literal 2^w-J (may be NEGATIVE) ; 1 = (2^w-J) mod 2^w
struct Form{int kind,w,wrap;};
static inline i64 fv(const Form&f,i64 J,i64 S,int u){ const i64 K3=1ll<<19; if(!S)return 0;
    if(f.kind==0) return sm(arr(J,S)+K3,u);
    i64 M=1ll<<f.w; i64 fld=M-J; if(f.wrap) fld=((M-J)%M+M)%M;
    return (arr(S,M)-arr(S,fld)+K3)>>u; }

int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int part=argc>1?atoi(argv[1]):0;
    i64 S21[17],S23[17]; int U21[17],U23[17];
    for(int n=0;n<=16;n++){sfield(1,n,S21[n],U21[n]); sfield(0,n,S23[n],U23[n]);}

    if(part==0){
      puts("=== (a) MONOTONICITY of the final multiply in J, full range, per node ===");
      puts("    the published window search binary-searches J; if the value is not nondecreasing the");
      puts("    resulting interval is not the true solution set.");
      const i64 LIMc=12000000ll;
      for(int kind=0;kind<2;kind++){
        Form f{kind,23,0}; long bad=0; i64 worstJ=-1; int worstn=-1;
        #pragma omp parallel for schedule(dynamic) reduction(+:bad)
        for(int n=1;n<=16;n++){ i64*S=kind?S21:S23; int*U=kind?U21:U23; if(!S[n])continue;
          i64 prev=fv(f,0,S[n],U[n]);
          for(i64 J=1;J<=LIMc;J++){ i64 c=fv(f,J,S[n],U[n]); if(c<prev){ bad++;
              #pragma omp critical
              { if(worstJ<0){worstJ=J;worstn=n;} } } prev=c; } }
        printf("    %-12s : %ld descents over J=0..%lld x nodes 1..16%s",
               kind?"FEEDBACK":"PUBLISHED",bad,(long long)LIMc,bad?"":"  (monotone -> binary search valid)\n");
        if(bad)printf("   first at node %d J=%lld\n",worstn,(long long)worstJ);
      }
      puts("");
      puts("=== (b) EXHAUSTIVE-in-J windows at the pinned offsets (no monotonicity assumed) ===");
      const int PT[6]={17,-17,342,-342,258,-476};
      for(int kind=0;kind<2;kind++){ Form f{kind,23,0}; i64*S=kind?S21:S23; int*U=kind?U21:U23;
        printf("    %-10s coarse:",kind?"FEEDBACK":"PUBLISHED");
        for(int i=0;i<6;i++){ int t=PT[i]; i64 dx=(i64)t<<13;
          i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
          i64 lo=-1,hi=-1; long cnt=0;
          i64 Jmax = 12000000ll;
          for(i64 J=0;J<=Jmax;J++){ bool ok=true;
            for(int node=1;node<=31&&ok;node++){int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
              i64 s=flip?-1:1;
              i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
              i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
              i64 V=C0TAB[n]+s*T1-fv(f,J,S[n],U[n])-s*T3;
              int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh); if(V<wl||V>wh)ok=false; }
            if(ok){ if(lo<0)lo=J; hi=J; cnt++; } }
          printf("  t=%d:%s",t,cnt?"":"NONE");
          if(cnt)printf("[%lld,%lld]n=%ld",(long long)lo,(long long)hi,cnt); }
        printf("\n"); }
      puts("");
    }
    if(part==1){
      puts("=== (c) IS THE COMPLEMENT FIELD A LEGAL w-BIT FIELD? ===");
      // max J the coarse latch must carry
      FILE*fp=fopen("jwin18.txt","r"); char ln[256]; i64 maxJ=0;
      while(fgets(ln,sizeof ln,fp)){ if(ln[0]=='#')continue; long long t,a,b,w2;
        if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w2)==4){ if(a<=b&&(a>>2)>maxJ)maxJ=a>>2; } }
      fclose(fp);
      printf("    largest coarse J required by the published dataset (jwin18>>2): %lld ; 2^23 = %lld\n",
             (long long)maxJ,(long long)(1ll<<23));
      printf("    -> at w=23 the field 2^23-J is NEGATIVE for every J > 2^23, i.e. it is not a 23-bit\n");
      printf("       field at all.  Count of t whose whole coarse window lies above 2^23: ");
      { FILE*g=fopen("jwin18.txt","r"); int c=0; while(fgets(ln,sizeof ln,g)){ if(ln[0]=='#')continue;
          long long t,a,b,w2; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w2)==4)
            if(a<=b&&(a>>2)>(1ll<<23))c++; } fclose(g); printf("%d of 1024\n\n",c); }
      puts("=== (c2) the claim's free-J floor as the field width w varies, and under a REAL w-bit wrap ===");
      for(int wrap=0;wrap<2;wrap++){
        printf("    %s\n", wrap? "field = (2^w - J) mod 2^w  [a genuine w-bit complement field]"
                               : "field = 2^w - J as a signed integer  [what the claim computes]");
        for(int w=22;w<=32;w++){ Form f{1,w,wrap};
          int feas=0; i64 j17l=1,j17h=0,j342l=1,j342h=0,jm342l=1,jm342h=0;
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000ll;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S21[n])continue;
              i64 s=flip?-1:1;
              i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
              i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
              i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
              i64 t2lo=base-wh,t2hi=base-wl;
              i64 a1=0,b1=12000001ll; while(a1<b1){i64 m=(a1+b1)/2; if(fv(f,m,S21[n],U21[n])>=t2lo)b1=m;else a1=m+1;}
              i64 a2=-1,b2=12000000ll; while(a2<b2){i64 m=(a2+b2+1)/2; if(fv(f,m,S21[n],U21[n])<=t2hi)a2=m;else b2=m-1;}
              if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            if(gl<=gh)feas++;
            if(t==17){j17l=gl;j17h=gh;} if(t==342){j342l=gl;j342h=gh;} if(t==-342){jm342l=gl;jm342h=gh;} }
          printf("      w=%2d: feasible %4d/1024   t=17 [%lld,%lld]%s  t=342 [%lld,%lld]  t=-342 [%lld,%lld]\n",
            w,feas,(long long)j17l,(long long)j17h,j17l>j17h?" INFEAS":"",
            (long long)j342l,(long long)j342h,(long long)jm342l,(long long)jm342h); }
        puts(""); }
    }
    if(part==2){
      puts("=== (d) coarse windows vs jwin18>>2, and (e) the full model ===");
      Form f{1,23,0};
      i64 JLc[1024],JHc[1024];
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000ll;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S21[n])continue;
          i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
          i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
          i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
          i64 t2lo=base-wh,t2hi=base-wl;
          i64 a1=0,b1=12000001ll; while(a1<b1){i64 m=(a1+b1)/2; if(fv(f,m,S21[n],U21[n])>=t2lo)b1=m;else a1=m+1;}
          i64 a2=-1,b2=12000000ll; while(a2<b2){i64 m=(a2+b2+1)/2; if(fv(f,m,S21[n],U21[n])<=t2hi)a2=m;else b2=m-1;}
          if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLc[t+512]=gl; JHc[t+512]=gh; }
      FILE*g=fopen("jwin18.txt","r"); char ln[256]; int ov=0,tot=0; std::vector<int> dis;
      while(fgets(ln,sizeof ln,g)){ if(ln[0]=='#')continue; long long t,a,b,w2;
        if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w2)!=4)continue; tot++;
        i64 fl=a>>2, fh=b>>2, cl=JLc[t+512],ch=JHc[t+512];
        if(cl<=ch && std::max(fl,cl)<=std::min(fh,ch)) ov++; else dis.push_back((int)t); }
      fclose(g);
      printf("    coarse window overlaps (jwin18 window >> 2) at %d of %d offsets\n",ov,tot);
      printf("    disagreements:"); for(int t:dis)printf(" %d",t); printf("\n\n");
      // full model, published chain, several shift/constant choices
      puts("    full model (nodes 0..31), published chain, claimed final multiply:");
      for(int s1=23;s1<=24;s1++) for(int ro=0;ro<2;ro++) for(int k2=0;k2<2;k2++){
        i64 K2=k2?K:0; int viol=0,guard=0;
        for(int node=0;node<=31;node++){int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
          int isg=(n==0||n==1||n==2||n==16);
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K)>>s1; i64 J=(arr(I1,dx)+K2)>>20;
            i64 V=C0TAB[n]+s*T1-fv(f,J,S21[n],U21[n])-s*T3;
            int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
            if(V<wl||V>wh){viol++; if(isg)guard++;} } }
        printf("      s1=%d %s K2=%s : %d violations, guard %d\n",s1,ro?"arr(dx,KC)":"arr(KC,dx)",k2?"2^19":"0",viol,guard); }
    }
    return 0; }
