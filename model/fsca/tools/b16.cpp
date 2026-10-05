// b16.cpp -- correct version of b15: for each structural variant of the NON-square parts, take for every node
// the UNION over its admissible C0 values of the allowed-J interval (an upper bound on feasibility), intersect
// over nodes, and report how many t become feasible.  1024 would mean the square latch alone can explain
// everything; anything less localises a second, independent error.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static inline i64 term(i64 field,i64 dx,int u,int conv,int flip){ if(!field) return 0;
    if(!flip) return sm(arr(field,dx)+K,u);
    switch(conv){ case 0: return -sm(arr(field,dx)+K,u); case 1: return sm(arr(field,-dx)+K,u);
                  default: return sm(~arr(field,dx)+K,u); } }
int main(){ if(!load_fsca(F))return 1; build_tables();
    i64 c0lo[17],c0hi[17]; for(int n=0;n<=16;n++){ i64 l,h;bool e; win_of(n*1024,l,h,e); c0lo[n]=l;c0hi[n]=h; }
    printf("c1conv c3conv Svar K3 | feasible t (union bound over ambiguous C0)\n");
    int best=0,bc[4]={0,0,0,0};
    for(int conv1=0;conv1<3;conv1++) for(int conv3=0;conv3<3;conv3++) for(int Svar=0;Svar<4;Svar++) for(int k3=0;k3<3;k3++){
        i64 SV[17]; int UV[17];
        for(int n=0;n<=16;n++){ if(!TB[n].S){SV[n]=0;UV[n]=30;continue;} int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
            i64 f23r=rn_scale(s,23+k), f24r=rn_scale(s,24+k);
            i64 f23t=(i64)((s<<(23+k))/QONE), f24t=(i64)((s<<(24+k))/QONE);
            switch(Svar){ case 0: SV[n]=std::min(f23r,(i64)((1ll<<23)-1)); UV[n]=30+k; break;
                          case 1: SV[n]=std::min(f23t,(i64)((1ll<<23)-1)); UV[n]=30+k; break;
                          case 2: SV[n]=std::min(f24r,(i64)((1ll<<24)-1)); UV[n]=31+k; break;
                          default:SV[n]=std::min(f24t,(i64)((1ll<<24)-1)); UV[n]=31+k; break; } }
        int feas=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            for(int node=1;node<=31&&gl<=gh;node++){ int flip=node>=17,n=flip?32-node:node; if(!SV[n])continue;
                i64 T1=term(TB[n].C1f,dx,23-TB[n].e1,conv1,flip); i64 T3=term(TB[n].c3<<5,A3,26,conv3,flip);
                i64 kk = k3==0?(1ll<<19) : k3==1?0 : (1ll<<(UV[n]-1));
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 ul=(i64)1<<62, uh=-((i64)1<<62);            // union over C0 candidates
                for(i64 c=c0lo[n];c<=c0hi[n];c++){ i64 base=c+T1-T3; i64 t2lo=base-hi,t2hi=base-lo;
                    i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,SV[n])+kk,UV[n])>=t2lo)b1=m;else a1=m+1;}
                    i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,SV[n])+kk,UV[n])<=t2hi)a2=m;else b2=m-1;}
                    if(a1<=a2){ ul=std::min(ul,a1); uh=std::max(uh,a2); } }
                if(ul>uh){ gl=1;gh=0; break; }
                gl=std::max(gl,ul); gh=std::min(gh,uh); }
            if(gl<=gh) feas++; }
        printf("  %d      %d     %d   %d  | %4d%s\n",conv1,conv3,Svar,k3,feas,feas>=1024?"   <== ALL FEASIBLE":"");
        if(feas>best){best=feas;bc[0]=conv1;bc[1]=conv3;bc[2]=Svar;bc[3]=k3;} }
    printf("best %d at c1conv=%d c3conv=%d Svar=%d K3=%d\n",best,bc[0],bc[1],bc[2],bc[3]);
    return 0; }
