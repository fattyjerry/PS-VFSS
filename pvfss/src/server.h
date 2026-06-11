//
// Created by fxy on 4/9/24.
//

#ifndef __SERVER_H
#define __SERVER_H

#include "opv.h"
#include "dlen_prg.h"
#include "dpf.h"
#include<cstring>
#include <emmintrin.h>
#include <vector>
extern "C" {
#include <stdio.h>
#include <relic/relic_bn.h>
}

#include <iostream>
#include <vector>
#include <chrono>
using namespace emp;

/*class server {

};*/
void BTGen(int party, HighSpeedNetIO *io, block &aa, block &bb, block &cch, block &ccl);
void dpfkeygen(int party, HighSpeedNetIO *io, DPFKey &dpfk);
block Mult(int party,HighSpeedNetIO *io,uint64_t N,block x,block y,block a,block b,block ch,block cl);
void CPRS(int party,HighSpeedNetIO *io,uint64_t N,block *v,block ran,block fa,block fb,block fch,block fcl, std::vector<uint64_t>& sig);
int testa();
int testb();

#endif //__SERVER_H
