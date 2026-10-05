// FSRRA node coefficient table, fitted against the SH4 dump (see sh4_fsrra.cpp for units).
// index = parity*17 + node ; node = (mantissa + 2^18) >> 19 ; parity = exponent LSB (0: [2,4) style... see code)
static const int64_t FSRRA_TAB[34][4] = {
  {67108864LL, -33554464LL, 98368LL, 2560LL},  // parity 0 node 0
  {65105160LL, -30637720LL, 84528LL, 2080LL},  // parity 0 node 1
  {63270842LL, -28120372LL, 73272LL, 1696LL},  // parity 0 node 2
  {61583317LL, -25929816LL, 64000LL, 1408LL},  // parity 0 node 3
  {60023991LL, -24009596LL, 56296LL, 1184LL},  // parity 0 node 4
  {58577421LL, -22315208LL, 49824LL, 992LL},  // parity 0 node 5
  {57230630LL, -20811136LL, 44360LL, 848LL},  // parity 0 node 6
  {55972662LL, -19468752LL, 39696LL, 720LL},  // parity 0 node 7
  {54794160LL, -18264720LL, 35680LL, 624LL},  // parity 0 node 8
  {53687088LL, -17179868LL, 32232LL, 544LL},  // parity 0 node 9
  {52644524LL, -16198314LL, 29208LL, 464LL},  // parity 0 node 10
  {51660427LL, -15306792LL, 26584LL, 416LL},  // parity 0 node 11
  {50729532LL, -14494152LL, 24280LL, 368LL},  // parity 0 node 12
  {49847212LL, -13750954LL, 22232LL, 320LL},  // parity 0 node 13
  {49009384LL, -13069168LL, 20424LL, 288LL},  // parity 0 node 14
  {48212432LL, -12441918LL, 18816LL, 256LL},  // parity 0 node 15
  {47453132LL, -11863282LL, 17392LL, 224LL},  // parity 0 node 16
  {47453132LL, -23726564LL, 69560LL, 1808LL},  // parity 1 node 0
  {46036301LL, -21664140LL, 59768LL, 1464LL},  // parity 1 node 1
  {44739243LL, -19884107LL, 51808LL, 1208LL},  // parity 1 node 2
  {43545981LL, -18335148LL, 45264LL, 992LL},  // parity 1 node 3
  {42443371LL, -16977348LL, 39808LL, 832LL},  // parity 1 node 4
  {41420491LL, -15779234LL, 35232LL, 696LL},  // parity 1 node 5
  {40468166LL, -14715696LL, 31368LL, 592LL},  // parity 1 node 6
  {39578650LL, -13766486LL, 28064LL, 504LL},  // parity 1 node 7
  {38745321LL, -12915106LL, 25232LL, 440LL},  // parity 1 node 8
  {37962507LL, -12148002LL, 22776LL, 376LL},  // parity 1 node 9
  {37225301LL, -11453938LL, 20648LL, 328LL},  // parity 1 node 10
  {36529438LL, -10823536LL, 18800LL, 288LL},  // parity 1 node 11
  {35871196LL, -10248912LL, 17168LL, 256LL},  // parity 1 node 12
  {35247303LL, -9723394LL, 15720LL, 224LL},  // parity 1 node 13
  {34654868LL, -9241298LL, 14440LL, 200LL},  // parity 1 node 14
  {34091336LL, -8797764LL, 13312LL, 176LL},  // parity 1 node 15
  {33554431LL, -8388640LL, 12296LL, 160LL},  // parity 1 node 16
};
