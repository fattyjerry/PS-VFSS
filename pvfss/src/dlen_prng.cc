#include "dlen_prng.h"


void PPRG(osuCrypto::block s, osuCrypto::block *rand) {
  osuCrypto::AES aes(s);
  osuCrypto::block pt[2] = { osuCrypto::ZeroBlock, osuCrypto::OneBlock };

  aes.ecbEncBlocks(pt, 2, rand);
}

void HalfPRG(osuCrypto::block s, osuCrypto::block& rand, uint8_t b) {
  osuCrypto::AES aes(s);
  osuCrypto::block pt[2] = { osuCrypto::ZeroBlock, osuCrypto::OneBlock };

  aes.ecbEncBlock(pt[b], rand);
}
