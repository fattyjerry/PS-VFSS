//
// Created by fxy on 4/9/24.
//
#include <iostream>
#include <iomanip>
#include "server.h"
#include<cstring>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <sys/socket.h>
#include <cerrno>
extern "C"{
#include <relic/relic.h>
#include <relic/relic_bn.h>
#include <smmintrin.h>
}

namespace {

constexpr uint32_t kMaxBnFrameBytes = 8192;

void send_bn(NetIO *io, bn_t value) {
    const uint32_t size = static_cast<uint32_t>(bn_size_bin(value));
    if (size > kMaxBnFrameBytes) {
        throw std::runtime_error("BTGen BN frame exceeds configured maximum");
    }
    std::vector<uint8_t> buffer(size);
    if (size != 0) {
        bn_write_bin(buffer.data(), static_cast<int>(size), value);
    }
    const uint8_t length_le[4] = {
        static_cast<uint8_t>(size),
        static_cast<uint8_t>(size >> 8),
        static_cast<uint8_t>(size >> 16),
        static_cast<uint8_t>(size >> 24),
    };
    io->send_data(length_le, sizeof(length_le));
    if (size != 0) {
        io->send_data(buffer.data(), static_cast<int>(size));
    }
    io->flush();
}

void recv_bn(NetIO *io, bn_t value) {
    uint8_t length_le[4] = {};
    io->recv_data(length_le, sizeof(length_le));
    const uint32_t size = static_cast<uint32_t>(length_le[0]) |
                          (static_cast<uint32_t>(length_le[1]) << 8) |
                          (static_cast<uint32_t>(length_le[2]) << 16) |
                          (static_cast<uint32_t>(length_le[3]) << 24);
    if (size > kMaxBnFrameBytes) {
        throw std::runtime_error("BTGen received invalid BN frame length");
    }
    std::vector<uint8_t> buffer(size);
    if (size != 0) {
        io->recv_data(buffer.data(), static_cast<int>(size));
        bn_read_bin(value, buffer.data(), static_cast<int>(size));
    } else {
        bn_zero(value);
    }
}

}  // namespace


void bn_2_block_128(bn_t a, block &b){
    int len = bn_size_bin(a);
    uint8_t buffer[len];
    memset(buffer,0, len);
    bn_write_bin(buffer, len, a);

    uint64_t r1 = 0, r2 = 0;
    int buffer_index = len - 1;

    for (int i = 0; i < 8 && buffer_index >= 0; i++) {
        r2 |= ((uint64_t)buffer[buffer_index--]) << (i * 8);
    }
    for (int i = 0; i < 8 && buffer_index >= 0; i++) {
        r1 |= ((uint64_t)buffer[buffer_index--]) << (i * 8);
    }

    b = makeBlock(r1, r2);

    //std::cout<<"bn_t = ";bn_print(a);
    //std::cout<<"block= "<<b<<endl;
}

void bn_2_block_256(bn_t a, block &b2, block &b1){
    int len = bn_size_bin(a);
    uint8_t buffer[len];
    memset(buffer,0, len);
    bn_write_bin(buffer, len, a);

    uint64_t r1 = 0, r2 = 0, r3 = 0, r4 = 0;
    int buffer_index = len - 1;

    for (int i = 0; i < 8 && buffer_index >= 0; i++) {
        r2 |= ((uint64_t)buffer[buffer_index--]) << (i * 8);
    }
    for (int i = 0; i < 8 && buffer_index >= 0; i++) {
        r1 |= ((uint64_t)buffer[buffer_index--]) << (i * 8);
    }
    b1 = makeBlock(r1, r2);

    for (int i = 0; i < 8 && buffer_index >= 0; i++) {
        r4 |= ((uint64_t)buffer[buffer_index--]) << (i * 8);
    }
    for (int i = 0; i < 8 && buffer_index >= 0; i++) {
        r3 |= ((uint64_t)buffer[buffer_index--]) << (i * 8);
    }
    b2 = makeBlock(r3, r4);

    //std::cout<<"bn_t = ";bn_print(a);
    //std::cout<<"block_low= "<<b1<<endl;
    //std::cout<<"block_high= "<<b2<<endl;
}
void block_2_bn_128(block b, bn_t &a){
    //std::cout<<"block= "<<b<<endl;
    uint8_t buffer[16];
    memset(buffer,0,sizeof(buffer));
    uint64_t r1 = b[1];
    uint64_t r2 = b[0];
    int buffer_index = 15;
    for (int i = 8; i < 16; i++) {
        uint8_t value = (r2 >> ((i - 8) * 8)) & 0xFF;
        if (value != 0 ) {
            buffer[15 - (i - 8)] = value;
            buffer_index--;
        }
    }
    for (int i = 0; i < 8; i++) {
        uint8_t value = (r1 >> (i * 8)) & 0xFF;
        if (value != 0 ) {
            buffer[7 - i] = value;
            buffer_index--;
        }
    }
    bn_read_bin(a, buffer, 16);

    //std::cout<<"bn_t = ";bn_print(a);
}
//party, io, fa, fb
void BTGen(int party, NetIO *io, block &aa, block &bb, block &cch, block &ccl){
    //block fc;

    int bits = 64;
    block BKERR = makeBlock(0, 1);
    cch = makeBlock(0, 0);
    ccl = makeBlock(0, 0);
    if (core_init() != RLC_OK) {
        core_clean();
        //return BKERR;
    }
    bn_t fa, fb, c, pub, Efa, Efb, t;
    phpe_t prv;
    bn_t p2b;
    char str[RLC_BN_SIZE + 1];  // RLC_BN_SIZE是大数的最大长度
    int len = RLC_BN_SIZE;
    bn_null(fa);
    bn_null(fb);
    bn_null(c);
    bn_null(pub);
    bn_null(Efa);
    bn_null(Efb);
    bn_null(t);
    bn_null(p2b);
    phpe_null(prv);
    bn_new(fa);
    bn_new(fb);
    bn_new(c);
    bn_new(pub);
    bn_new(Efa);
    bn_new(Efb);
    bn_new(t);
    bn_new(p2b);

    phpe_new(prv);
    bn_set_2b(p2b, bits);

    // Paillier setup is owned by ALICE. BOB receives the public key below and
    // must not generate an unrelated second keypair.
    if (party == ALICE) {
        cp_phpe_gen(pub, prv, 3072);
    }

    //std::cout<<"pub = ";bn_write_str(str, len, pub, 10);  printf("%s\n", str);//bn_print(pub);
    //std::cout<<"aa"<<std::endl;
    //auto end = std::chrono::system_clock::now();
    //auto dura = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    //std::cout << "Generate time: " << double(dura.count()) * std::chrono::milliseconds::period::num / std::chrono::milliseconds::period::den << "s" << std::endl;

    bn_rand_mod(fa, p2b);
    bn_rand_mod(fb, p2b);
    //std::cout<<"fa = ";bn_write_str(str, len, fa, 10);  printf("%s\n", str); //bn_print(fa);
    //std::cout<<"fb = ";bn_write_str(str, len, fb, 10);  printf("%s\n", str);//bn_print(fb);

    int BTtimes = 1;
    auto start = std::chrono::system_clock::now();
    for(int bti = 0; bti < BTtimes; bti++) {
        if (party == ALICE) {

            //encryption of a0 and b0
            //start = std::chrono::system_clock::now();
            cp_phpe_enc(Efa, fa, pub);
            //std::cout<<"Efa = ";bn_write_str(str, len, Efa, 10);  printf("%s\n", str);//bn_print(Efa);
            //end = std::chrono::system_clock::now();
            //dura = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            //std::cout << "Enc time: " << double(dura.count()) * std::chrono::milliseconds::period::num / std::chrono::milliseconds::period::den << "s" << std::endl;

            cp_phpe_enc(Efb, fb, pub);
            //std::cout<<"Efb = ";bn_write_str(str, len, Efb, 10);  printf("%s\n", str);//bn_print(Efb);
            cp_phpe_dec(fb, Efb, prv);
            //std::cout<<"Dfb = ";bn_write_str(str, len, fb, 10);  printf("%s\n", str);//bn_print(fb);

            //1.P0将Enc(a)和Enc(b)发给P1; and pub key
            send_bn(io, Efa);
            send_bn(io, Efb);
            send_bn(io, pub);



            /*bn_t Ev, v, Efa_fb, Efb_fa, Efa_fb_Efb_fa, E_r;
            bn_new(Ev);
            bn_new(v);
            bn_new(Efa_fb);
            bn_new(Efb_fa);
            bn_new(Efa_fb_Efb_fa);
            bn_new(E_r);

            io->recv_data(&Efa_fb, sizeof(bn_t));
            std::cout<<"Efa_fb = ";bn_write_str(str, len, Efa_fb, 10);  printf("%s\n", str);//bn_print(Efa_fb);
            cp_phpe_dec(Efa_fb, Efa_fb, prv);
            std::cout<<"Dfa_fb = ";bn_write_str(str, len, Efa_fb, 10);  printf("%s\n", str);//bn_print(Efa_fb);
            bn_clean(Efa_fb);

            io->recv_data(&Efb_fa, sizeof(bn_t));
            std::cout<<"Efb_fa = ";bn_write_str(str, len, Efb_fa, 10);  printf("%s\n", str);//bn_print(Efb_fa);
            cp_phpe_dec(Efb_fa, Efb_fa, prv);
            std::cout<<"Dfb_fa = ";bn_write_str(str, len, Efb_fa, 10);  printf("%s\n", str);//bn_print(Efb_fa);
            bn_clean(Efb_fa);

            io->recv_data(&Efa_fb_Efb_fa, sizeof(bn_t));
            std::cout<<"Efa_fb_Efb_fa = ";bn_write_str(str, len, Efa_fb_Efb_fa, 10);  printf("%s\n", str);//bn_print(Efa_fb_Efb_fa);
            cp_phpe_dec(Efa_fb_Efb_fa, Efa_fb_Efb_fa, prv);
            std::cout<<"Dfa_fb_Efb_fa = ";bn_write_str(str, len, Efa_fb_Efb_fa, 10);  printf("%s\n", str);//bn_print(Efa_fb_Efb_fa);
            bn_clean(Efa_fb_Efb_fa);

            io->recv_data(&E_r, sizeof(bn_t));
            std::cout<<"E_r = ";bn_write_str(str, len, E_r, 10);  printf("%s\n", str);//bn_print(E_r);
            cp_phpe_dec(E_r, E_r, prv);
            std::cout<<"D_r = ";bn_write_str(str, len, E_r, 10);  printf("%s\n", str);//bn_print(E_r);
            bn_clean(E_r);*/



            //3. P0计算c0=a0b0+dec(v) = a0b0+a0b1+b0a1+r;
            bn_t Ev, v;
            bn_null(Ev);
            bn_null(v);
            bn_new(Ev);
            bn_new(v);
            recv_bn(io, Ev);
            //std::cout<<"Ev = ";bn_write_str(str, len, Ev, 10);  printf("%s\n", str);//bn_print(Ev);
            //start = std::chrono::system_clock::now();
            cp_phpe_dec(v, Ev, prv);
            //std::cout<<"v = ";bn_write_str(str, len, v, 10);  printf("%s\n", str);//bn_print(v);
            //end = std::chrono::system_clock::now();
            //dura = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            //std::cout << "Dec time: " << double(dura.count()) * std::chrono::milliseconds::period::num / std::chrono::milliseconds::period::den << "s" << std::endl;

            bn_mul(t, fa, fb);
            //std::cout<<"t = fa * fb =";bn_write_str(str, len, t, 10);  printf("%s\n", str);//bn_print(t);

            bn_mod(t, t, pub);
            //std::cout<<"a0b0 = t mod pub =";bn_write_str(str, len, t, 10);  printf("%s\n", str);//bn_print(t);

            bn_add(t, v, t);
            bn_mod(t, t, pub);
            //std::cout<<"fc = a0b0 + v =";bn_write_str(str, len, t, 10);  printf("%s\n", str);//bn_print(t);

            bn_clean(Ev);
            bn_clean(v);
        } else {
            //1.P0将Enc(a)和Enc(b)发给P1; and pub key
            recv_bn(io, Efa);
            recv_bn(io, Efb);

            recv_bn(io, pub);


            //std::cout<<"Efa = ";bn_write_str(str, len, Efa, 10);  printf("%s\n", str);//bn_print(Efa);
            //std::cout<<"Efb = ";bn_write_str(str, len, Efb, 10);  printf("%s\n", str);//bn_print(Efb);
            bn_t r, t1, t2, v, n2;
            bn_null(r);
            bn_null(t1);
            bn_null(t2);
            bn_null(v);
            bn_null(n2);
            bn_new(r);
            bn_new(t1);
            bn_new(t2);
            bn_new(v);
            bn_new(n2);

            bn_mul(n2, pub, pub);
            //std::cout<<"n^2 =";bn_write_str(str, len, n2, 10);  printf("%s\n", str);//bn_print(n2);

            //c1 = a1*b1 - r
            bn_mul(t, fa, fb);
            //std::cout<<"a1 * b1 =";bn_write_str(str, len, t, 10);  printf("%s\n", str);//bn_print(t);
            bn_mod(t, t, pub);
            //std::cout<<"a1 * b1 mod pub =";bn_write_str(str, len, t, 10);  printf("%s\n", str);//bn_print(t);
            bn_rand_mod(r, p2b);

            //std::cout<<"r = ";bn_write_str(str, len, r, 10);  printf("%s\n", str);//bn_print(r);
            bn_sub(t, t, r);
            bn_mod(t, t, pub);
            //std::cout<<"fc = a1 * b1 - r =";bn_write_str(str, len, t, 10);  printf("%s\n", str);//bn_print(t);

            //2. P1:生成一个随机数r,计算v=Enc(a0)^b1×Enc(b0)^a1×r,然后将v发给P0;
            dig_t dexp;

            bn_mxp(t1, Efa, fb, n2);
            //io->send_data(&t1, sizeof(bn_t));
            //std::cout<<"t1=Efa^fb, where fb = ";bn_write_str(str, len, fb, 10);  printf("%s\n", str);

            bn_mxp(t2, Efb, fa, n2);
            //io->send_data(&t2, sizeof(bn_t));
            //std::cout<<"t2=Efb^fa, where fa = ";bn_write_str(str, len, fa, 10);  printf("%s\n", str);

            cp_phpe_add(v, t2, t1, pub);
            //std::cout<<"t1 = ";        bn_write_str(str,len,t1,10);    printf("%s\n",str);
            //std::cout<<"t2 = ";        bn_write_str(str,len,t2,10);    printf("%s\n",str);
            //std::cout<<"v = t1 * t2 =";bn_write_str(str, len, v, 10);  printf("%s\n", str);//bn_print(v);
            //io->send_data(&v, sizeof(bn_t));
            // Keep plaintext randomness and ciphertext in distinct BN objects;
            // the pinned RELIC Paillier API does not guarantee alias-safe enc.
            bn_t r_plain, r_enc;
            bn_null(r_plain); bn_null(r_enc);
            bn_new(r_plain); bn_new(r_enc);
            bn_copy(r_plain, r);
            cp_phpe_enc(r_enc, r_plain, pub);
            //std::cout<<"r = ";bn_write_str(str, len, r, 10);  printf("%s\n", str);//bn_print(r);
            //io->send_data(&r, sizeof(bn_t));
            bn_t v_sum;
            bn_null(v_sum); bn_new(v_sum);
            cp_phpe_add(v_sum, v, r_enc, pub);
            bn_copy(v, v_sum);
            //std::cout<<"v = v * r =";bn_write_str(str, len, v, 10);  printf("%s\n", str);//bn_print(v);

            send_bn(io, v);

            bn_clean(r_plain);
            bn_clean(r_enc);
            bn_clean(v_sum);
            bn_clean(r);
            bn_clean(t1);
            bn_clean(t2);
            bn_clean(v);
        }
    }
    std::cout<<"BTGen "<<BTtimes<<" times needs ";
    auto end = std::chrono::system_clock::now();
    auto dura = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << double(dura.count()) * std::chrono::milliseconds::period::num / std::chrono::milliseconds::period::den << "s" << std::endl;

    //bn_2_block_256(t, cc1, cc2);

    bn_2_block_128(fa, aa);
    //std::cout<<endl;
    //block_2_bn_128(aa, fa);
    //std::cout<<endl;
    bn_2_block_128(fb, bb);
    //std::cout<<endl;
    //block_2_bn_128(bb, fb);
    //std::cout<<endl;
    bn_2_block_256(t, cch, ccl);
    //std::cout<<endl;
    //bn_2_block_128(fb, bb);


    if(party == ALICE){
        send_bn(io, fa);
        send_bn(io, fb);
        send_bn(io, t);
        //std::cout<<"a0 = ";bn_print(fa);
        //std::cout<<"b0 = ";bn_print(fb);
        //std::cout<<"c0 = ";bn_print(t);
    }else{
        bn_t a0, b0, c0, cc;
        bn_null(a0);
        bn_null(b0);
        bn_null(c0);
        bn_null(cc);
        bn_new(a0);
        bn_new(b0);
        bn_new(c0);
        bn_new(cc);
        recv_bn(io, a0);
        recv_bn(io, b0);
        recv_bn(io, c0);
        //std::cout<<"a1 = ";bn_print(fa);
        //std::cout<<"b1 = ";bn_print(fb);
        //std::cout<<"c1 = ";bn_print(t);

        bn_add(a0,a0,fa);
        bn_add(b0,b0,fb);
        bn_add(c0, c0, t);
        bn_mul(cc, a0, b0);
        //if(bn_cmp(cc, c0) == RLC_EQ)      std::cout<<"------------- BTGen Succeed! -----------"<<endl<<endl;
        //else std::cout<<"a*b != c+c"<<endl;

        bn_clean(a0);
        bn_clean(b0);
        bn_clean(c0);
        bn_clean(cc);
    }
    bn_clean(fa);
    bn_clean(fb);
    bn_clean(c);
    bn_clean(pub);
    bn_clean(Efa);
    bn_clean(Efb);
    bn_clean(t);
    bn_clean(p2b);
    phpe_free(prv);
    //return fc;
}



block Mult(int party,NetIO *io,uint64_t N,block x,block y,block a,block b,block ch,block cl){
    block D,E,Di,Ei,Zl,Zh,Zli,Zhi, out;
    D = x-a;
    E = y-b;
    if(party==ALICE) {
        io->send_data(&D, sizeof(block));
        io->send_data(&E, sizeof(block));

        io->recv_data(&Di, sizeof(block));
        io->recv_data(&Ei, sizeof(block));
    }else{
        io->recv_data(&Di, sizeof(block));
        io->recv_data(&Ei, sizeof(block));

        io->send_data(&D, sizeof(block));
        io->send_data(&E, sizeof(block));
    }
    D+=Di;
    E+=Ei;
    Zl = D * b + a * E + cl;
    if(party==ALICE) {
        io->send_data(&Zl, sizeof(block));
        io->recv_data(&Zli, sizeof(block));
    }
    else{
        io->recv_data(&Zli, sizeof(block));
        io->send_data(&Zl, sizeof(block));
    }
    return out = D*E + Zl + Zli;
}

void CPRS(int party, NetIO *io, uint64_t N, block *v, block ran,
          block fa, block fb, block fch, block fcl,
          std::vector<uint64_t>& nonzero_indices,
          std::vector<uint64_t>& compressed_shares) {
    //block r= makeBlock(0, ran_i);
    //vector<uint64_t> sig;
    std::vector<block> xr(N);
    block xr_other;
    for(int i = 0 ;i<N;i++){
        xr[i] = Mult(party, io, N, v[i], ran, fa, fb, fch, fcl);
        if(party==ALICE) {
            io->send_data(&xr[i], sizeof(block));
            io->recv_data(&xr_other, sizeof(block));
        } else {
            io->recv_data(&xr_other, sizeof(block));
            io->send_data(&xr[i], sizeof(block));
        }
        block opened = xr[i] + xr_other;
        if(_mm_extract_epi64(opened, 0)!=0) {
            nonzero_indices.push_back(i);
            compressed_shares.push_back(
                static_cast<uint64_t>(_mm_extract_epi64(v[i], 0)) %
                UINT64_C(2305843009213693951));
        }
    }
}

int testb(){
    return 0;
}

int testa(void) {
    int code = RLC_ERR;
    bn_t a, b, c, pub;
    phpe_t prv;
    int result;

    if (core_init() != RLC_OK) {
        core_clean();
        //return BKERR;
    }
    bn_null(a);
    bn_null(b);
    bn_null(c);
    bn_null(pub);
    phpe_null(prv);
    bn_new(a);
    bn_new(b);
    bn_new(c);
    bn_new(pub);
    phpe_new(prv);
    cp_phpe_gen(pub, prv, 7);
    bn_rand_mod(a, pub);
    bn_t fa, fb, Efa, Efb,Dfa, Dfb;
    bn_null(fa);
    bn_null(fb);
    bn_null(Efa);
    bn_null(Efb);
    bn_null(Dfa);
    bn_null(Dfb);
    bn_new(fa);
    bn_new(fb);
    bn_new(Efa);
    bn_new(Efb);
    bn_new(Dfa);
    bn_new(Dfb);

    std::cout<<"a = ";bn_print(a);
    cp_phpe_enc(c, a, pub);
    std::cout<<"c = ";bn_print(c);

    auto start = std::chrono::system_clock::now();
    for(int i=0;i<4000;i++)
    cp_phpe_dec(b, c, prv);

    auto end = std::chrono::system_clock::now();
    auto dura_cs = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "phpe_dec: " << double(dura_cs.count())  << "us" << std::endl;
    //std::cout<<"b = ";bn_print(b);


/*    do {
        bn_rand(fa, RLC_POS, 16);
    } while (bn_is_zero(fa));
    bn_rand_mod(fa, pub);
    std::cout<<"fa = ";bn_print(fa);
    cp_phpe_enc(Efa, fa, pub);
    std::cout<<"Efa = ";bn_print(Efa);
    cp_phpe_dec(Dfa, Efa, prv);
    std::cout<<"Dfa = ";bn_print(Dfa);*/
/*

    do {
        bn_rand(fb, RLC_POS, 16);
    } while (bn_is_zero(fb));
    //bn_rand_mod(fb, pub);
    std::cout<<"fb = ";bn_print(fb);
    cp_phpe_enc(Efb, fb, pub);
    std::cout<<"Efb = ";bn_print(Efb);
    cp_phpe_dec(Dfb, Efb, prv);
    std::cout<<"Dfb = ";bn_print(Dfb);*/

    bn_mxp(a, a, b, c);
    std::cout<<"Dfa = ";bn_print(a);

    bn_clean(a);
    bn_clean(b);
    bn_clean(c);
    bn_clean(pub);

    bn_clean(fa);
    bn_clean(fb);
    bn_clean(Efa);
    bn_clean(Efb);
    bn_clean(Dfa);
    bn_clean(Dfb);
    phpe_free(prv);
    return code;
}
