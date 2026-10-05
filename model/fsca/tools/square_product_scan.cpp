// Truncated coefficient-times-exact-square array, with affine compensation.
// Node 16 first; survivors transfer to independently isolated squares at 1,2.
// g++ -O3 -std=c++17 square_product_scan.cpp -lgmpxx -lgmp -o /tmp/square_product_scan
#define FSCA_SQUARE_OFFSET_LIBRARY
#include "square_offset_scan.cpp"
struct Config{int shift,gap,hot,nz,group,rec;i64 c16;};
static std::array<i64,1024> slo[3],shi[3];
constexpr i64 F=1ll<<32;
static i64 mulraw(uint32_t x,uint32_t m,int cut,int hot,int nz,int group){i64 row[13],v=0;
 for(int k=0;k<13;++k){int d=booth_digit(x,k);bool neg=nz?((x>>(2*k+1))&1):d<0;row[k]=(i64(d)*m-neg)*(1ll<<(2*k));v+=(row[k]>>cut)*(1ll<<cut);if(neg&&2*k>=hot)v+=1ll<<(2*k);}
 if(group&1)v+=(1ll<<cut)*(((row[0]>>(cut-1))&1)+((row[1]>>(cut-1))&1)+((row[2]>>(cut-1))&1)>=2);
 if(group&2)v-=(1ll<<cut)*(((row[6]>>cut)^(row[7]>>cut)^(row[8]>>cut))&1);
 return v;
}
static std::vector<i64> product(Config p,i64 c){int e=std::max(0,msb(c)-23),unit=29-e+p.shift,cut=25+p.shift-p.gap;
 uint32_t m=c>>e;std::vector<i64> v(1024);
 for(int i=0;i<1024;++i){int t=i-512;uint32_t sq=uint32_t(t*t)<<p.shift;
 i64 r=mulraw(p.rec?m:sq,p.rec?sq:m,cut,cut+p.hot,p.nz,p.group);
 // Can have up to unit=34 at low coefficients; raw rows are divisible by
 // 2^cut, so the right shift is exact, including negative values at t=0.
 v[i]=unit<=32?r*(1ll<<(32-unit)):r/(1ll<<(unit-32));}
 return v;
}
struct FitAffine{i64 h,lo,hi;int lt,ht;};
static FitAffine interval_affine(const std::vector<i64>&v,i64 h){FitAffine r{h,LLONG_MIN/4,LLONG_MAX/4,0,0};for(int i=0;i<1024;++i){i64 l=slo[0][i]-v[i]-h*(i-512),u=shi[0][i]-v[i]-h*(i-512);if(l>r.lo){r.lo=l;r.lt=i-512;}if(u<r.hi){r.hi=u;r.ht=i-512;}}return r;}
static FitAffine fit_affine(const std::vector<i64>&v){i64 l=-(1<<22),h=1<<22;while(l<h){i64 m=(l+h)/2;auto a=interval_affine(v,m),b=interval_affine(v,m+1);if(a.lo-a.hi<=b.lo-b.hi)h=m;else l=m+1;}return interval_affine(v,l);}
static bool transfer(Config p,const std::vector<i64>&v,int mode,R&h,R&b){std::vector<Line>ls={{1,0,1<<22},{-1,0,1<<22},{0,1,Z(1)<<32},{0,-1,Z(1)<<32}};
 for(int node=0;node<3;++node){i64 c=node==0?p.c16:node==1?16227180:32298060;int e=std::max(0,msb(c)-23);auto q=node==0?v:product(p,c);
 // H_j/H16 = Cj/C16 (smooth power bias). B is either a shared power
 // bias, fixed-column array compensation, or final-unit compensation.
 i64 hmul=c,bmul=mode==0?c:mode==1?p.c16:p.c16,den=p.c16;
 if(mode==1){int shift=4-e;hmul*=1ll<<shift;den*=1ll<<shift;}
 for(int i=0;i<1024;++i){Z a=Z((long)(hmul*(i-512))),bb=Z((long)bmul),hi=zi(I(shi[node][i]-q[i])*den),lo=zi(I(slo[node][i]-q[i])*den);ls.push_back({a,bb,hi});ls.push_back({-a,-bb,-lo});}
 }
 std::mt19937 rng(7091);std::shuffle(ls.begin()+4,ls.end(),rng);bool ok=feasible2(ls,h,b);
 if(ok)for(const auto&l:ls)if(!inside(l,h,b)){fprintf(stderr,"transfer verification failed\n");abort();}
 return ok;
}
static void show(Config p,FitAffine f,const char*label,int mode=-1){printf("%s C16=%lld shift=%d rec=%d cut=%d gap=%d hot=+%d nz=%d groups=%d affineH=%lld B=[%lld,%lld] slack=%.12Lg mode=%d\n",label,(long long)p.c16,p.shift,p.rec,25+p.shift-p.gap,p.gap,p.hot,p.nz,p.group,(long long)f.h,(long long)f.lo,(long long)f.hi,(long double)(f.lo-f.hi)/F/2,mode);fflush(stdout);}
int main(int argc,char**argv){Fsca f;if(!load_fsca(f))return 2;bool wide=argc>1&&atoi(argv[1]);
 for(int node=0;node<3;++node)for(int i=0;i<1024;++i){int t=i-512;i64 l,h;if(node==0){Win w=decode(f.srz[16384+t],f.srn[16384+t]);l=67108863-w.hi;h=67108863-w.lo;}else{Win a=decode(f.srz[node*1024+t],f.srn[node*1024+t]),b=decode(f.srz[(32-node)*1024+t],f.srn[(32-node)*1024+t]);i64 c0=node==1?6577819:13092290;l=c0-(a.hi+b.hi)/2;h=c0-(a.lo+b.lo+1)/2;}slo[node][i]=l>0?l*F:(l-1)*F+1;shi[node][i]=h<0?h*F:(h+1)*F-1;}
 long configs=0,pass16=0,pass[3]={};FitAffine best{0,LLONG_MAX/4,0,0,0};Config bc{};
 for(i64 c16=165553792;c16<=165554816;c16+=32){
 for(int shift=0;shift<=5;++shift)for(int rec=0;rec<2;++rec)for(int gap=wide?6:8;gap<=(wide?16:12);++gap)for(int hot=0;hot<=(wide?4:2);hot+=wide?1:2)for(int nz=0;nz<=1;++nz)for(int group=0;group<=3;group+=wide?1:3){Config p{shift,gap,hot,nz,group,rec,c16};++configs;auto v=product(p,c16);auto fit=fit_affine(v);if(fit.lo-fit.hi<best.lo-best.hi){best=fit;bc=p;}
 if(fit.lo>fit.hi)continue;++pass16;
 for(int mode=0;mode<3;++mode){R h,b;if(transfer(p,v,mode,h,b)){++pass[mode];show(p,fit,"TRANSFER",mode);printf(" H=%s B=%s (units 2^-32)\n",h.get_str().c_str(),b.get_str().c_str());}}
 }
 fprintf(stderr,"C16=%lld configs=%ld node16=%ld transfer=%ld,%ld,%ld\n",(long long)c16,configs,pass16,pass[0],pass[1],pass[2]);}
 printf("SUMMARY configs=%ld node16=%ld transfer=%ld,%ld,%ld wide=%d\n",configs,pass16,pass[0],pass[1],pass[2],wide);show(bc,best,"BEST");
}
