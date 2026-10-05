// wv_feedback_sweep.cpp -- how much of the claimed 940 is the two free constants (A, K1)?
// Exact: for each A, the maximum over ALL integer K1 is found by interval sweep (no sampling).
// J = sm(arr(L,dx), 20), L = floor((arr(A,dx)+K1)/2^s1).
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
static i64 JL[1024],JH[1024]; static int OKT[1024];
static void load_win(){ FILE*f=fopen("jwin.txt","r"); int t,ok; long long l,h;
    while(fscanf(f,"%d %lld %lld %d",&t,&l,&h,&ok)==4){ JL[t+512]=l;JH[t+512]=h;OKT[t+512]=ok; } fclose(f); }
// exact max over K1 for a given A and s1, plus one witness K1
static int best_over_K1(i64 A,int s1,int swap,i64& wK1,int Lscan=400){
    std::vector<std::pair<i64,int>> ev; ev.reserve(200000);
    i64 M = 1ll<<s1;
    for(int ti=0;ti<1024;ti++){ if(!OKT[ti])continue; int t=ti-512; i64 dx=(i64)t<<13;
        i64 P = swap? arr(dx,A) : arr(A,dx);
        i64 Lnom = P>>s1;                       // nominal (K1=0)
        // collect every L in [Lnom-Lscan, Lnom+Lscan] whose J lands in the window
        for(i64 L=Lnom-Lscan; L<=Lnom+Lscan; L++){ i64 J=sm(arr(L,dx),20);
            if(J>=JL[ti]&&J<=JH[ti]){ i64 lo = L*M - P, hi = (L+1)*M - 1 - P;
                ev.push_back({lo,+1}); ev.push_back({hi+1,-1}); } } }
    std::sort(ev.begin(),ev.end());
    int cur=0,best=0; i64 bk=0;
    for(size_t i=0;i<ev.size();){ i64 x=ev[i].first; while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;}
        if(cur>best){best=cur;bk=x;} }
    wK1=bk; return best; }
int main(int argc,char**argv){ build_tables(); load_win();
    int feas=0; for(int i=0;i<1024;i++)feas+=OKT[i];
    printf("KC = %lld ; 4*KC = %lld ; 8*KC = %lld ; 16*KC = %lld\n",(long long)KC,(long long)(4*KC),(long long)(8*KC),(long long)(16*KC));
    printf("claim A - 4KC = %lld ; B - 8KC = %lld ; C - 16KC = %lld\n",
        (long long)(20694294-4*KC),(long long)(41388588-8*KC),(long long)(82777175-16*KC));
    printf("feasible t = %d\n\n",feas);
    // 1. what do the DERIVED constants give?
    { i64 k; int b;
      b=best_over_K1(4*KC,25,0,k);  printf("A = 4*KC  = %8lld (derived)      best over all K1: %4d   witness K1 = %lld\n",(long long)(4*KC),b,(long long)k);
      b=best_over_K1(8*KC,26,0,k);  printf("A = 8*KC  = %8lld (derived)      best over all K1: %4d   witness K1 = %lld\n",(long long)(8*KC),b,(long long)k);
      b=best_over_K1(16*KC,27,0,k); printf("A = 16*KC = %8lld (derived)      best over all K1: %4d   witness K1 = %lld\n",(long long)(16*KC),b,(long long)k);
      b=best_over_K1(20694294,25,0,k); printf("A = 20694294 (claim A)          best over all K1: %4d   witness K1 = %lld\n",b,(long long)k);
      b=best_over_K1(41388588,26,0,k); printf("A = 41388588 (claim B)          best over all K1: %4d   witness K1 = %lld\n",b,(long long)k);
      b=best_over_K1(82777175,27,1,k); printf("A = 82777175 (claim C, swapped) best over all K1: %4d   witness K1 = %lld\n",b,(long long)k); }
    // 2. the landscape: A near 4*KC, exact max over K1 for each
    puts("\nA - 4KC : best-over-K1 (s1=25)   [how special is the claimed offset +2?]");
    int hist[64]; memset(hist,0,sizeof hist); int over=0,best=0; i64 bA=0;
    for(i64 d=-32;d<=32;d++){ i64 k; int b=best_over_K1(4*KC+d,25,0,k);
        printf("%+4lld:%4d%s",(long long)d,b,(d%8==0)?"\n":"  ");
        if(b>=940)over++; if(b>best){best=b;bA=4*KC+d;} }
    printf("\nmax over that 65-wide A window: %d at A = %lld ; number of A with best>=940: %d of 65\n",best,(long long)bA,over);
    // 3. how many K1 values reach >= 940 at the claimed A (i.e. is K1 a knife edge or a plateau)?
    { i64 A=20694294; std::vector<std::pair<i64,int>> ev;
      i64 M=1ll<<25;
      for(int ti=0;ti<1024;ti++){ if(!OKT[ti])continue; int t=ti-512; i64 dx=(i64)t<<13; i64 P=arr(A,dx); i64 Ln=P>>25;
        for(i64 L=Ln-400;L<=Ln+400;L++){ i64 J=sm(arr(L,dx),20); if(J>=JL[ti]&&J<=JH[ti]){ ev.push_back({L*M-P,1}); ev.push_back({(L+1)*M-P,-1}); } } }
      std::sort(ev.begin(),ev.end()); int cur=0; i64 span940=0,span930=0,prev=0;
      for(size_t i=0;i<ev.size();){ i64 x=ev[i].first; if(cur>=940) span940+=x-prev; if(cur>=930) span930+=x-prev;
          while(i<ev.size()&&ev[i].first==x){cur+=ev[i].second;i++;} prev=x; }
      printf("at A=20694294: measure of K1 giving >=940 : %lld integers ; >=930 : %lld integers (of 2^25 = %lld)\n",
          (long long)span940,(long long)span930,(long long)M); }
    // 4. natural rounding constants at the claimed A
    puts("\nnatural K1 at A = 20694294, s1 = 25:");
    for(i64 k : {0ll, 1ll<<19, 1ll<<24, -(1ll<<19), -(1ll<<24), 1ll<<17, -(1ll<<17), 1ll<<23, -(1ll<<23), -3276800ll}){
        int sc=0; for(int ti=0;ti<1024;ti++){ if(!OKT[ti])continue; int t=ti-512; i64 dx=(i64)t<<13;
            i64 L=(arr(20694294,dx)+k)>>25; i64 J=sm(arr(L,dx),20); if(J>=JL[ti]&&J<=JH[ti])sc++; }
        printf("   K1 = %12lld -> %4d\n",(long long)k,sc); }
    puts("\nnatural K1 at A = 4*KC (derived), s1 = 25:");
    for(i64 k : {0ll, 1ll<<19, 1ll<<24, -(1ll<<19), -(1ll<<24)}){
        int sc=0; for(int ti=0;ti<1024;ti++){ if(!OKT[ti])continue; int t=ti-512; i64 dx=(i64)t<<13;
            i64 L=(arr(4*KC,dx)+k)>>25; i64 J=sm(arr(L,dx),20); if(J>=JL[ti]&&J<=JH[ti])sc++; }
        printf("   K1 = %12lld -> %4d\n",(long long)k,sc); }
    return 0; }
