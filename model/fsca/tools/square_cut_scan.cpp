// Generic signed Booth squarer scan, independent of the fixed FIPR cut.
// All fits and comparisons are integer; printf decimal output is descriptive only.
// g++ -O3 -std=c++17 square_cut_scan.cpp -o /tmp/square_cut_scan
#include "fsca_common.h"
#include <algorithm>
#include <array>
#include <climits>
#include <unordered_map>
#include <string>
using I = __int128;
constexpr int Q=32;
constexpr i64 ONE=1ll<<Q;
constexpr I D=I(1)<<(29+Q);
struct Param{int a,b,gap,hot,nz,group;};
struct Sample{int t;i64 p;I low,high;};
struct Result{Param par; i64 c;I lo,hi;int lt,ht;std::vector<i64> powers;};
static std::array<I,1024> low,high;
static std::string str(I v){if(!v)return "0";bool neg=v<0;if(neg)v=-v;std::string s;while(v){s.push_back('0'+v%10);v/=10;}if(neg)s.push_back('-');std::reverse(s.begin(),s.end());return s;}
// Arithmetic right shift is used intentionally, on GCC/Clang, to truncate a
// signed row down to a column. No negative signed left shifts are used.
static i64 power(int t,Param p){
 const int shift=p.a+p.b,cut=shift-p.gap,hot=cut+p.hot;
 uint32_t x=uint32_t(i64(t)*(1ll<<p.b))&0xffffff;
 if(x&0x800000)x|=0x3000000;
 const i64 m=i64(t)*(1ll<<p.a);i64 rows[13],sum=0;
 for(int k=0;k<13;++k){int d=booth_digit(x,k);bool neg=p.nz?((x>>(2*k+1))&1):d<0;
 rows[k]=(d*m-neg)*(1ll<<(2*k));
 sum+=(rows[k]>>cut)*(1ll<<cut);
 if(neg&&2*k>=hot)sum+=1ll<<(2*k);
 }
 if((p.group&1)&&cut>0)sum+=(1ll<<cut)*(((rows[0]>>(cut-1))&1)+((rows[1]>>(cut-1))&1)+((rows[2]>>(cut-1))&1)>=2);
 if(p.group&2)sum-=(1ll<<cut)*(((rows[6]>>cut)^(rows[7]>>cut)^(rows[8]>>cut))&1);
 return sum*(1ll<<(Q-shift));
}
static Result interval(const std::vector<i64>& powers,i64 c,Param p){
 Result r;r.par=p;r.c=c;r.lo=-(I(1)<<110);r.hi=I(1)<<110;r.lt=r.ht=0;
 for(int i=0;i<1024;++i){I l=low[i]-I(c)*powers[i],h=high[i]-I(c)*powers[i];if(l>r.lo){r.lo=l;r.lt=i-512;}if(h<r.hi){r.hi=h;r.ht=i-512;}}
 return r;
}
static Result fit(const std::vector<i64>& powers,Param p){
 // max(lower lines)-min(upper lines) is convex in C2. Binary search its
 // integer minimum; this is not a local/coordinate coefficient fit.
 i64 l=165550000,h=165558000;
 while(l<h){i64 m=(l+h)/2;auto a=interval(powers,m,p),b=interval(powers,m+1,p);if(a.lo-a.hi<=b.lo-b.hi)h=m;else l=m+1;}
 auto r=interval(powers,l,p);r.powers=powers;return r;
}
static int mismatches(const Result&r,I bias){int n=0;for(int i=0;i<1024;++i){I v=I(r.c)*r.powers[i]+bias;n+=v<low[i]||v>high[i];}return n;}
static size_t hash(const std::vector<i64>& v){size_t h=1469598103934665603ull;for(i64 x:v){h^=u64(x);h*=1099511628211ull;}return h;}
static void print(const Result&r,const char*prefix){const Param&p=r.par;
 I width=r.lo-r.hi;printf("%s a=%d b=%d cut=%d gap=%d hot=+%d nz=%d groups=%d C2=%lld gapQ61=%s slack=%.12Lg bindings=%d,%d midpoint_misses=%d bias=[%s,%s]\n",prefix,p.a,p.b,p.a+p.b-p.gap,p.gap,p.hot,p.nz,p.group,(long long)r.c,str(width).c_str(),(long double)width/(long double)D/2,r.lt,r.ht,mismatches(r,(r.lo+r.hi)/2),str(r.lo).c_str(),str(r.hi).c_str());
}
#ifndef FSCA_SQUARE_SCAN_LIBRARY
int main(int argc,char**argv){Fsca f;if(!load_fsca(f))return 2;int maxgroup=argc>1?atoi(argv[1]):3;
 for(int i=0;i<1024;++i){Win w=decode(f.srz[16384+i-512],f.srn[16384+i-512]);i64 l=67108863-w.hi,h=67108863-w.lo;low[i]=l>0?I(l)*D:I(l-1)*D+1;high[i]=h<0?I(h)*D:I(h+1)*D-1;}
 std::vector<i64> exact(1024);for(int i=0;i<1024;++i)exact[i]=i64(i-512)*(i-512)*ONE;
 auto baseline=fit(exact,{0,0,0,0,0,0});print(baseline,"EXACT");
 std::vector<Result> results;std::unordered_map<size_t,std::vector<size_t>> cache;long configs=0;int pass=0;
 for(int a=0;a<=14;++a){for(int b=0;b<=14;++b)for(int gap=4;gap<=16;++gap){int cut=a+b-gap;if(cut<0)continue;
 for(int hot=0;hot<=4;++hot)for(int nz=0;nz<=1;++nz)for(int group=0;group<=maxgroup;++group){++configs;Param p{a,b,gap,hot,nz,group};std::vector<i64>v(1024);for(int i=0;i<1024;++i)v[i]=power(i-512,p);
 size_t key=hash(v);bool seen=false;auto&bucket=cache[key];for(size_t j:bucket)if(results[j].powers==v){seen=true;break;}if(seen)continue;
 auto r=fit(v,p);if(r.lo<=r.hi){++pass;print(r,"PASS");}bucket.push_back(results.size());results.push_back(std::move(r));
 }}fprintf(stderr,"a=%d configurations=%ld distinct=%zu feasible=%d\n",a,configs,results.size(),pass);}
 std::sort(results.begin(),results.end(),[](const Result&a,const Result&b){return a.lo-a.hi<b.lo-b.hi;});
 printf("SUMMARY configurations=%ld distinct=%zu feasible=%d\n",configs,results.size(),pass);for(size_t i=0;i<std::min<size_t>(30,results.size());++i)print(results[i],"BEST");
}
#endif
