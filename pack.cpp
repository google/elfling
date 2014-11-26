// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// elfling - a linking compressor for ELF files by Minas ^ Calodox

#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int cmax = 0;

#define CONTEXT_COUNT 8

#define GENOME_SIZE 48
#define GENOME_ITERATIONS 100

#define MAX_WEIGHT 60

int FromHexDigit(char d) {
  if (d >= '0' && d <= '9') return d - '0';
  if (d >= 'A' && d <= 'F') return d - 'A' + 10;
  if (d >= 'a' && d <= 'f') return d - 'a' + 10;
  return -1;
}

int FromHex2(const char* str) {
  int d0 = FromHexDigit(str[0]);
  int d1 = FromHexDigit(str[1]);
  if (d0 < 0 || d1 < 0) return -1;
  return d0 * 16 + d1;
}

void ToHex2(int v, char* dst) {
  const char* hd = "0123456789abcdef";
  dst[0] = hd[(v >> 4) & 0x0f];
  dst[1] = hd[v & 0x0f];
}

bool CompressionParameters::FromString(const char* str) {
  int l = strlen(str);
  if (l < 10) return false;  // Expect at least two contexts.
  contextCount = FromHex2(str);
  if (contextCount < 2 || contextCount > MAX_CONTEXT_COUNT) return false;
  if (l != 2 + 4 * contextCount) return false;
  for (int i = 0; i < contextCount; ++i) {
    weights[i] = FromHex2(str + 2 + 4 * i);
    contexts[i] = FromHex2(str + 4 + 4 * i);
  }
  return true;
}

void CompressionParameters::ToString(char* str) {
  ToHex2(contextCount, str);
  for (int i = 0; i < contextCount; ++i) {
    ToHex2(weights[i], str + 2 + 4 * i);
    ToHex2(contexts[i], str + 4 + 4 * i);
  }
  str[2 + 4 * contextCount] = 0;
}

struct Context {
  u8 ctx;
  int bs;
  int bw;
};

static int CompareContext(const Context* a, const Context* b) {
  return a->bs - b->bs;
}

struct Genome {
  CompressionParameters params;
  int fitness;
};

static int CompareGenome(const Genome* a, const Genome* b) {
  if (a->fitness != b->fitness) {
    return a->fitness - b->fitness;
  }
  for (int i = 0; i < CONTEXT_COUNT; ++i) {
    if (a->params.weights[i] != b->params.weights[i])
      return a->params.weights[i] - b->params.weights[i];
    if (a->params.contexts[i] != b->params.contexts[i])
      return a->params.contexts[i] - b->params.contexts[i];
  }
  return 0;
}

bool Compressor::Compress(CompressionParameters* params, void* in, int inLen, void* out, int* outLen) {
  // Test all context patterns individually to figure out which ones are most
  // likely to produce good results for seeding our initial set.
  Context pats[128];
  u32 pc = 0;
  int orgOutLen = *outLen;
  for (u32 i = 3; i < 256; i += 2) {
    u32 bc = 0;
    for (u8 b = 0; b < 8; ++b) {
      if (i & (1 << b)) ++bc;
    }
    if (bc > 4) continue;  // Only keep patterns with 4 bytes at most.
    pats[pc].ctx = i;
    pats[pc].bs = *outLen;
    pats[pc].bw = 0;
    CompressionParameters c;
    c.contextCount = 2;
    c.weights[0] = 8;
    c.weights[1] = 1;
    c.contexts[0] = pats[pc].ctx;
    c.contexts[1] = 1;
    CompressSingle(&c, in, inLen, out, &pats[pc].bs);
    ++pc;
  }
  qsort(pats, pc, sizeof(Context), (__compar_fn_t)CompareContext);
  if (verbose_) {
    for (int i = 0; i < pc; ++i) {
      printf("Pattern %2d [%2.2x] = %d bytes @ %d\n", i, pats[i].ctx, pats[i].bs, pats[i].bw);
    }  
  }

  Genome* g = new Genome[GENOME_SIZE];
  for (int i = 0; i < GENOME_SIZE; ++i) {
    g[i].params.contextCount = CONTEXT_COUNT;
    g[i].params.contexts[0] = 1;
    g[i].params.weights[0] = 1;
    for (int j = 1; j < CONTEXT_COUNT; ++j) {
      if (i == 0) {
        g[i].params.contexts[j] = pats[j - 1].ctx;
        g[i].params.weights[j] = 20;
      } else {
        g[i].params.contexts[j] = pats[rand() % (pc / 4)].ctx;
        g[i].params.weights[j] = rand() % MAX_WEIGHT + 1;
      }
    }
  }
  if (params->contextCount) {
    g[1].params = *params;
  }
  for (int i = 0; i < GENOME_ITERATIONS; ++i) {
    for (int j = 0; j < GENOME_SIZE; ++j) {
      g[j].fitness = *outLen;
      CompressSingle(&g[j].params, in, inLen, out, &g[j].fitness);
    }
    qsort(g, GENOME_SIZE, sizeof(Genome), (__compar_fn_t)CompareGenome);
    for (int j = 0; j < GENOME_SIZE; ++j) {
      if (j >= 3) break;
      printf("I[%3d,%d]: %d", i, j, g[j].fitness);
      for (int i = 0; i < g[j].params.contextCount; ++i) {
        printf(" %2d*%2.2x", g[j].params.weights[i], g[j].params.contexts[i]);
      }
      printf("\n");
    }
    *params = g[0].params;
    srand(time(nullptr));
    int keep = GENOME_SIZE / 4;
    for (int j = 0; j < GENOME_SIZE; ++j) {
      g[j].fitness = 0;
    }
    for (int j = keep; j < GENOME_SIZE / 2; j += 2) {
      int m1 = rand() % keep;
      int m2 = rand() % keep;
      while (m2 == m1) { m2 = rand() % keep; }
      int cb = rand() % (CONTEXT_COUNT * 2);
      bool equals = true;
      for (int k = 0; k < 2 * CONTEXT_COUNT; ++k) {
        u8* trg1 = (k & 1) ? g[j].params.contexts : g[j].params.weights;
        u8* trg2 = (k & 1) ? g[j + 1].params.contexts : g[j + 1].params.weights;
        u8* src1 = (k & 1) ? g[m1].params.contexts : g[m1].params.weights;
        u8* src2 = (k & 1) ? g[m2].params.contexts : g[m2].params.weights;
        if (k >= cb) {
          u8* tmp = src1;
          src1 = src2;
          src2 = tmp;
        }
        trg1[k >> 1] = src1[k >> 1];
        trg2[k >> 1] = src2[k >> 1];
      }
    }
    qsort(g, GENOME_SIZE / 2, sizeof(Genome), (__compar_fn_t)CompareGenome);
    for (int j = 1; j < GENOME_SIZE / 2; ++j) {
      if (!memcmp(g[j].params.weights, g[j - 1].params.weights, CONTEXT_COUNT) && !memcmp(g[j].params.contexts, g[j - 1].params.contexts, CONTEXT_COUNT)) {
        int byte = rand() % (2 * CONTEXT_COUNT);
        if (byte < CONTEXT_COUNT) {
          g[j - 1].params.contexts[byte] = pats[rand() % pc].ctx;
        } else {
          g[j - 1].params.weights[byte - CONTEXT_COUNT] = rand() % MAX_WEIGHT + 1;
        }
      }
    }
    for (int j = GENOME_SIZE / 2; j < GENOME_SIZE; ++j) {
      memcpy(&g[j], &g[j % keep], sizeof(Genome));
      int limit = 1;
      if (j > 3 * GENOME_SIZE / 4)
        limit = 3;
      for (int k = 0; k < 3; ++k) {
        // Mutation.
        int byte = rand() % (2 * CONTEXT_COUNT);
        if (byte < CONTEXT_COUNT) {
          g[j].params.contexts[byte] = pats[rand() % pc].ctx;
        } else {
          g[j].params.weights[byte - CONTEXT_COUNT] = rand() % MAX_WEIGHT + 1;
        }
      }
    }
  }

  delete[] g;

  if (CompressSingle(params, in, inLen, out, outLen)) {
    printf("Final: %d", *outLen);
    for (int i = 0; i < params->contextCount; ++i) {
      printf(" %2d*%2.2x", params->weights[i], params->contexts[i]);
    }
    printf("\n");
    printf("cmax: %d\n", cmax);
    char buf[128];
    params->ToString(buf);
    printf("Params: %s\n", buf);
    return true;
  }
  printf("Failed, for some reason could not recompress with optimal settings\n");
  return false;
}

bool Compressor::CompressSingle(CompressionParameters* comp, void* in, int inLen, void* out, int* outLen) {
  u8* archive = (u8*)in;
  u8* output;
  u8* counters[MAX_CONTEXT_COUNT];  // Counter base offsets
  u8* cp[MAX_CONTEXT_COUNT];  // Current counters
  u8 tbuf[8] = {1, 0, 0, 0, 0, 0, 0, 0};

  memset(modelCounters_, 0, MAX_CONTEXT_SIZE * comp->contextCount);
  u8* base = modelCounters_; 
  for (int m = 0; m < comp->contextCount; ++m) {
    counters[m] = base;
    cp[m] = base;
    base += MAX_CONTEXT_SIZE;
  }

  u8* cout = (u8*)out;
  u32 x1 = 0, x2 = 0xffffffff;
  for (int j = 0; j < inLen; ++j) {
    u32 byte = *archive++;
    for (u32 i = 0; i < 8; ++i) {
      u32 n0 = 1, n1 = 1;
      for (int m = 0; m < comp->contextCount; ++m) {
        n0 += cp[m][0] * comp->weights[m];
        n1 += cp[m][1] * comp->weights[m];
      }

      u32 xmid = x1 + n0 * (u64)(x2 - x1) / (n0 + n1); 

      int y;
      if (byte & 0x80) {
        x1 = xmid + 1;
        y = 1;
      } else {
        x2 = xmid;
        y = 0;
      }

      // Store bit y 
      tbuf[0] += tbuf[0] + y;
      if (i == 7) {  // Start new byte
        tbuf[7] = tbuf[6];
        tbuf[6] = tbuf[5];
        tbuf[5] = tbuf[4];
        tbuf[4] = tbuf[3];
        tbuf[3] = tbuf[2];
        tbuf[2] = tbuf[1];
        tbuf[1] = tbuf[0];
        tbuf[0] = 1;
      }

      // Count y by context
      for (int m = comp->contextCount - 1; m >= 0; --m) {
        if (cp[m][y] < 255)
          ++cp[m][y];
        if (cp[m][1-y] > 2)
          cp[m][1-y] = cp[m][1-y] / 2 + 1;
        u32 off = 0, cnt = 0;
        for (char i = 0; i < 8; ++i) {
          if (comp->contexts[m] & (1 << i)) {
            off = (off << 8) + tbuf[i];
          }
        }
        // Use a sort of hashtable here. Decompressor just uses c = 0.
        u32 c = 24 * ((off & 0xffff) ^ (off >> 16));
        while (*(u32*)&counters[m][c] != 0 && *(u32*)&counters[m][c] != off) c += 6;
        if (c > cmax) cmax = c;
        *(u32*)&counters[m][c] = off;
        cp[m] = &counters[m][c + 4];
      }

      while (((x1 ^ x2) & 0xff000000) == 0) {
        *cout++ = x2 >> 24;
        x1 <<= 8;
        x2 = (x2 << 8) + 255;
        if (cout - (u8*)out >= *outLen) return false;
      }
      byte <<= 1;
    }
  }
  while (((x1 ^ x2) & 0xff000000)==0) {
    *cout++ = x2 >> 24;
    x1 <<= 8;
    x2 = (x2 << 8) + 255;
    if (cout - (u8*)out >= *outLen) return false;
  }
  *cout++ = x2 >> 24;  // First unequal byte
  // Now for some weirdness: if the next byte happens to be less than 0xc3 (the
  // ret opcode), we need to output something that's less than that. Zero is a
  // good candidate.
  if (((x2 >> 16) & 0xff) < 0xc3) {
    if (cout - (u8*)out >= *outLen) return false;
    *cout++ = 0;
  }
  *outLen = cout - (u8*)out;
  return true;
}

