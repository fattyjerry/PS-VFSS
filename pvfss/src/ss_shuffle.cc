#include "ss_shuffle.h"

#include <algorithm>
#include <vector>

namespace {
int CappedSuffixLen(int raw_suffix_len, int n, int t) {
  return std::min(raw_suffix_len, n - t);
}

uint64_t LowMask(int bits) {
  return bits == 0 ? 0 : ((1ULL << bits) - 1);
}
}

/**
 * @param perm: permutation information
 * @param length: length of permutation
 * @param party: party
 * @param io: io
 * @param out: the pointer of output value.
 */
void ShareTranslation(uint64_t *perm, int length, int party, HighSpeedNetIO *io,
                      block *a, block *b, block *delta) {
  // alice has two outputs, a and b, while bob has only one, delta.
  std::vector<block> v(static_cast<size_t>(length) * length);
  std::vector<block *> seeds(length);

  if (party == ALICE) {
    // ALICE gets the whole matrix and generate vector a and b
    memset(a, 0, length * sizeof(block));
    memset(b, 0, length * sizeof(block));

    for (int i = 0; i < length; i++) {
      OblivSetup(length, -1, party, io, seeds.data() + i);
      Expand(length, -1, seeds[i], party, v.data() + static_cast<size_t>(i) * length);
    }

    for (int i = 0; i < length; i++) {
      for (int j = 0; j < length; j++) {
        a[i] += v[static_cast<size_t>(j) * length + i];
        b[i] += v[static_cast<size_t>(i) * length + j];
      }
    }

  } else {
    memset(delta, 0, length * sizeof(block));

    for (int i = 0; i < length; i++) {
      OblivSetup(length, perm[i], party, io, seeds.data() + i);
      Expand(length, perm[i], seeds[i], party, v.data() + static_cast<size_t>(i) * length);
    }

    for (int i = 0; i < length; i++) {
      for (int j = 0; j < length; j++) {
        delta[i] += (v[static_cast<size_t>(i) * length + j] -
                     v[static_cast<size_t>(j) * length + perm[i]]);
      }
    }
  }
}

template<typename T>
T *LocateVector(T *vecs, int x, int y, int z, int i, int j) {
  return vecs + i * y * z + j * z;
}

void SubPerm(uint64_t T, int t,
             int prefix_len, uint64_t prefix, 
             int suffix_len, uint64_t suffix, 
             uint64_t *perm, uint64_t *v) {

  std::vector<uint64_t> tmp(T);
  uint64_t index;
  for (int i = 0; i < T; i++) {
    index = (prefix << (t + suffix_len)) | (perm[i] << suffix_len) | suffix;
    tmp[i] = v[index];
  }

  for (int i = 0; i < T; i++) {
    index = (prefix << (t + suffix_len)) | (i << suffix_len) | suffix;
    v[index] = tmp[i];
  }
}

template<typename T>
void SimplePerm(uint64_t *perm, uint64_t length, T *v) {
  std::vector<T> tmp(length);
  
  for (int i = 0; i < length; i++) {
    tmp[i] = v[perm[i]];
  }

  memcpy(v, tmp.data(), length * sizeof(T));
}

void PermReconstruct(int d, uint64_t N, uint64_t T, int n, int t, uint64_t *perms, uint64_t *perm) {
  int prefix_len = 0;
  int suffix_len = n - t;
  int subperm_num = N / T;
  uint64_t *subperm;
  uint64_t index, prefix, suffix, counter;

  if (d == 1) {
    for (int j = 0; j < subperm_num; j++) {
      subperm = LocateVector<uint64_t>(perms, d, subperm_num, T, 0, j);
      for (uint64_t i = 0; i < T; i++) {
        perm[static_cast<uint64_t>(j) * T + i] = static_cast<uint64_t>(j) * T + subperm[i];
      }
    }
    return;
  }

  // initial perm
  for (int i = 0; i < N; i++) {
    perm[i] = i;
  }
  // process first d / 2 layers
  for (int i = 0; i < d / 2; i++) { 
    counter = 0;
    for (int j = 0; j < subperm_num; j++) {
      prefix = counter >> suffix_len;
      suffix = counter & LowMask(suffix_len);
      subperm = LocateVector<uint64_t>(perms, d, subperm_num, T, i, j);
      SubPerm(T, t, prefix_len, prefix, suffix_len, suffix, subperm, perm);
      counter++;
    }
    prefix_len += t;
    suffix_len -= t;
  }
  
  // process middle layer
  counter = 0;
  for (int j = 0; j < subperm_num; j++) {
    prefix = counter >> suffix_len;
    suffix = counter & LowMask(suffix_len);
    subperm = LocateVector<uint64_t>(perms, d, subperm_num, T, d / 2, j);
    SubPerm(T, t, prefix_len, prefix, suffix_len, suffix, subperm, perm);
    counter++;
  }

  prefix_len -= t;
  suffix_len += t;

  // process the last d / 2 layers
  for (int i = d / 2 + 1; i < d; i++) {
    counter = 0;
    int eff_suffix_len = CappedSuffixLen(suffix_len, n, t);
    for (int j = 0; j < subperm_num; j++) {
      prefix = counter >> eff_suffix_len;
      suffix = counter & LowMask(eff_suffix_len);
      subperm = LocateVector<uint64_t>(perms, d, subperm_num, T, i, j);
      SubPerm(T, t, prefix_len, prefix, eff_suffix_len, suffix, subperm, perm);
      counter++;
    }
    prefix_len -= t;
    suffix_len += t;
  }

}

void SubPermReconstruct(int layer, int d, uint64_t N, uint64_t T, int n, int t, uint64_t *perms, uint64_t *perm) {
  int prefix_len, suffix_len, prefix, suffix, index;
  int counter = 0;
  int subperm_num = N / T;
  if (layer < d / 2) {
    prefix_len = layer * t;
    suffix_len = n - (layer + 1) * t;
  } else if (layer == d / 2) {
    prefix_len = n - t;
    suffix_len = 0;
  } else {
    suffix_len = CappedSuffixLen((layer - d / 2) * t, n, t);
    prefix_len = n - suffix_len - t;
  }

  // initial perm
  for (int i = 0; i < N; i++) {
    perm[i] = i;
  }

  counter = 0;
  for (int i = 0; i < subperm_num; i++) {
    prefix = counter >> suffix_len;
    suffix = counter & LowMask(suffix_len);
    SubPerm(T, t, prefix_len, prefix, suffix_len, suffix, perms + i * T, perm);
    counter++;
  }
}

void Reallocate(block *v, int layer, int d, uint64_t N, uint64_t T, int n, int t) {
  int prefix_len, suffix_len, prefix, suffix, index;
  int counter = 0;
  if (layer < d / 2) {
    prefix_len = layer * t;
    suffix_len = n - (layer + 1) * t;
  } else if (layer == d / 2) {
    prefix_len = n - t;
    suffix_len = 0;
  } else {
    suffix_len = CappedSuffixLen((layer - d / 2) * t, n, t);
    prefix_len = n - suffix_len - t;
  }

  std::vector<block> tmp(N);
  for (int i = 0; i < N; i++) {
    prefix = counter >> suffix_len;
    suffix = counter & LowMask(suffix_len);
    index = (prefix << (suffix_len + t)) 
          | ((i % T) << suffix_len)
          | suffix;
    tmp[index] = v[i];
    if ((i + 1) % T == 0) counter++; 
  }

  memcpy(v, tmp.data(), N * sizeof(block));
}

void Offline(uint64_t N, uint64_t T, uint64_t *perms, uint64_t party, HighSpeedNetIO *io,
             uint64_t *perm, block *a, block * b, block *delta) {
  int n = (int)(log2(N));
  int t = (int)(log2(T));
  int d = 2 * (int)ceil(n / t) - 1;
  int subperm_num = N / T;
  uint64_t *subperm;
  std::vector<uint64_t> subperms(static_cast<size_t>(d) * N);

  std::vector<block> as(static_cast<size_t>(d) * N);
  std::vector<block> bs(static_cast<size_t>(d) * N);
  std::vector<block> deltas(static_cast<size_t>(d) * N);
  std::vector<block> offset(static_cast<size_t>(std::max(d - 1, 0)) * N);
  std::vector<block> offset_(static_cast<size_t>(std::max(d - 1, 0)) * N);

  PermReconstruct(d, N, T, n, t, perms, perm);
  if (party == ALICE) {
    for (int i = 0; i < d; i++) {
      for (int j = 0; j < subperm_num; j++) {
        ShareTranslation(nullptr, T, ALICE, io,
                        LocateVector<block>(as.data(), d, subperm_num, T, i, j), 
                        LocateVector<block>(bs.data(), d, subperm_num, T, i, j), 
                        nullptr);
              
        subperm = LocateVector<uint64_t>(perms, d, subperm_num, T, i, j);
        ShareTranslation(subperm, T, BOB, io,
                        nullptr, nullptr, 
                        LocateVector<block>(deltas.data(), d, subperm_num, T, i, j));
      }
    }
  } else {
    for (int i = 0; i < d; i++) {
      for (int j = 0; j < subperm_num; j++) {
        subperm = LocateVector<uint64_t>(perms, d, subperm_num, T, i, j);
        ShareTranslation(subperm, T, BOB, io,
                        nullptr, nullptr, 
                        LocateVector<block>(deltas.data(), d, subperm_num, T, i, j));
        ShareTranslation(nullptr, T, ALICE, io,
                        LocateVector<block>(as.data(), d, subperm_num, T, i, j), 
                        LocateVector<block>(bs.data(), d, subperm_num, T, i, j), 
                        nullptr);
      }
    }
  }
  std::cout<<"offline done"<<endl;
  if (d == 1) {
    for (int i = 0; i < N; i++) {
      a[i] = as[i];
      b[i] = bs[i];
      delta[i] = deltas[i];
    }
    return;
  }

  // reallocate a and b
  for (int i = 0; i < d; i++) {
    Reallocate(as.data() + i * N, i, d, N, T, n, t);
    Reallocate(bs.data() + i * N, i, d, N, T, n, t);
    Reallocate(deltas.data() + i * N, i, d, N, T, n, t);
    SubPermReconstruct(i, d, N, T, n, t,
                       perms + i * N, 
                       subperms.data() + i * N);
  }

  // calculate delta = a ^ {i + 1} - b^i
  for (int i = 0; i < (d - 1) * N; i++) {
    offset[i] = as[N + i] - bs[i];
  }

  // obtain the final delta
  if ((d - 1) * N > 0) {
    if (party == ALICE) {
      io->send_block(offset.data(), (d - 1) * N);
      io->recv_block(offset_.data(), (d - 1) * N);
    } else {
      io->recv_block(offset_.data(), (d - 1) * N);
      io->send_block(offset.data(), (d - 1) * N);
    }
  }
  
  for (int i = 0; i < N; i++) {
    delta[i] = d > 1 ? deltas[i] + offset_[i] : deltas[i];
  }

  for (int i = N; i < (d - 1) * N; i += N) {
    SimplePerm<block>(subperms.data() + i, N, delta);

    for (int j = 0; j < N; j++) {
      delta[j] += (deltas[i + j] + offset_[i + j]);
    }
  }

  SimplePerm<block>(subperms.data() + (d - 1) * N, N, delta);
  for (int i = 0; i < N; i++) {
    a[i] = as[i];
    b[i] = bs[(d - 1) * N + i];
    delta[i] += deltas[(d - 1) * N + i];
  }
}

void PermuteShare(uint64_t N, uint64_t T, 
                  uint64_t *perm, block *delta,
                  block *x, block *a, block *b,
                  uint64_t party, HighSpeedNetIO *io, 
                  block *out) {
  // implementation of single round permute+share
  int n = (int)(log2(N));
  int t = (int)(log2(T));
  int d = 2 * (int)ceil(n / t) - 1;

  // suppose T|N

  std::vector<block> m(N);
  std::vector<block> w(N);
  //std::cout<<"here"<<endl;
  if (party == ALICE) {
    // calculate x + a^1
    for (int i = 0; i < N; i++) {
      m[i] = x[i] + a[i];
    }

    io->send_block(m.data(), N);
    io->recv_block(w.data(), N);

    for (int i = 0; i < N; i++) {
      out[i] = w[i] - b[i];
    }
  } else {
    // already computes the subpermutation, stored in perms
    PRG prg;
    prg.random_block(w.data(), N);
    io->recv_block(m.data(), N);
    io->send_block(w.data(), N);

    SimplePerm(perm, N, m.data());
    for (int i = 0; i < N; i++) {
      out[i] = m[i] + delta[i] - w[i];
    }
  }
}

void SecretSharedShuffle(uint64_t N, uint64_t T, uint64_t party, HighSpeedNetIO *io, 
                         block *x, uint64_t *perm, block *delta, block *a, block *b,
                         block *out) {
  std::vector<block> out0(N);

  int n = (int)(log2(N));
  int t = (int)(log2(T));
  int d = 2 * (int)ceil(n / t) - 1;
  int subperm_num = N / T;

  if (d == 1) {
    PermuteShare(N, T, perm, delta, x, a, b, party, io, out);
    return;
  }

  if (party == ALICE) {
    PermuteShare(N, T, nullptr, nullptr, x, a, b, ALICE, io, out0.data());
    PermuteShare(N, T, perm, delta, nullptr, nullptr, nullptr, BOB, io, out);

    SimplePerm(perm, N, out0.data());
    for (int i = 0; i < N; i++) {
      out[i] += out0[i];
    }
  } else { // bob
    PermuteShare(N, T, perm, delta, nullptr, nullptr, nullptr, BOB, io, out0.data());
    SimplePerm<block>(perm, N, x);
    for (int i = 0; i < N; i++) {
      x[i] += out0[i];
    }
    PermuteShare(N, T, nullptr, nullptr, x, a, b, ALICE, io, out);
  }

}
