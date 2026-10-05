// v40_a.cpp -- ADVERSARIAL VERIFIER (branch 40) for the "loss-alignment" claims.
//  1. independent baseline score vs jwin18.txt (claimed 918/1024, landmarks 45630/18466846/18466857)
//  2. the array loss law:  arr(rec,mul) - rec*mul  ==  -sum_{2k<16}(R[k] mod 2^16)
//                                                     -sum_{d<0,2k<18} 2^(2k) + 2^16*(g1-g2)
//     and the a-priori bound [-655360, +65536]; plus the key-dependence claim
//     loss = f(rec mod 2^18, mul mod 2^16)
//  3. observed loss range: random pairs, and the 2048 actual square-path calls
//  4. the ENVELOPE: both losses free over the whole a-priori range, both rounding constants free
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <map>
#include <algorithm>

static i64 JL[1024], JH[1024];
static bool loadwin(const char* p){ FILE*f=fopen(p,"r"); if(!f) return false; char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#') continue; long long t,a,b,w; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4) continue;
    if(t<-512||t>511){fprintf(stderr,"bad t %lld\n",t); return false;} if(b-a+1!=w){fprintf(stderr,"width mismatch at t=%lld\n",t); return false;}
    JL[t+512]=a; JH[t+512]=b; n++; }
  fclose(f); if(n!=1024){fprintf(stderr,"only %d rows\n",n); return false;} return true; }

// ---- re-derivation of the array's internals, written independently of arr() but from the same source text
static i64 modp(i64 v,int b){ i64 m=v&((1ll<<b)-1); return m; }   // v & (2^b-1) == v mod 2^b for two's complement
struct Dec { i64 loss; int nrow_neg_lo; i64 sum_mod; int g1,g2; };
static Dec decompose(i64 rec, i64 mul){
    i64 R[14]; Dec D{0,0,0,0,0};
    for(int k=0;k<14;k++){
        auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
        i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag; R[k]=row*(1ll<<(2*k));
        if(2*k<16){ i64 m=modp(R[k],16); D.sum_mod+=m; }
        if(d<0 && 2*k<18){ D.loss-=1ll<<(2*k); D.nrow_neg_lo++; }
    }
    D.loss-=D.sum_mod;
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1); D.g1=(m15>=2);
    D.g2=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1;
    D.loss+=(1ll<<16)*(i64)(D.g1-D.g2);
    return D;
}
static i64 sext28(i64 v){ i64 m=v&((1ll<<28)-1); return m>=(1ll<<27)? m-(1ll<<28): m; }

int main(){
    if(!loadwin("jwin18.txt")){ printf("FATAL: jwin18.txt\n"); return 1; }
    // sanity on the dataset itself
    { int bad=0; for(int i=0;i<1024;i++) if(JL[i]>JH[i]) bad++; printf("[0] jwin18: 1024 rows, %d with JL>JH, JL(17)=%lld JH(17)=%lld  JL(342)=%lld JH(342)=%lld  JL(-342)=%lld JH(-342)=%lld\n",
        bad,(long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],(long long)JL[-342+512],(long long)JH[-342+512]); }

    // ---- 1. baseline
    int score=0; for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
        if(J>=JL[t+512]&&J<=JH[t+512]) score++; }
    i64 j17,j342,jm342; { i64 dx=(i64)17<<13; i64 I1=(arr(KC,dx)+K)>>23; j17=(arr(I1,dx)+K)>>18;
        dx=(i64)342<<13; I1=(arr(KC,dx)+K)>>23; j342=(arr(I1,dx)+K)>>18;
        dx=(i64)(-342)<<13; I1=(arr(KC,dx)+K)>>23; jm342=(arr(I1,dx)+K)>>18; }
    printf("[1] baseline model (w=13,s1=23,s2=18,K=2^19): score %d/1024 ; J(17)=%lld J(342)=%lld J(-342)=%lld\n",
        score,(long long)j17,(long long)j342,(long long)jm342);

    // ---- 2. loss law over 4 sign quadrants
    std::mt19937_64 rng(20260918);
    const i64 LO=-655360, HI=65536;
    long long mism=0, boundviol=0, N=0; i64 obsmin=0, obsmax=0; bool first=true;
    for(int q=0;q<4;q++){
        i64 smul=(q&1)?-1:1, srec=(q&2)?-1:1;
        for(int i=0;i<500000;i++){
            i64 rec=srec*(i64)(rng()%(1ull<<27)), mul=smul*(i64)(rng()%(1ull<<24));
            i64 a=arr(rec,mul); __int128 ideal=(__int128)rec*mul; i64 loss=(i64)(a-ideal);
            Dec D=decompose(rec,mul);
            if(loss!=D.loss) mism++;
            if(loss<LO||loss>HI) boundviol++;
            if(first){obsmin=obsmax=loss;first=false;} else {obsmin=std::min(obsmin,loss); obsmax=std::max(obsmax,loss);}
            N++;
        }
    }
    printf("[2] loss law: %lld pairs (4 quadrants, |rec|<2^27, |mul|<2^24): formula mismatches %lld ; bound[-655360,65536] violations %lld ; observed loss range [%lld,%lld]\n",
        (long long)N,mism,boundviol,(long long)obsmin,(long long)obsmax);

    // 2b. is the a-priori bound actually -655360?  maximise the decomposition term by term
    {   // sum_{2k<16}(R[k] mod 2^16): R[k] is a multiple of 2^(2k), so max term = 2^16 - 2^(2k)
        i64 mx=0; for(int k=0;k<8;k++) mx += (1ll<<16)-(1ll<<(2*k));
        i64 hot=0; for(int k=0;k<9;k++) hot += 1ll<<(2*k);
        printf("[2b] a-priori: max sum(R[k] mod 2^16)=%lld  max hot-one deficit=%lld  +2^16 for -g2  => LOWER BOUND %lld ; naive 8*(2^16-1) version would give %lld\n",
            (long long)mx,(long long)hot,(long long)-(mx+hot+65536),(long long)-(8*65535+hot+65536));
    }
    // 2c. key dependence: loss == f(rec mod 2^18, mul mod 2^16)?
    {   std::map<std::pair<i64,i64>,i64> M; long long coll=0,n=0;
        for(int i=0;i<400000;i++){
            i64 rec=(i64)(rng()%(1ull<<27))*((rng()&1)?1:-1), mul=(i64)(rng()%(1ull<<24))*((rng()&1)?1:-1);
            i64 loss=(i64)(arr(rec,mul)-(__int128)rec*mul);
            auto key=std::make_pair(rec&((1ll<<18)-1), mul&((1ll<<16)-1));
            auto it=M.find(key); if(it==M.end()) M[key]=loss; else if(it->second!=loss) coll++;
            n++; }
        printf("[2c] loss==f(rec mod 2^18, mul mod 2^16): %lld probes, %zu distinct keys, %lld contradictions\n",n,M.size(),coll); }
    // 2d. domain caveat: does the law hold for rec >= 2^27 (28-bit wrap)?
    {   long long bad=0; for(int i=0;i<200000;i++){ i64 rec=(i64)(rng()%(1ull<<30)), mul=(i64)(rng()%(1ull<<24));
            i64 loss=(i64)(arr(rec,mul)-(__int128)rec*mul); if(loss<LO||loss>HI) bad++; }
        printf("[2d] DOMAIN CAVEAT: rec uniform in [0,2^30) (outside the 28-bit Booth window): %lld/200000 pairs break the bound (sext28 wrap, not a loss)\n",bad); }

    // ---- 3. observed range on the real square-path calls
    {   i64 mn=0,mx=0; bool f=true; i64 mn2=0,mx2=0; bool f2=true;
        for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13;
            i64 l1=(i64)(arr(KC,dx)-(__int128)KC*dx); i64 I1=(arr(KC,dx)+K)>>23;
            i64 l2=(i64)(arr(I1,dx)-(__int128)I1*dx);
            if(f){mn=mx=l1;f=false;} else {mn=std::min(mn,l1);mx=std::max(mx,l1);}
            if(f2){mn2=mx2=l2;f2=false;} else {mn2=std::min(mn2,l2);mx2=std::max(mx2,l2);} }
        printf("[3] real calls: loss1 range [%lld,%lld] ; loss2 range [%lld,%lld]  (units of the raw product)\n",
            (long long)mn,(long long)mx,(long long)mn2,(long long)mx2);
        // distinct loss values
        std::vector<i64> a,b; for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13; i64 l1=(i64)(arr(KC,dx)-(__int128)KC*dx);
            i64 I1=(arr(KC,dx)+K)>>23; i64 l2=(i64)(arr(I1,dx)-(__int128)I1*dx); a.push_back(l1); b.push_back(l2); }
        std::sort(a.begin(),a.end()); a.erase(std::unique(a.begin(),a.end()),a.end());
        std::sort(b.begin(),b.end()); b.erase(std::unique(b.begin(),b.end()),b.end());
        printf("[3b] distinct loss1 values over the 1024 offsets: %zu ; distinct loss2: %zu\n",a.size(),b.size()); }

    // ---- 4. ENVELOPE at w=13, (s1,s2)=(23,18): loss1,loss2 free in [LO,HI], K1 in [0,2^23), K2 in [0,2^18)
    {   int out=0, worst_t=0; i64 worst_gap=0;
        i64 e17lo=0,e17hi=0,e342lo=0,e342hi=0,em342lo=0,em342hi=0;
        for(int t=-512;t<512;t++){
            i64 dx=(i64)t<<13; __int128 base=(__int128)KC*dx;
            // I1 = floor((KC*dx + loss1 + K1)/2^23), loss1 in [LO,HI], K1 in [0,2^23)
            __int128 nlo=base+LO, nhi=base+HI+((1ll<<23)-1);
            i64 I1lo=(i64)((nlo>=0? nlo : nlo-((1ll<<23)-1))/(1ll<<23));
            i64 I1hi=(i64)((nhi>=0? nhi : nhi-((1ll<<23)-1))/(1ll<<23));
            // products over that I1 range (dx sign decides orientation)
            __int128 p1=(__int128)I1lo*dx, p2=(__int128)I1hi*dx;
            __int128 plo=std::min(p1,p2), phi=std::max(p1,p2);
            __int128 qlo=plo+LO, qhi=phi+HI+((1ll<<18)-1);
            i64 Jlo=(i64)((qlo>=0? qlo : qlo-((1ll<<18)-1))/(1ll<<18));
            i64 Jhi=(i64)((qhi>=0? qhi : qhi-((1ll<<18)-1))/(1ll<<18));
            if(t==17){e17lo=Jlo;e17hi=Jhi;} if(t==342){e342lo=Jlo;e342hi=Jhi;} if(t==-342){em342lo=Jlo;em342hi=Jhi;}
            if(Jhi<JL[t+512]||Jlo>JH[t+512]){ out++; i64 g=(Jhi<JL[t+512])? JL[t+512]-Jhi : Jlo-JH[t+512];
                if(g>worst_gap){worst_gap=g;worst_t=t;} } }
        printf("[4] ENVELOPE (both losses free over the FULL a-priori range, both K free): %d/1024 offsets unreachable, worst t=%d by %lld latch units\n",out,worst_t,(long long)worst_gap);
        printf("    t=17    envelope [%lld,%lld]  required [%lld,%lld]\n",(long long)e17lo,(long long)e17hi,(long long)JL[17+512],(long long)JH[17+512]);
        printf("    t=342   envelope [%lld,%lld]  required [%lld,%lld]\n",(long long)e342lo,(long long)e342hi,(long long)JL[342+512],(long long)JH[342+512]);
        printf("    t=-342  envelope [%lld,%lld]  required [%lld,%lld]\n",(long long)em342lo,(long long)em342hi,(long long)JL[-342+512],(long long)JH[-342+512]); }
    return 0;
}
