// rt41_c0f32.cpp -- were the FSCA constants generated in SINGLE PRECISION?  rt39/rt40: both ROMs carry irregular errors of
// about 2^-24 RELATIVE (+-3 units for values in [0.5,1), <= 1 for small values).  Pipelines tried: the angle in float or
// double; the sine rounded to float or not; the Chebyshev factor (1 - h^4/192) applied in float, in double, or not;
// final conversion RN / floor / ceil.  Score = entries 1..16 reproduced exactly.
#include <cstdio>
#include <cmath>
#include <cstdint>
static const int64_t C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ const double PI=3.14159265358979323846; const double h=512*2*PI/65536, econ=pow(h,4)/192; int best=0;
    const char* an[]={"double n*pi/32","(float)(n*pi/32)","n*(float)(pi/32)","(n*(float)pi)/32 in float","2*pi*(float)(n/64.)"};
    const char* sn[]={"sin in double","sin rounded to float","sinf(float angle)"}; const char* en[]={"no economisation","econ in double","econ: float multiply","econ: subtract (float)(s*econ) in float"};
    const char* fn[]={"RN","floor","ceil"};
    for(int A=0;A<5;A++)for(int S=0;S<3;S++)for(int E=0;E<4;E++)for(int Fm=0;Fm<3;Fm++){ int ok=0; char pat[20];
        for(int n=1;n<=16;n++){ double a; switch(A){case 0:a=n*PI/32;break;case 1:a=(float)(n*PI/32);break;case 2:a=(double)((float)n*(float)(PI/32));break;case 3:a=(double)(((float)n*(float)PI)/32.0f);break;default:a=2*PI*(double)(float)(n/64.0);}
            double s; if(S==0)s=sin(a); else if(S==1)s=(float)sin(a); else s=sinf((float)a);
            double v; if(E==0)v=s; else if(E==1)v=s*(1-econ); else if(E==2)v=(double)((float)s*(float)(1-econ)); else v=(double)((float)s-(float)(s*econ));
            double x=v*67108864.0; int64_t c= Fm==0? llround(x) : Fm==1? (int64_t)floor(x) : (int64_t)ceil(x); if(c>67108863)c=67108863; bool m=(c==C0TAB[n]); ok+=m; pat[n-1]=m?'#':'.'; } pat[16]=0;
        if(ok>best)best=ok; if(ok>=10) printf("%2d/16  %s   | %-28s | %-22s | %-40s | %s\n",ok,pat,an[A],sn[S],en[E],fn[Fm]); }
    printf("best: %d of 16\n",best); return 0; }
