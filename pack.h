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

#ifndef INCLUDED_PACK_H
#define INCLUDED_PACK_H

#define MAX_CONTEXT_COUNT 16
#define MAX_CONTEXT_SIZE (4 << 20)

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

struct CompressionParameters {
  int contextCount = 0;
  u8 weights[MAX_CONTEXT_COUNT];
  u8 contexts[MAX_CONTEXT_COUNT];

  bool FromString(const char* str);
  void ToString(char* str);
};

class Compressor {
public:
  //! Compresses data.
  /*! \param params Filled out with the compression parameters.
      \param in Pointer to input data.
      \param inLen Length of input data in bytes.
      \param out Pointer to output data.
      \param outLen Pointer to length of output data. Must contain maximum length of output data as input.
      \return true if the data could be compressed into the provided output buffer.
  */
  bool Compress(CompressionParameters* params, void* in, int inLen, void* out, int* outLen);

  //! Decompresses data.
  /*! \param params Compression parameters.
      \param in Pointer to last 4 bytes of input data, since this is read backwards.
      \param out Pointer to output data.
      \param outLen Length of output data.
  */
  void Decompress(CompressionParameters* params, void* in, void* out, int outLen);
  
private:
  bool CompressSingle(CompressionParameters* comp, void* in, int inLen, void* out, int* outLen);

private:
  unsigned char modelCounters_[MAX_CONTEXT_SIZE * MAX_CONTEXT_COUNT];
  bool verbose_ = false;
};

#endif  // INCLUDED_PACK_H
