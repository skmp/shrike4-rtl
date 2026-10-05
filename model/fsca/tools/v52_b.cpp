// v52_b.cpp -- B(i) extra LSB, B(iii) node rounding rho, B(iv) alignment w with KC free.
#include "v52_common.h"
static Fsca F; static JW W;
int main(){ if(!load_fsca(F)||!load_jwin(W)){puts("load fail");return 1;} build_tables();
    i64 J[1024];
    puts("== B(i) dx = (2t+e)<<12 : square path on jwin18 (baseline 918 at e=0) ==");
    for(int e=-4;e<=4;e++){ for(int t=-512;t<=511;t++){ i64 dx=((i64)(2*t+e))<<12; J[t+512]=chainJ_dx(dx,dx);} 
        printf("  e=%+d : %d/1024   J(17)=%lld J(342)=%lld\n",e,jscore(W,J),(long long)J[17+512],(long long)J[342+512]); }
    puts("== B(i) full model (baseline 294) ==");
    for(int e=-2;e<=2;e++) for(int cl=0;cl<2;cl++){ Cfg c; c.order=1; c.lsb_e=e; c.cube_lsb=cl;
        FullRes r=full_score(c,F); printf("  e=%+d cube_lsb=%d : %d violations\n",e,cl,r.viol); }
    puts("== B(iii) node = (angle+rho)>>10 : full model ==");
    { int rl[]={0,256,384,448,480,500,511,512,513,524,544,576,640,768}; 
      for(int i=0;i<(int)(sizeof rl/sizeof*rl);i++){ Cfg c; c.order=1; c.rho=rl[i]; FullRes r=full_score(c,F);
        printf("  rho=%4d : %d violations%s\n",rl[i],r.viol,rl[i]==512?"   <- published":""); } }
    puts("== B(iii) sharp: the 64 boundary angles, each assigned to the other node ==");
    { // angles a = 1024n+512 : rho=512 puts them at t=-512 of node n+1 ; rho=513 puts them at t=+512 of node n
      // score each assignment directly
      for(int which=0;which<2;which++){
        int vio_up=0,vio_dn=0;
        for(int n=0;n<64;n++){
          int a = (1024*n + (which?511:512)) & 0xffff;
          Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
          auto ev=[&](int node,int t)->i64{ int nd=node&63,hs=1; if(nd>=32){nd-=32;hs=-1;} int flip=nd>=17,nn=flip?32-nd:nd;
            Cfg c; c.order=1; return model_Vc(c,nn,t,flip)*hs; };
          // upper node assignment: node n+1, t = a-1024(n+1)
          i64 Vu = ev(n+1, (which?511:512)-1024);
          i64 Vd = ev(n,   (which?511:512));
          if(Vu<lo||Vu>hi)vio_up++; if(Vd<lo||Vd>hi)vio_dn++; }
        printf("  a = 1024n%+d : upper-node (t=%d) %d/64 violations ; lower-node (t=%d) %d/64 violations\n",
          which?511:512, (which?511:512)-1024, vio_up, which?511:512, vio_dn); } }
    puts("== B(iv) dx = t<<w, KC free over nominal +-4096 : square path on jwin18 ==");
    for(int w=10;w<=16;w++){
        int s1 = 23 + 2*(w-13) >= 0 ? 23 : 23; // keep s1+s2 = 41+2(w-13): put the whole compensation on s2
        int s2 = 18 + 2*(w-13);
        if(s2<0){printf("  w=%d : s2 negative, skipped\n",w);continue;}
        i64 nom = KC; int best=-1; i64 bkc=0; i64 bj17=0;
        for(i64 kc=nom-4096;kc<=nom+4096;kc++){
            int sc=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K)>>s1; i64 v=(arr(I1,dx)+K)>>s2;
                if(v>=W.lo[t+512]&&v<=W.hi[t+512])sc++; }
            if(sc>best){best=sc;bkc=kc;} }
        { i64 dx=(i64)17<<w; i64 I1=(arr(bkc,dx)+K)>>s1; bj17=(arr(I1,dx)+K)>>s2; }
        printf("  w=%2d s1=%d s2=%2d : best %d/1024 at KC=%lld (0x%llX)%s   J(17)=%lld\n",
            w,s1,s2,best,(long long)bkc,(long long)bkc,bkc==KC?" == published KC":"",(long long)bj17); }
    return 0; }
