// Diagnostic only: can a conventional finite-precision CORDIC generate C0?
#include "base2.h"
#include <algorithm>
#include <vector>
static const i64 target[]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i128 root(i128 n){i128 l=0,h=i128(1)<<62;while(l<h){i128 m=(l+h+1)/2;if(m*m<=n)l=m;else h=m-1;}return l;}
static i64 shr(i64 x,int s,int mode){return mode&&x<0?-((-x)>>s):x>>s;}
struct R{int hits,err,xy,ang,N,ar,gr,tr,ou,units;};
int main(){i128 at[40],gain[41];gain[0]=QONE;at[0]=PI_()/4;for(int i=1;i<40;i++)at[i]=i<30?atan_inv_(1<<i):(QONE>>i);for(int i=0;i<40;i++){i128 a=QONE+(QONE>>(2*i));i128 factor=root(a*QONE);gain[i+1]=gain[i]*QONE/factor;}
 std::vector<R> best;
 for(int xy=26;xy<=36;xy++)for(int ang=26;ang<=36;ang++)for(int N=22;N<=36;N++)for(int ar=0;ar<2;ar++)for(int gr=0;gr<2;gr++)for(int tr=0;tr<2;tr++)for(int ou=0;ou<2;ou++)for(int units=0;units<2;units++){
 i64 angles[40];for(int i=0;i<N;i++){i128 a=units?at[i]*QONE/(2*PI_()):at[i];angles[i]=i64((a+(ar?(i128(1)<<(59-ang)):0))>>(60-ang));}i64 g=i64((gain[N]+(gr?(i128(1)<<(59-xy)):0))>>(60-xy));int hits=0,err=0;
 for(int n=1;n<16;n++){i64 x=g,y=0; i128 theta=units?QONE*n/64:PI_()*n/32;i64 z=i64((theta+(ar?(i128(1)<<(59-ang)):0))>>(60-ang));for(int i=0;i<N;i++){i64 xs=shr(x,i,tr),ys=shr(y,i,tr);if(z>=0){x-=ys;y+=xs;z-=angles[i];}else{x+=ys;y-=xs;z+=angles[i];}}i64 out=(y+(ou&&xy>26?(1ll<<(xy-27)):0))>>(xy-26);hits+=out==target[n];err+=std::abs(out-target[n]);}best.push_back({hits,err,xy,ang,N,ar,gr,tr,ou,units});}
 std::sort(best.begin(),best.end(),[](R a,R b){return a.hits!=b.hits?a.hits>b.hits:a.err<b.err;});printf("structures %zu; score is interior entries 1..15 (axes excluded)\n",best.size());for(int i=0;i<12;i++){auto r=best[i];printf("hits%d error%d xy%d angle%d N%d angleRN%d gainRN%d truncZero%d outRN%d turns%d\n",r.hits,r.err,r.xy,r.ang,r.N,r.ar,r.gr,r.tr,r.ou,r.units);}}
