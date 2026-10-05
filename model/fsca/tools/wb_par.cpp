// wb_par.cpp -- PARITY DECOMPOSITION.  For table entry n, V(n,t) = C0+T1-T2-T3 and V(32-n,t) = C0-T1-T2+T3.
//   difference  V_n - V_{32-n} = 2*(T1-T3)   -> tests the ODD path alone, independent of the square path
//   sum         V_n + V_{32-n} = 2*(C0-T2)   -> tests the EVEN path alone, independent of C1/c3
// Report window violations of the odd model, and rebuild the J windows from the sums only.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    // ---- (1) odd path check
    int totviol=0, tot=0, exactpairs=0;
    puts("tab  odd-window violations / 1024   (max |miss|)  exact-pair count");
    for(int n=1;n<=15;n++){ int v=0,ex=0; i64 worst=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            const Tab&T=TB[n];
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            int a1=(n*1024+t)&0xffff, a2=((32-n)*1024+t)&0xffff;
            i64 l1,h1,l2,h2; bool e1,e2; win_of(a1,l1,h1,e1); win_of(a2,l2,h2,e2);
            i64 dlo=l1-h2, dhi=h1-l2; i64 mo=2*(T1-T3);
            if(e1&&e2)ex++;
            if(mo<dlo){v++; if(dlo-mo>worst)worst=dlo-mo;} else if(mo>dhi){v++; if(mo-dhi>worst)worst=mo-dhi;} }
        totviol+=v; tot+=1024; exactpairs+=ex;
        printf("%3d   %4d   (%lld)   %d\n",n,v,(long long)worst,ex); }
    printf("TOTAL odd-path window violations over tables 1..15: %d of %d   (exact pairs %d)\n",totviol,tot,exactpairs);
    // ---- (2) even path from sums only: J windows
    std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024,1);
    int infeas=0;
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int n=1;n<=16;n++){ const Tab&T=TB[n]; if(!T.S)continue;
            int a1=(n*1024+t)&0xffff, a2=((32-n)*1024+t)&0xffff;
            i64 l1,h1,l2,h2; bool e1,e2; win_of(a1,l1,h1,e1);
            if(n==16){ l2=l1;h2=h1; } else win_of(a2,l2,h2,e2);
            i64 slo=l1+l2, shi=h1+h2;                     // 2*(C0-T2)
            i64 t2lo=(2*C0FIT[n]-shi), t2hi=(2*C0FIT[n]-slo);
            // T2 in [ceil(t2lo/2), floor(t2hi/2)]
            i64 lo2=(t2lo+1)>>1, hi2=t2hi>>1; if(t2lo<0) lo2=-((-t2lo)>>1);
            int u=U2FIT[n];
            i64 a=0,b=12000001; while(a<b){i64 m=(a+b)/2; if(sm(arr(m,T.S)+K,u)>=lo2)b=m;else a=m+1;}
            i64 c=-1,d=12000000; while(c<d){i64 m=(c+d+1)/2; if(sm(arr(m,T.S)+K,u)<=hi2)c=m;else d=m-1;}
            if(a>gl)gl=a; if(c<gh)gh=c; }
        JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; if(gl>gh)infeas++; }
    int disj=0; for(int A=1;A<=511;A++){int ip=A+512,im=-A+512; if(!ok[ip]||!ok[im])continue;
        if(JH[ip]<JL[im]||JH[im]<JL[ip])disj++; }
    int sc=0,scs=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20; if(J>=JL[t+512]&&J<=JH[t+512])sc++;
        i64 Js=arr(((i64)t*t)<<1,2586787)>>17; if(Js>=JL[t+512]&&Js<=JH[t+512])scs++; }
    printf("\nJ windows from SUMS ONLY: infeasible %d, disjoint pairs %d (symceil %d), chain %d, sym-rule %d\n",
        infeas,disj,1024-2*infeas-2*disj,sc,scs);
    FILE*g=fopen("wb_jwin_sum.txt","w");
    for(int t=-512;t<=511;t++) fprintf(g,"%d %lld %lld %d\n",t,(long long)JL[t+512],(long long)JH[t+512],(int)ok[t+512]);
    fclose(g);
    return 0; }
