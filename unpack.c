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

#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u24;

extern u8 modelCounters[1 << 27];  // 128MB
extern u8 weights[4];
extern u8 contexts[4];

void DecompressSingle(u8* archive, u8* out, u32 osize) {
  u8* counters[4];  // Counter base offsets
  u8* cp[4];  // Current counters

  *out = 1;

  u8* base = modelCounters; 
  for (int m = 3; m >= 0; --m) {
    counters[m] = base;
    cp[m] = base;
    base += 1 << 25;
  }

  u32 x2 = 0xffffffff, x1 = 0;
                              
  for (u32 j = osize; j > 0; --j) {
    // Split the range
    u16 n0 = 1, n1 = 1;
    for (char m = 3; m >= 0; --m) {
      n0 += cp[m][0] * weights[m];
      n1 += cp[m][1] * weights[m];
    }
    
    u32 xmid = x1 + n0 * (u64)(x2 - x1) / (n0 + n1);
                                                        
    char y;
    out[0] <<= 1;
    if (*(u32*)archive <= xmid) {
      x2 = xmid;
      y = 0;
    } else {
      out[0] += 1;
      x1 = xmid + 1;
      y = 1;
    }

    if (((j - 1) & 7) == 0) {  // Start new byte
      out++;
      *out = 1;
    }
    
    // Count y by context
    for (char m = 3; m >= 0; --m) {
      if (cp[m][y] < 255)
        ++cp[m][y];
      if (cp[m][1-y] > 2)
        cp[m][1-y] = cp[m][1-y] / 2 + 1;
      u32 off = 0, cnt = 0;
      for (char i = 0; i < 8; ++i) {
        if (contexts[m] & (1 << i)) {
          off = (off << 8) + out[-i];
        }
      }
      cp[m] = &counters[m][off << 1];
    }

    // Shift equal MSB's out
    while (((x1^x2) >> 24) == 0) {
      x1 <<= 8;
      x2 = (x2 << 8) + 255;
      --archive;
    }
  }
  
  memset(modelCounters, 0, 1 << 27);
}


