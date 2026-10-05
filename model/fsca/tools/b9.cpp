// b9.cpp -- exact arithmetic audit of the two chain multiplies at the tightly-constrained t values,
// plus: which t make arr(dx,dx) exact?
#include "base2.h"
#include <cstring>
int main(){ build_tables();
    // which t give an exact arr(dx,dx)?
    printf("t with arr(dx,dx) EXACT (= t^2*2^26):");
    int ne=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; if(arr(dx,dx)==((i64)t*t<<26)){ if(ne<40)printf(" %d",t); ne++; } }
    printf("  ... total %d\n",ne);
    printf("distinct errors of arr(dx,dx):");
    { i64 seen[8]; int ns=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 e=arr(dx,dx)-((i64)t*t<<26); int f=0; for(int i=0;i<ns;i++) if(seen[i]==e)f=1; if(!f&&ns<8) seen[ns++]=e; }
      for(int i=0;i<ns;i++) printf(" %lld",(long long)seen[i]); printf("\n"); }
    // audit
    int ts[]={-376,376,-40,40,258,-258,342,-342,417,-417,144,-144,301,-301,348,-348,420,-420,408,-408,324,-324,303,-303,17,-17};
    puts("\n    t |  arr(KC,dx) error  I1     I1exact*2^10   | arr(I1,dx) error   J(model)  ideal J");
    for(int t : ts){ i64 dx=(i64)t<<13;
        i64 p1=arr(KC,dx), p1e=(i64)KC*dx; i64 I1=(p1+K)>>23;
        i64 p2=arr(I1,dx), p2e=I1*dx; i64 J=(p2+K)>>20;
        printf("%5d | %10lld  %9lld  %14.4Lf | %10lld  %9lld  %14.4Lf\n",t,(long long)(p1-p1e),(long long)I1,
            (long double)KC*t/1024.0L,(long long)(p2-p2e),(long long)J,(long double)KC*t*t/131072.0L); }
    return 0; }
