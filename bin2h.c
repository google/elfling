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

int main(int argc, char* argv[]) {
  if (argc < 4) { printf("Usage: bin2h [in] [out] [var]\n"); return 1; }
  FILE* fptr = fopen(argv[1], "rb");
  if (fptr == NULL) { printf("Could not open %s\n", argv[1]); return 1; }
  unsigned char buf[4096];
  int l = fread(buf, 1, 4096, fptr);
  fclose(fptr);
  if (l <= 0) { printf("%s is empty\n", argv[1]); return 1; }
  fptr = fopen(argv[2], "w");
  if (fptr == NULL) { printf("Could not open %s\n", argv[2]); return 1; }
  fprintf(fptr, "unsigned char %s[] = {", argv[3]);
  for (int i = 0; i < l; ++i) {
    if ((i & 15) == 0) { fprintf(fptr, "\n  "); }
    fprintf(fptr, "0x%2.2x, ", buf[i]);
  }
  fprintf(fptr, "\n};\n");
  fclose(fptr);
  return 0;
}
