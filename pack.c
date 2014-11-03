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
typedef unsigned int u24;

u8 modelCounters[1 << 27];  // 128MB
u8 weights[4] = {1, 4, 4, 4};
u8 contexts[4] = { 0x01, 0x03, 0x05, 0x13 };

void CompressSingle(u8* archive, u32 size, u8* out, u32* osize);

void Optimize(u8* archive, u32 size, u8* out, u32* osize) {
  u8 pats[256];
  u32 pc = 0;
  for (u32 i = 0; i < 256; ++i) {
    u32 bc = 0;
    for (u8 b = 0; b < 8; ++b) {
      if (i & (1 << b)) ++bc;
    }
    if (bc <= 3 && (i&1)) pats[pc++] = i;
  }
  printf("%d bytes, trying %d patterns\n", size, pc);
  int smallest = size;
  u32 sc, sw;
  for (int i = 0; i < pc; ++i) {
    for (int j = i + 1; j < pc; ++j) {
      for (int k = j + 1; k < pc; ++k) {
        for (int l = k + 1; l < pc; ++l) {
          for (int u = 4; u <= 8; u <<= 1) {
            for (int v = 4; v <= 8; v <<= 1) {
              for (int w = 4; w <= 8; w <<= 1) {
                weights[1] = u;
                weights[2] = v;
                weights[3] = w;
                contexts[0] = pats[i];
                contexts[1] = pats[j];
                contexts[2] = pats[k];
                contexts[3] = pats[l];
                CompressSingle(archive, size, out, osize);
                if (*osize < smallest) {
                  smallest = *osize;
                  printf("Smallest: %d %x*%2.2x %x*%2.2x %x*%2.2x %x*%2.2x\n", smallest, weights[0], contexts[0], weights[1], contexts[1], weights[2], contexts[2], weights[3], contexts[3]);
                  sc = *(u32*)contexts;
                  sw = *(u32*)weights;
                }
              }
            }
          }
        }
      goto done;
      }
    }
  }
done:
  *(u32*)contexts = sc;
  *(u32*)weights = sw;
  CompressSingle(archive, size, out, osize);
  printf("Final: %d %8.8x %8.8x\n", *osize, sc, sw);
}

void CompressSingle(u8* archive, u32 size, u8* out, u32* osize) {
  u8* output;
  u8* counters[4];  // Counter base offsets
  u8* cp[4];  // Current counters
  int tbuf[8] = {1, 0, 0, 0, 0, 0, 0, 0};

  memset(modelCounters, 0, 1 << 27);
  u8* base = modelCounters; 
  for (int m = 3; m >= 0; --m) {
    counters[m] = base;
    cp[m] = base;
    base += 1 << 25;
  }

  u8* cout = out;
  u32 x1 = 0, x2 = 0xffffffff;
  for (u32 j =0; j < size; ++j) {
    u32 byte = *archive++;
    for (u32 i =0; i < 8; ++i) {
      // Split the range
      int n0 = 1, n1 = 1;
      for (int m = 0; m < 4; ++m) {
        n0 += cp[m][0] * weights[m];
        n1 += cp[m][1] * weights[m];
      }
      
      u32 xmid = x1 + n0 * (u64)(x2 - x1) / (n0 + n1); 
                                                          
      // Update the range
      int y;
      if (byte & 0x80) {
        x1 = xmid+1;
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
        tbuf[1] = tbuf[0] - 256; 
        tbuf[0] = 1;
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
            off = (off << 8) + tbuf[i];
          }
        }
        cp[m] = &counters[m][off << 1];
      }

      // Shift equal MSB's out
      while (((x1^x2)&0xff000000)==0) {
        *cout++ = x2>>24;
        x1<<=8;
        x2=(x2<<8)+255;
      }
      byte <<= 1;
    }
  }
  while (((x1^x2)&0xff000000)==0) {
    *cout++ = x2>>24;
    x1<<=8;
    x2=(x2<<8)+255;
  }
  *cout++ = x2>>24;  // First unequal byte
  *osize = cout - out;
}

