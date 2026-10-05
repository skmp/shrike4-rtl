// wb_e2.cpp -- cross-tabulate the dx^2 array product's own sign asymmetry e2(t) = arr(-dx,-dx)-arr(dx,dx)
// against the REQUIRED asymmetry D(|t|) = J(-t)-J(t) from the window data.
#include "base2.h"
#include <cstring>
#include <vector>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    int n_pp=0,n_pz=0,n_pn=0, n_zp=0,n_zz=0, n_np=0,n_nz=0,n_nn=0;
    std::vector<int> forced_e2p, forced_e2z, forced_e2n;
    for(int A=1;A<=511;A++){ int ip=A+512,im=-A+512; if(!OKT[ip]||!OKT[im])continue;
        i64 dx=(i64)A<<13; i64 e2=arr(-dx,-dx)-arr(dx,dx);
        i64 dlo=JL[im]-JH[ip], dhi=JH[im]-JL[ip];
        int forced = dlo>0 ? 1 : (dhi<0 ? -1 : 0);
        if(e2>0){ if(forced>0){n_pp++;forced_e2p.push_back(A);} else if(forced==0)n_pz++; else n_pn++; }
        else if(e2==0){ if(forced>0){n_zp++;forced_e2z.push_back(A);} else if(forced==0)n_zz++; else n_nz++; }
        else { if(forced>0){n_np++;forced_e2n.push_back(A);} else if(forced==0)n_nz++; else n_nn++; } }
    printf("e2>0 : forced D>0 %d, D=0 allowed %d, forced D<0 %d\n",n_pp,n_pz,n_pn);
    printf("e2=0 : forced D>0 %d, D=0 allowed %d\n",n_zp,n_zz);
    printf("e2<0 : forced D>0 %d, (other) %d\n",n_np,n_nz);
    printf("forced |t| with e2>0 (%zu): ",forced_e2p.size()); for(int v:forced_e2p)printf("%d ",v); printf("\n");
    printf("forced |t| with e2=0 (%zu): ",forced_e2z.size()); for(int v:forced_e2z)printf("%d ",v); printf("\n");
    printf("forced |t| with e2<0 (%zu): ",forced_e2n.size()); for(int v:forced_e2n)printf("%d ",v); printf("\n");
    // list |t| with e2 != 0
    int cp=0,cn=0; printf("\n|t| with e2>0: "); for(int A=1;A<=511;A++){ i64 dx=(i64)A<<13;
        if(arr(-dx,-dx)-arr(dx,dx)>0){printf("%d ",A); cp++;} } printf(" (%d)\n",cp);
    printf("|t| with e2<0: "); for(int A=1;A<=511;A++){ i64 dx=(i64)A<<13;
        if(arr(-dx,-dx)-arr(dx,dx)<0){printf("%d ",A); cn++;} } printf(" (%d)\n",cn);
    return 0; }
