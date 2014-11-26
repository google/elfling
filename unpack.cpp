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
#include <string.h>

void Compressor::Decompress(CompressionParameters* params, void* in, void* out, int outLen) {
  u8* counters[MAX_CONTEXT_COUNT];  // Counter base offsets
  u8* cp[MAX_CONTEXT_COUNT];  // Current counters
  u8* archive = (u8*)in;
  u8* cout = (u8*)out;

  memset(modelCounters_, 0, MAX_CONTEXT_SIZE * params->contextCount);
  u8* base = modelCounters_; 
  for (int m = 0; m < params->contextCount; ++m) {
    counters[m] = base;
    cp[m] = base;
    base += MAX_CONTEXT_SIZE;
  }

  *cout = 1;
  u32 x1 = 0, x2 = 0xffffffff;                              
  for (u32 j = outLen * 8; j > 0; --j) {
    u32 n0 = 1, n1 = 1;
    for (char m = 0; m < params->contextCount; ++m) {
      n0 += cp[m][0] * params->weights[m];
      n1 += cp[m][1] * params->weights[m];
    }

    u32 xmid = x1 + n0 * (u64)(x2 - x1) / (n0 + n1);

    char y;
    cout[0] <<= 1;
    if (*(u32*)archive <= xmid) {
      x2 = xmid;
      y = 0;
    } else {
      cout[0] += 1;
      x1 = xmid + 1;
      y = 1;
    }

    if (((j - 1) & 7) == 0) {  // Start new byte
      cout++;
      *cout = 1;
    }
    
    // Count y by context
    for (char m = 0; m < params->contextCount; ++m) {
      if (cp[m][y] < 255)
        ++cp[m][y];
      if (cp[m][1 - y] > 2)
        cp[m][1 - y] = cp[m][1 - y] / 2 + 1;
      u32 off = 0, cnt = 0;
      for (char i = 0; i < 8; ++i) {
        if (params->contexts[m] & (1 << i)) {
          off = (off << 8) + cout[-i];
        }
      }
      u32 c = 0;
      while (*(u32*)&counters[m][c] != 0 && *(u32*)&counters[m][c] != off) c += 6;
      *(u32*)&counters[m][c] = off;
      cp[m] = &counters[m][c + 4];
    }

    while (((x1 ^ x2) >> 24) == 0) {
      x1 <<= 8;
      x2 = (x2 << 8) + 255;
      --archive;
    }
  }
}

