// Exact rational feasibility for square-array + affine power bias.
// Links system GMP: g++ -O3 -std=c++17 square_offset_scan.cpp -lgmpxx -lgmp -o /tmp/square_offset_scan
// V=67108863-tz((C2*array_Q32(t)+H*t*2^24+B)/2^61).
// H is a smooth signed offset in the power, B is common compensation.
// Both are continuous here, an intentionally generous relaxation of hardware.
#define FSCA_SQUARE_SCAN_LIBRARY
#include "square_cut_scan.cpp"
#include <gmpxx.h>
#include <random>
using Z=mpz_class;using R=mpq_class;
static Z zi(I x){return Z(str(x));}
struct Plane{Z a,b,c,d;}; // d * B + a*x + b*y <= c; x=C2-base
struct Line{Z a,b,c;};
struct Point{R x,y,b;};
static bool inside(const Line&l,R x,R y){return l.a*x+l.b*y<=l.c;}
static bool feasible2(const std::vector<Line>& ls,R&x,R&y){
 x=0;y=0;
 for(size_t i=0;i<ls.size();++i){const auto&l=ls[i];if(inside(l,x,y))continue;
 if(l.a==0&&l.b==0)return false;
 // Solve l for a dependent variable. The free variable is bounded by
 // the box constraints present first in ls, so no artificial infinity.
 bool swap=l.a==0;Z a=swap?l.b:l.a,b=swap?l.a:l.b,c=l.c;
 R lower,upper;bool haslo=false,hashi=false;
 for(size_t j=0;j<i;++j){const auto&v=ls[j];Z d=swap?v.b:v.a,e=swap?v.a:v.b;
 Z lhs=a*e-d*b,rhs=a*v.c-d*c;if(a<0){lhs=-lhs;rhs=-rhs;}
 if(lhs==0){if(rhs<0)return false;continue;}
 R bound(rhs,lhs);bound.canonicalize();
 if(lhs>0){if(!hashi||bound<upper){upper=bound;hashi=true;}}else{if(!haslo||bound>lower){lower=bound;haslo=true;}}
 if(haslo&&hashi&&lower>upper)return false;
 }
 R free=0;if(haslo&&free<lower)free=lower;if(hashi&&free>upper)free=upper;
 R dependent=(R(c)-R(b)*free)/R(a);
 if(swap){y=dependent;x=free;}else{x=dependent;y=free;}
 }
 return true;
}
static std::array<I,1024> mirrorLow[2],mirrorHigh[2];
static bool feasible3(const std::vector<i64>& powers,Point&pt,i64 slack=0,int transfer=0,i64 fixed16=0){
 const i64 base=165554300;std::vector<Plane> ps;
 const Z box=zi(D/2);
 ps.push_back({0,0,box,1});ps.push_back({0,0,box,-1});
 std::vector<int> order(1024);for(int i=0;i<1024;++i)order[i]=i;
 std::mt19937 rng(7091);std::shuffle(order.begin(),order.end(),rng);
 // slack measured in 2^-32 internal units, not native floating point.
 const I widening=I(slack)*(I(1)<<29);
 for(int i:order){Z a=Z((long)powers[i]),b=Z((long)((i-512)*(1ll<<24)));
 ps.push_back({a,b,zi(high[i]+widening-I(base)*powers[i]),1});
 ps.push_back({-a,-b,zi(-low[i]+widening+I(base)*powers[i]),-1});}
 for(int node=2;node>=1&&node>2-transfer;--node){const i64 coefficient=node==2?32298060:16227180;
 for(int i:order){Z a=zi(I(coefficient)*powers[i]-mirrorHigh[node-1][i]);
 Z b=zi(I(coefficient)*(i-512)*(1ll<<24));
 ps.push_back({a,b,-base*a,Z((long)coefficient)});
 a=zi(mirrorLow[node-1][i]-I(coefficient)*powers[i]);
 ps.push_back({a,-b,-base*a,-Z((long)coefficient)});
 }}
 std::shuffle(ps.begin()+2,ps.end(),rng);
 pt={fixed16?fixed16-base:0,0,0};
 const std::vector<Line> box2={{1,0,fixed16?fixed16-base:3700},{-1,0,fixed16?base-fixed16:4300},{0,1,1<<26},{0,-1,1<<26}};
 for(size_t i=0;i<ps.size();++i){const auto&p=ps[i];if(p.a*pt.x+p.b*pt.y+p.d*pt.b<=p.c)continue;
 std::vector<Line> lines=box2;lines.reserve(i+4);
 for(size_t j=0;j<i;++j){const auto&v=ps[j];int sign=p.d>0?1:-1;
 lines.push_back({sign*(v.a*p.d-v.d*p.a),sign*(v.b*p.d-v.d*p.b),sign*(v.c*p.d-v.d*p.c)});}
 if(!feasible2(lines,pt.x,pt.y))return false;
 pt.b=(p.c-p.a*pt.x-p.b*pt.y)/p.d;
 }
 // Independent final pass, including all box constraints.
 for(const auto&p:ps)if(p.a*pt.x+p.b*pt.y+p.d*pt.b>p.c){fprintf(stderr,"LP verification failure\n");abort();}
 for(const auto&l:box2)if(!inside(l,pt.x,pt.y)){fprintf(stderr,"LP box failure\n");abort();}
 pt.x+=base;return true;
}
static void show(Param p,const Point&pt,const char*label){R mu=pt.y/(pt.x*256),beta=pt.b/(pt.x*R(ONE));
 printf("%s a=%d b=%d cut=%d gap=%d hot=+%d nz=%d groups=%d C2=%s mu=%s beta=%s\n",label,p.a,p.b,p.a+p.b-p.gap,p.gap,p.hot,p.nz,p.group,pt.x.get_str().c_str(),mu.get_str().c_str(),beta.get_str().c_str());fflush(stdout);}
#ifndef FSCA_SQUARE_OFFSET_LIBRARY
int main(int argc,char**argv){Fsca f;if(!load_fsca(f))return 2;int maxa=argc>1?atoi(argv[1]):14,transfer=argc>2?atoi(argv[2]):0;
 for(int i=0;i<1024;++i){Win w=decode(f.srz[16384+i-512],f.srn[16384+i-512]);i64 l=67108863-w.hi,h=67108863-w.lo;low[i]=l>0?I(l)*D:I(l-1)*D+1;high[i]=h<0?I(h)*D:I(h+1)*D-1;}
 for(int n=1;n<=2;++n)for(int i=0;i<1024;++i){int t=i-512;Win a=decode(f.srz[n*1024+t],f.srn[n*1024+t]),b=decode(f.srz[(32-n)*1024+t],f.srn[(32-n)*1024+t]);i64 c0=n==1?6577819:13092290,l=c0-(a.hi+b.hi)/2,h=c0-(a.lo+b.lo+1)/2;
 mirrorLow[n-1][i]=l>0?I(l)*D:I(l-1)*D+1;mirrorHigh[n-1][i]=h<0?I(h)*D:I(h+1)*D-1;}
 std::vector<i64> exact(1024);for(int i=0;i<1024;++i)exact[i]=i64(i-512)*(i-512)*ONE;
 Point point;bool baseline=feasible3(exact,point);printf("EXACT+AFFINE feasible=%d\n",baseline);
 if(!baseline){i64 l=0,h=1<<26;while(l<h){i64 m=(l+h)/2;if(feasible3(exact,point,m))h=m;else l=m+1;}printf("EXACT+AFFINE minimum symmetric slack numerator=%lld /2^32\n",(long long)l);}
 std::vector<std::vector<i64>> powers;std::unordered_map<size_t,std::vector<size_t>>cache;long configs=0;int pass=0;
 for(int a=0;a<=maxa;++a){for(int b=0;b<=14;++b)for(int gap=4;gap<=16;++gap){if(a+b<gap)continue;
 for(int hot=0;hot<=4;++hot)for(int nz=0;nz<=1;++nz)for(int group=0;group<=3;++group){++configs;Param p{a,b,gap,hot,nz,group};std::vector<i64>v(1024);for(int i=0;i<1024;++i)v[i]=power(i-512,p);
 size_t key=hash(v);bool seen=false;auto&bucket=cache[key];for(size_t j:bucket)if(powers[j]==v){seen=true;break;}if(seen)continue;
 bool ok=feasible3(v,point);if(ok&&transfer)ok=feasible3(v,point,0,transfer);
 if(ok){++pass;show(p,point,transfer?"TRANSFER":"PASS");}
 bucket.push_back(powers.size());powers.push_back(std::move(v));
 }}fprintf(stderr,"a=%d configs=%ld distinct=%zu feasible=%d\n",a,configs,powers.size(),pass);}
 printf("SUMMARY configs=%ld distinct=%zu feasible=%d\n",configs,powers.size(),pass);
}
#endif
