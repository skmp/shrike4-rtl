// exhaustive exact fit of one 512-entry FSCA segment: W = C0 + s*mag(C1*a + B1, 13) + mag(C2*a^2 + B2, 22) + s*mag(C3*a^3 + B3, 31)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <algorithm>
typedef int64_t i64;
static inline i64 mg(i64 P,int sh,i64 B){ return P>=0 ? ((P+B)>>sh) : -(((-P)+B)>>sh); }
int main(int argc,char**argv){
  int seg=atoi(argv[1]); i64 C0=atoll(argv[2]), C1c=atoll(argv[3])*8, C2c=atoll(argv[4])*8, C3c=atoll(argv[5])*8;
  int mirror=argc>6?atoi(argv[6]):0;
  FILE*f=fopen("V26.bin","rb"); std::vector<i64> V(32768); if(fread(V.data(),8,32768,f)!=32768) return 1; fclose(f);
  i64 y[512]; i64 a[512]; int s= mirror?-1:1;
  for(int b=0;b<512;b++){ y[b]=V[seg*512+b]; a[b]= mirror? 512-b : b; }
  int betas[]={0,1,-1,2,-2,3,-3,4,-4,5,-5,6,-6,8,-8};
  int nb=sizeof(betas)/sizeof(int);
  int best=1<<30; i64 bC[4]={0}; int bB[3]={0};
  // precompute T2,T3 tables
  std::vector<std::vector<i64>> T2((49)*nb, std::vector<i64>(512)), T3((25)*nb, std::vector<i64>(512));
  for(int d2=-24;d2<=24;d2++) for(int j=0;j<nb;j++) for(int b=0;b<512;b++) T2[(d2+24)*nb+j][b]=mg((C2c+d2)*a[b]*a[b],22,(i64)betas[j]<<15);
  for(int d3=-12;d3<=12;d3++) for(int j=0;j<nb;j++) for(int b=0;b<512;b++) T3[(d3+12)*nb+j][b]=s*mg((C3c+d3)*a[b]*a[b]*a[b],31,(i64)betas[j]<<24);
  for(int d1=-6;d1<=6;d1++) for(int j1=0;j1<nb;j1++){
    i64 T1[512]; for(int b=0;b<512;b++) T1[b]=s*mg((C1c+d1)*a[b],13,(i64)betas[j1]<<6);
    for(int i2=0;i2<49*nb;i2++){ const i64* t2=T2[i2].data();
      for(int i3=0;i3<25*nb;i3++){ const i64* t3=T3[i3].data();
        // C0 free: count mismatches vs mode of diff; approximate: use diff at b=0..3 to pick C0 candidate = y[0]-(T1+T2+T3)[0], then count
        i64 c0=y[0]-(T1[0]+t2[0]+t3[0]); int bad=0;
        for(int b=1;b<512;b++){ if(y[b]-(T1[b]+t2[b]+t3[b])!=c0){ if(++bad>=best) break; } }
        if(bad<best){ best=bad; bC[0]=c0; bC[1]=C1c+d1; bC[2]=C2c+(i2/nb-24); bC[3]=C3c+(i3/nb-12); bB[0]=betas[j1]; bB[1]=betas[i2%nb]; bB[2]=betas[i3%nb]; }
      } }
  }
  printf("seg %d mirror %d: mism %d  C0=%lld C1=%.4f/step C2=%.4f C3=%.4f  biases(1/128)=(%d,%d,%d)\n",seg,mirror,best,(long long)bC[0],bC[1]/16.0/1024,bC[2]/16.0,bC[3]/16.0,bB[0],bB[1],bB[2]);
}
