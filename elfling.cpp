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

#include <elf.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "header.h"
#include "pack.h"

typedef unsigned char u8;
typedef unsigned int u32;

class Section {
public:
  Section(const char* name) : name_(name) {}
  ~Section() {
    delete[] data_;
  }
  
  void Set(const void* data, u32 size) {
    if (size > limit_) {
      delete[] data_;
      data_ = new u8[size];
      limit_ = size; 
    }
    memcpy(data_, data, size);
    size_ = size; 
  }
  void Reset() {
    size_ = 0;
  }
  void Append(const void* data, u32 size) {
    if (size + size_ > limit_) {
      limit_ = (size + size_ + 0x0fff) & (~0x0fff);
      u8* nd = new u8[limit_];
      memcpy(nd, data_, size_);
      delete[] data_;
      data_ = nd;
    }
    memcpy(&data_[size_], data, size);
    size_ += size; 
  }
  void SetHdr(const Elf32_Shdr& hdr) {
    hdr_ = hdr;
  }
  const Elf32_Shdr& Hdr() const { return hdr_; }
    
  const std::string& Name() const { return name_; }
  const u8* Data() const { return data_; }
  u32 Size() const { return size_; }
  
protected:
  std::string name_;
  u32 size_ = 0;
  u32 limit_ = 0;
  u8* data_ = nullptr;

  Elf32_Shdr hdr_;
};

class Image {
public:
  Image() {}
  ~Image() {
    if (obj_) { free(obj_); }
  }
  bool Load(const char* fn) {
    FILE* fptr = fopen(fn, "rb");
    if (!fptr) {
      printf("Could not open %s\n", fn);
      return false;
    }
    fseek(fptr, 0, SEEK_END);
    size_ = ftell(fptr);
    fseek(fptr, 0, 0);
    obj_ = (u8*)malloc(size_);
    u32 rl = fread(obj_, 1, size_, fptr);
    if (size_ != rl) {
      printf("Could not read %s\n", fn);
      return false;
    }
    fclose(fptr);
    
    memcpy(&ehdr_, obj_, sizeof(ehdr_));
    if (memcmp(ehdr_.e_ident, elfSig_, 16)) {
      printf("Invalid header signature in %s\n", fn);
      return false;
    }
    if (ehdr_.e_phnum) {
      if (ehdr_.e_phentsize != sizeof(Elf32_Phdr)) { 
        printf("Object %s has unexpected e_phentsize (%d != %lu)\n", fn, ehdr_.e_phentsize, sizeof(Elf32_Phdr));
        return false;
      }
      memcpy(phdr_, &obj_[ehdr_.e_phoff], ehdr_.e_phnum * ehdr_.e_phentsize);
    }
    if (ehdr_.e_shentsize != sizeof(Elf32_Shdr)) { 
      printf("Object %s has unexpected e_shentsize (%d != %lu)\n", fn, ehdr_.e_shentsize, sizeof(Elf32_Shdr));
      return false;
    }
    memcpy(shdr_, &obj_[ehdr_.e_shoff], ehdr_.e_shnum * ehdr_.e_shentsize);
    
    // Get the strings for now
    char* strings = (char*)&obj_[shdr_[ehdr_.e_shstrndx].sh_offset];
    for (u32 i = 0; i < ehdr_.e_shnum; ++i) {
      printf("Loading section '%s' at offset 0x%x, %d bytes\n", &strings[shdr_[i].sh_name], shdr_[i].sh_offset, shdr_[i].sh_size);  
      Section* s = new Section(&strings[shdr_[i].sh_name]);
      if (shdr_[i].sh_type != SHT_NOBITS) {
        s->Set(&obj_[shdr_[i].sh_offset], shdr_[i].sh_size);
      }
      s->SetHdr(shdr_[i]);
      sectionMap_[s->Name()] = s;
      sections_.push_back(s);
    }
    return true;
  }
  
  u32 SectionCount() const { return sections_.size(); }
  Section* GetSection(const char* str) {
    std::map<std::string, Section*>::iterator it = sectionMap_.find(str);
    if (it == sectionMap_.end()) { return nullptr; }
    return it->second;
  }
  Section* GetSection(u32 s) {
    if (s < sections_.size()) { return sections_[s]; }
    return nullptr;
  }
  const u8* ElfSig() const { return elfSig_; }
private:
  u32 size_ = 0;
  u8* obj_ = nullptr;

  Elf32_Ehdr ehdr_;
  Elf32_Phdr phdr_[64];
  Elf32_Shdr shdr_[256];
  
  std::map<std::string, Section*> sectionMap_;
  std::vector<Section*> sections_;

  static const u8 elfSig_[16];
};

const u8 Image::elfSig_[16] = {0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00 , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const u32 base = 0x08000000;

static void SetPhdr(Elf32_Phdr* ph, Elf32_Word p_type, Elf32_Off p_offset, Elf32_Word p_filesz, Elf32_Word p_memsz, Elf32_Word p_flags) {
  ph->p_type = p_type;
  ph->p_offset = p_offset;
  ph->p_vaddr = p_offset + base;
  ph->p_paddr = p_offset + base;
  ph->p_filesz = p_filesz;
  ph->p_memsz = p_memsz;
  ph->p_flags = p_flags; 
  ph->p_align = 1;
}

u32 rol(u32 v, u32 s) {
  return (v << s) | ( v >> (32 - s));
}  
  
u32 hash(const char* str) {
  u32 rv = 0;
  for (const char* a = str; *a; ++a) {
    rv ^= *a;
    rv = rol(rv, 5);
  }
  rv = rol(rv, 5);
  return rv;
}


std::map<char, std::set<std::string>> args;

bool HasFlag(const char* flag) {
  return args['f'].find(flag) != args['f'].end();
}

const char* FlagWithDefault(char f, const char* def) {
  if (args[f].size() > 0) {
    return args[f].begin()->c_str();
  } else {
    return def;
  }
}

void Invert(u8* data, u32 s) {
  for (u32 i = 0; i < s >> 1; ++i) {
    u8 t = data[i];
    data[i] = data[s - i - 1];
    data[s - i - 1] = t;
  }
}

int main(int argc, char* argv[]) {
  for (int i =1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1]) {
        args[argv[i][1]].insert(std::string(&argv[i][2]));
      }
    } else {
      args['i'].insert(argv[i]);
    }
  }
    
  if (args['i'].size() == 0) { printf("No obj specified\n"); return 1; }
  
  Image obj;
  if (!obj.Load(args['i'].begin()->c_str())) {
    return 0;
  }
  
  // Start off by finding the _start symbol.
  Section* symtab = obj.GetSection(".symtab");
  Elf32_Sym* symbols = (Elf32_Sym*)symtab->Data();
  char* symbolNames = (char*)obj.GetSection(symtab->Hdr().sh_link)->Data();
  u32 startSection = 0;
  u32 startOffset = 0;
  {
    u32 sc = symtab->Size() / sizeof(Elf32_Sym);
    //printf("Scanning symbol table.\n");
    for (u32 i = 0; i < sc; ++i) {
      const char* bind = "*";
      switch (ELF32_ST_BIND(symbols[i].st_info)) {
      case STB_LOCAL: bind = "STB_LOCAL"; break;
      case STB_GLOBAL: bind = "STB_GLOBAL"; break;
      case STB_WEAK: bind = "STB_WEAK"; break;
      }
      const char* type = "*";
      switch (ELF32_ST_TYPE(symbols[i].st_info)) {
      case STT_NOTYPE: type = "STT_NOTYPE"; break;
      case STT_OBJECT: type = "STT_OBJECT"; break;
      case STT_FUNC: type = "STT_FUNC"; break;
      case STT_SECTION: type = "STT_SECTION"; break;
      case STT_FILE: type = "STT_FILE"; break;
      }
      if (!strcmp(&symbolNames[symbols[i].st_name], "_start")) {
        startSection = symbols[i].st_shndx;
        startOffset = symbols[i].st_value;
      }
    }
  }

  std::set<std::string> imports;
  std::map<std::string, u32> sections;
  std::map<u32, u32> common;
  u32 commonOff = 0;
  Section* bss = obj.GetSection(".bss");
  sections[".text"] = 0;

  for (u32 i = 0; i < obj.SectionCount(); ++i) {
    if (strncmp(obj.GetSection(i)->Name().c_str(), ".rel.", 5)) continue;
    Section* rel = obj.GetSection(i);
    Elf32_Rel* tr = (Elf32_Rel*)rel->Data();
    u32 trc = rel->Size() / sizeof(Elf32_Rel);
    if (bss) {
      commonOff += bss->Size(); 
    }
    for (u32 j = 0; j < trc; ++j) {
      u32 sym = ELF32_R_SYM(tr[j].r_info);
      u32 type = ELF32_R_TYPE(tr[j].r_info);
      //printf(" %3d %-20s %4x %4x %x %-s %x\n", j, &symbolNames[symbols[sym].st_name], symbols[sym].st_value, symbols[sym].st_size, symbols[sym].st_other, symbols[sym].st_shndx < obj.SectionCount() ? obj.GetSection(symbols[sym].st_shndx)->Name().c_str() : "*", symbols[sym].st_shndx);
      if (symbols[sym].st_shndx && symbols[sym].st_shndx < obj.SectionCount()) {
        if (obj.GetSection(symbols[sym].st_shndx)->Name() != ".bss") {
          sections[obj.GetSection(symbols[sym].st_shndx)->Name()] = 0;
        }
      }
      if (ELF32_ST_TYPE(symbols[sym].st_info) == STT_NOTYPE) {
        imports.insert(&symbolNames[symbols[sym].st_name]);
      }
      if (symbols[sym].st_shndx == SHN_COMMON && common.find(symbols[sym].st_name) == common.end()) {
        common[symbols[sym].st_name] = commonOff;
        commonOff += symbols[sym].st_size;
      }
    }
  }  

  const char* sig = "XXXX-Compressed code here-XXXX"; 
  u32 sz = 0;
  for (u8* compoff = header; compoff < header +sizeof(header) - strlen(sig); ++compoff) {
    if (!memcmp(compoff, sig, strlen(sig))) {
      sz = compoff - header;
      break;
    }
  }
  
  u8* finalout = (u8*)malloc(65536);
  u32 finalsize = sizeof(header) - strlen(sig) - sz;
  memcpy(finalout, &header[sz + strlen(sig)], finalsize);
  u32 tailoff = finalsize - 4;

  u32 hashoff = finalsize;
  for (const std::string& imp : imports) {
    // e9 78 56 34 12 c3 jmp dword relative
    printf("%x %s\n", finalsize, imp.c_str());
    finalout[finalsize++] = 0xe9;    
    *(u32*)&finalout[finalsize] = hash(imp.c_str());
    finalsize += 4;
  }
  for (int i = 0; i < 8; ++i) {
    finalout[finalsize++] = 0;    
  }
  *(u32*)&finalout[tailoff] = finalsize + startOffset - tailoff - 4;  // Put in entry point 
  
  u32 textoff = 0;
  for (auto& sec : sections) {
    // Take all sections starting with ".text"
    if (!strncmp(sec.first.c_str(), ".text", 5)) {
      sec.second = finalsize;
      textoff = finalsize;
      printf("%s @ %x\n", sec.first.c_str(), finalsize);
      finalsize += obj.GetSection(sec.first.c_str())->Size();
    }
  }
  for (auto& sec : sections) {
    // Take all sections not starting with ".text"
    if (strncmp(sec.first.c_str(), ".text", 5)) {
      sec.second = finalsize;
      printf("%s @ %x\n", sec.first.c_str(), finalsize);
      finalsize += obj.GetSection(sec.first.c_str())->Size();
    }
  }
  for (auto& sec : sections) {
    // Copy section contents.
    memcpy(&finalout[sec.second], obj.GetSection(sec.first.c_str())->Data(), obj.GetSection(sec.first.c_str())->Size());
  }
  memset(&finalout[finalsize], 0, 16);  // Add a few zeroes at the end.
  
  u32 commonbase = (finalsize + 255) & (~255); 
  if (bss) {
    sections[".bss"] = commonbase;
    printf(".bss @ %x\n", commonbase);
    commonbase += bss->Size();
  }

  // Apply text relocations.
  for (u32 i = 0; i < obj.SectionCount(); ++i) {
    if (strncmp(obj.GetSection(i)->Name().c_str(), ".rel.", 5)) continue;
    const char* secName = obj.GetSection(i)->Name().c_str() + 4;
    if (sections.find(secName) == sections.end()) continue;
    printf("Relocating %s\n",  secName);
    Section* rel = obj.GetSection(i);
    Elf32_Rel* tr = (Elf32_Rel*)rel->Data();
    u32 trc = rel->Size() / sizeof(Elf32_Rel);
    Elf32_Sym* st = (Elf32_Sym*)obj.GetSection(rel->Hdr().sh_link)->Data();
    char* sn = (char*)obj.GetSection(obj.GetSection(rel->Hdr().sh_link)->Hdr().sh_link)->Data();
    u32 secoff = sections[secName];
    for (u32 j = 0; j < trc; ++j) {
      u32 sym = ELF32_R_SYM(tr[j].r_info);
      u32 type = ELF32_R_TYPE(tr[j].r_info);
      u32* off = (u32*)&finalout[secoff + tr[j].r_offset];
      u32 b = 0;
      const char* bind = "*";
      switch (ELF32_ST_BIND(symbols[sym].st_info)) {
      case STB_LOCAL: bind = "STB_LOCAL"; break;
      case STB_GLOBAL: bind = "STB_GLOBAL"; break;
      case STB_WEAK: bind = "STB_WEAK"; break;
      }
      const char* types = "*";
      switch (ELF32_ST_TYPE(symbols[sym].st_info)) {
      case STT_NOTYPE: types = "STT_NOTYPE"; break;
      case STT_OBJECT: types = "STT_OBJECT"; break;
      case STT_FUNC: types = "STT_FUNC"; break;
      case STT_SECTION: types = "STT_SECTION"; break;
      case STT_FILE: types = "STT_FILE"; break;
      }
      printf(" %3d %4x %d %3d %-20s %-10s %-10s %4x %4x %x %-s %x\n", j, tr[j].r_offset, type, sym, &symbolNames[symbols[sym].st_name], bind, types, symbols[sym].st_value, symbols[sym].st_size, symbols[sym].st_other, symbols[sym].st_shndx < obj.SectionCount() ? obj.GetSection(symbols[sym].st_shndx)->Name().c_str() : "*", symbols[sym].st_shndx);
      u32 sec = st[sym].st_shndx;
      if (sec && sec < obj.SectionCount()) {
        b = base + sections[obj.GetSection(st[sym].st_shndx)->Name()] + symbols[sym].st_value; 
      } else if (sec == SHN_COMMON) {
        b = base + commonbase + common[st[sym].st_name];
      } else if (sec == 0) {
        u32 func = 0;
        for (const std::string& imp : imports) {
          if (imp == &symbolNames[symbols[sym].st_name]) break;
          ++func;
        }
        b = hashoff + 5 * func + base;
      } else {
        printf("Unknown section %x\n", sec);
      }
      if (type == R_386_32) {
        *off += b + 0x10000;
      } else if (type == R_386_PC32) {
        *off += b - tr[j].r_offset - secoff - base;
      } else {
        printf("Unknown type %d\n", type);
      }
    }
  }
  
  u8* bin = (u8*)malloc(65536);
  u8* data = (u8*)malloc(65536);
  int ds = 65536;
  Compressor* c = new Compressor();
  CompressionParameters params;
  params.FromString(FlagWithDefault('c', ""));
  c->Compress(&params, finalout, finalsize, data + 8, &ds);
  Invert(data + 8, ds);
  memcpy(data, &header[sz - 8], 8);

  // Sanity check our compressed data by decompressing it again.
  memset(bin, 0, 65536);
  c->Decompress(&params, &data[8 + ds - 4], bin + 8, finalsize);
  if (memcmp(finalout, bin + 8, finalsize)) {
    printf("Decompression failed, first 10 different bytes\n");
    int c = 0;
    for (int i = 0; i < finalsize; ++i) {
      if (finalout[i] != bin[i + 8]) {
        printf("%6x: %2.2x != %2.2x\n", i, finalout[i], bin[i + 8]);
        c++;
        if (c == 10) break;
      }
    }
  }
  memcpy(bin, header, sz);
  memcpy(&bin[sz], data + 8, ds);
  sz += ds;
  *(u32*)&bin[0xd8] = 0x08000000 + sz - 4;  // Set pointer to last 4 bytes of compressed data. 
  *(u32*)&bin[sz] = finalsize * 8 ;
  memcpy(&bin[sz + 4], params.weights, params.contextCount);
  memcpy(&bin[sz + 4 + params.contextCount], params.contexts, params.contextCount);
  sz += 4 + 2 * params.contextCount;
  *(u32*)&bin[0x7c] = sz; 

  FILE* fptr = fopen(FlagWithDefault('o', "c.out"), "wb");
  fwrite(bin, sz, 1, fptr);
  fclose(fptr);
  printf("Wrote %d bytes\n", sz);
}
