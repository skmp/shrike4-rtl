// Quantize the two transfer survivors and validate integer coefficient witnesses.
#define FSCA_SQUARE_OFFSET_LIBRARY
#include "square_offset_scan.cpp"
static Z floorR(R a){Z z;mpz_fdiv_q(z.get_mpz_t(),a.get_num_mpz_t(),a.get_den_mpz_t());return z;}
static Z ceilR(R a){Z z;mpz_cdiv_q(z.get_mpz_t(),a.get_num_mpz_t(),a.get_den_mpz_t());return z;}
static bool beta_interval(const std::vector<i64>&p,i64 c16,R mu,R&lo,R&hi){
 lo=-1;hi=1;
 for(int node:{16,2,1}){i64 c=node==16?c16:node==2?32298060:16227180;
 for(int i=0;i<1024;++i){I l=node==16?low[i]:mirrorLow[node-1][i],h=node==16?high[i]:mirrorHigh[node-1][i];
 R pl(R((long)p[i])/R(ONE)+mu*(i-512));R a=R(zi(l),Z((long)c)*Z(ONE))-pl,b=R(zi(h),Z((long)c)*Z(ONE))-pl;
 if(a>lo)lo=a;if(b<hi)hi=b;if(lo>hi)return false;
 }}return true;
}
int main(){Fsca f;if(!load_fsca(f))return 2;
 for(int i=0;i<1024;++i){Win w=decode(f.srz[16384+i-512],f.srn[16384+i-512]);i64 l=67108863-w.hi,h=67108863-w.lo;low[i]=l>0?I(l)*D:I(l-1)*D+1;high[i]=h<0?I(h)*D:I(h+1)*D-1;}
 for(int n=1;n<=2;++n)for(int i=0;i<1024;++i){int t=i-512;Win a=decode(f.srz[n*1024+t],f.srn[n*1024+t]),b=decode(f.srz[(32-n)*1024+t],f.srn[(32-n)*1024+t]);i64 c0=n==1?6577819:13092290,l=c0-(a.hi+b.hi)/2,h=c0-(a.lo+b.lo+1)/2;
 mirrorLow[n-1][i]=l>0?I(l)*D:I(l-1)*D+1;mirrorHigh[n-1][i]=h<0?I(h)*D:I(h+1)*D-1;}
 for(Param par: {Param{12,11,7,3,0,2},Param{12,11,8,4,0,2}}){std::vector<i64>p(1024);for(int i=0;i<1024;++i)p[i]=power(i-512,par);
 int count=0,quant=0;
 for(i64 c16=165554200;c16<=165554450;++c16){Point pt;if(!feasible3(p,pt,0,2,c16))continue;++count;
 bool done=false;R origmu=pt.y/(pt.x*256);
 for(int bits=10;bits<=32&&!done;++bits){Z den=Z(1)<<bits,base=floorR(origmu*den);
 for(int delta=-2;delta<=2&&!done;++delta){R mu(base+delta,den);mu.canonicalize();R lo,hi;if(!beta_interval(p,c16,mu,lo,hi))continue;
 for(int b=0;b<=32&&!done;++b){Z bd=Z(1)<<b,bn=ceilR(lo*bd);R beta(bn,bd);beta.canonicalize();if(beta>hi)continue;
 printf("QUANT cut=%d C16=%lld mantissa23=%d mu=%s beta=%s\n",par.a+par.b-par.gap,(long long)c16,c16%32==0,mu.get_str().c_str(),beta.get_str().c_str());fflush(stdout);++quant;done=true;
 }
 }}
 }
 printf("cut=%d integer C16 feasible=%d dyadic witnesses=%d\n",par.a+par.b-par.gap,count,quant);
 }
}
