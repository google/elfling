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

#include "header32.h"
#include "header64.h"
#include "pack.h"

bool verbose = false;

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

template<int bits> struct Elf {
};
template<> struct Elf<32> {
  typedef Elf32_Shdr Shdr;
  typedef Elf32_Phdr Phdr;
  typedef Elf32_Ehdr Ehdr;
  typedef Elf32_Sym Sym;
  typedef Elf32_Rel Rel;
  typedef Elf32_Rela Rela;
};
template<> struct Elf<64> {
  typedef Elf64_Shdr Shdr;
  typedef Elf64_Phdr Phdr;
  typedef Elf64_Ehdr Ehdr;
  typedef Elf64_Sym Sym;
  typedef Elf64_Rel Rel;
  typedef Elf64_Rela Rela;
};

template<int bits> class Section {
  typedef Elf<bits> E;
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
  void SetHdr(const typename E::Shdr& hdr) {
    hdr_ = hdr;
  }
  const typename E::Shdr& Hdr() const { return hdr_; }

  const std::string& Name() const { return name_; }
  const u8* Data() const { return data_; }
  u32 Size() const { return size_; }
  
protected:
  std::string name_;
  u32 size_ = 0;
  u32 limit_ = 0;
  u8* data_ = nullptr;

  typename E::Shdr hdr_;
};

class MemFile {
public:
  MemFile() {}
  ~MemFile() {
    delete[] data_;
  }
  bool Load(const char* fn) {
    name_ = fn;
    FILE* fptr = fopen(fn, "rb");
    if (!fptr) {
      printf("Could not open %s\n", fn);
      return false;
    }
    fseek(fptr, 0, SEEK_END);
    size_ = ftell(fptr);
    fseek(fptr, 0, 0);
    data_ = new u8[size_];
    u32 rl = fread(data_, 1, size_, fptr);
    if (size_ != rl) {
      printf("Could not read %s\n", fn);
      return false;
    }
    fclose(fptr);
    return true;
  }

  const char* Name() const { return name_.c_str(); }
  const u8* Data() const { return data_; }
  size_t Size() const { return size_; }

private:
  u8* data_ = nullptr;
  size_t size_ = 0;
  std::string name_;
};

template<int bits> class Image {
  typedef Elf<bits> E;
public:
  Image() {}
  ~Image() {
    for (Section<bits>* s : sections_) {
      delete s;
    }
  }
  
  bool Load(MemFile* f) {
    const u8* obj = f->Data();
    u8 elfMagic[4] = {0x7f, 0x45, 0x4c, 0x46};
    memcpy(&ehdr_, obj, sizeof(ehdr_));
    if (memcmp(ehdr_.e_ident, elfMagic, 4)) {
      printf("Invalid header signature in %s\n", f->Name());
      return false;
    }
    if (ehdr_.e_phnum) {
      if (ehdr_.e_phentsize != sizeof(typename E::Phdr)) { 
        printf("Object %s has unexpected e_phentsize (%d != %lu)\n", f->Name(), ehdr_.e_phentsize, sizeof(typename E::Phdr));
        return false;
      }
      memcpy(phdr_, &obj[ehdr_.e_phoff], ehdr_.e_phnum * ehdr_.e_phentsize);
    }
    if (ehdr_.e_shentsize != sizeof(typename E::Shdr)) { 
      printf("Object %s has unexpected e_shentsize (%d != %lu)\n", f->Name(), ehdr_.e_shentsize, sizeof(typename E::Shdr));
      return false;
    }
    memcpy(shdr_, &obj[ehdr_.e_shoff], ehdr_.e_shnum * ehdr_.e_shentsize);
    
    // Get the strings for now
    char* strings = (char*)&obj[shdr_[ehdr_.e_shstrndx].sh_offset];
    for (u32 i = 0; i < ehdr_.e_shnum; ++i) {
      if (verbose)
        printf("Loading section '%s' at offset 0x%x, %d bytes\n", &strings[shdr_[i].sh_name], (u32)shdr_[i].sh_offset, (int)shdr_[i].sh_size);  
      Section<bits>* s = new Section<bits>(&strings[shdr_[i].sh_name]);
      if (shdr_[i].sh_type != SHT_NOBITS) {
        s->Set(&obj[shdr_[i].sh_offset], shdr_[i].sh_size);
      }
      s->SetHdr(shdr_[i]);
      sectionMap_[s->Name()] = s;
      sections_.push_back(s);
    }
    return true;
  }
  
  u32 SectionCount() const { return sections_.size(); }
  Section<bits>* GetSection(const char* str) {
    typename std::map<std::string, Section<bits>*>::iterator it = sectionMap_.find(str);
    if (it == sectionMap_.end()) { return nullptr; }
    return it->second;
  }
  Section<bits>* GetSection(u32 s) {
    if (s < sections_.size()) { return sections_[s]; }
    return nullptr;
  }
private:
  typename E::Ehdr ehdr_;
  typename E::Phdr phdr_[64];
  typename E::Shdr shdr_[256];

  std::map<std::string, Section<bits>*> sectionMap_;
  std::vector<Section<bits>*> sections_;
};

const u32 base = 0x08000000;

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

template<int bits> class Linker {
  typedef typename Elf<bits>::Sym Sym;
  typedef typename Elf<bits>::Rel Rel;
  typedef typename Elf<bits>::Rela Rela;
public:
  bool Link(MemFile* o) {
    Image<bits> obj;
    if (!obj.Load(o)) {
      return false;
    }

    // Start off by finding the _start symbol.
    Section<bits>* symtab = obj.GetSection(".symtab");
    Sym* symbols = (Sym*)symtab->Data();
    char* symbolNames = (char*)obj.GetSection(symtab->Hdr().sh_link)->Data();
    u32 startSection = 0;
    u32 startOffset = 0;
    {
      u32 sc = symtab->Size() / sizeof(Sym);
      for (u32 i = 0; i < sc; ++i) {
        // The ELF32_ST_XXX macros are identical to ELF64_ST_XXX.
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
    Section<bits>* bss = obj.GetSection(".bss");
    sections[".text"] = 0;
    if (bss) {
      commonOff += bss->Size(); 
    }
  
    for (u32 i = 0; i < obj.SectionCount(); ++i) {
      if (!strncmp(obj.GetSection(i)->Name().c_str(), ".rel.", 5) && bits == 32) {
        if (bits != 32) { printf("Unsupported relocation: .rel with 64 bits\n"); return false; }
        Section<bits>* rel = obj.GetSection(i);
        Rel* tr = (Rel*)rel->Data();
        for (u32 j = 0; j < rel->Size() / sizeof(Rel); ++j) {
          u32 sym = ELF32_R_SYM(tr[j].r_info);
          u32 type = ELF32_R_TYPE(tr[j].r_info);
          if (symbols[sym].st_shndx && symbols[sym].st_shndx < obj.SectionCount()) {
            if (obj.GetSection(symbols[sym].st_shndx)->Name() != ".bss") {
              sections[obj.GetSection(symbols[sym].st_shndx)->Name()] = 0;
            }
          }
          if (ELF32_ST_TYPE(symbols[sym].st_info) == STT_NOTYPE && ELF32_ST_BIND(symbols[sym].st_info) == STB_GLOBAL) {
            imports.insert(&symbolNames[symbols[sym].st_name]);
          }
          if (symbols[sym].st_shndx == SHN_COMMON && common.find(symbols[sym].st_name) == common.end()) {
            common[symbols[sym].st_name] = commonOff;
            commonOff += symbols[sym].st_size;
          }
        }
      } else if (!strncmp(obj.GetSection(i)->Name().c_str(), ".rela.", 6)) {
        if (bits != 64) { printf("Unsupported relocation: .rela with 32 bits\n"); return false; }
        Section<bits>* rel = obj.GetSection(i);
        Rela* tr = (Rela*)rel->Data();
        for (u32 j = 0; j < rel->Size() / sizeof(Rela); ++j) {
          u32 sym = ELF64_R_SYM(tr[j].r_info);
          u32 type = ELF64_R_TYPE(tr[j].r_info);
          if (symbols[sym].st_shndx && symbols[sym].st_shndx < obj.SectionCount()) {
            if (obj.GetSection(symbols[sym].st_shndx)->Name() != ".bss") {
              sections[obj.GetSection(symbols[sym].st_shndx)->Name()] = 0;
            }
          }
          if (ELF32_ST_TYPE(symbols[sym].st_info) == STT_NOTYPE && ELF32_ST_BIND(symbols[sym].st_info) == STB_GLOBAL) {
            imports.insert(&symbolNames[symbols[sym].st_name]);
          }
          if (symbols[sym].st_shndx == SHN_COMMON && common.find(symbols[sym].st_name) == common.end()) {
            common[symbols[sym].st_name] = commonOff;
            commonOff += symbols[sym].st_size;
          }
        }
      }
    }  
  
    const char* sig = "XXXX-Compressed code here-XXXX"; 
    u32 sz = 0;
    for (const u8* compoff = Header(); compoff < Header() + HeaderSize() - strlen(sig); ++compoff) {
      if (!memcmp(compoff, sig, strlen(sig))) {
        sz = compoff - Header();
        break;
      }
    }
    
    u8* finalout = (u8*)malloc(65536);
    u32 finalsize = HeaderSize() - strlen(sig) - sz;
    memcpy(finalout, &Header()[sz + strlen(sig)], finalsize);
    u32 tailoff = finalsize;
  
    u32 hashoff = finalsize;
    for (const std::string& imp : imports) {
      if (verbose)
        printf("Import %-15s @ 0x%8.8x\n", imp.c_str(), finalsize);
      if (bits == 32) {
        // 5 byte jump table entries:
        //  e9 xx xx xx xx jmp dword relative
        finalout[finalsize++] = 0xe9;    
        *(u32*)&finalout[finalsize] = hash(imp.c_str());
        finalsize += 4;
      } else {
        // 14 byte jump table entries:
        //  ff 25 00 00 00 00       jmp [rip + rel]
        //  xx xx xx xx xx xx xx xx absolute destination of jump
        finalout[finalsize++] = 0xff;
        finalout[finalsize++] = 0x25;
        *(u32*)&finalout[finalsize] = 0;
        finalsize += 4;
        *(u64*)&finalout[finalsize] = hash(imp.c_str());
        finalsize += 8;
      }
    }
    // Add one more entry with all zeroes for termination.
    for (int i = 0; i < (bits == 32 ? 5 : 14); ++i) {
      finalout[finalsize++] = 0;    
    }
    // Put in entry point as relative 32-bit address. The assembly header ends
    // with a relative jump.
    *(u32*)&finalout[tailoff - 4] = finalsize + startOffset - tailoff;  

    // We now have a list of all referenced sections, locate them in the final
    // binary.
    u32 textoff = 0;
    for (auto& sec : sections) {
      // Take all sections starting with ".text"
      if (!strncmp(sec.first.c_str(), ".text", 5)) {
        sec.second = finalsize;
        textoff = finalsize;
        printf("Section %-15s @ 0x%8.8x\n", sec.first.c_str(), finalsize);
        finalsize += obj.GetSection(sec.first.c_str())->Size();
      }
    }
    for (auto& sec : sections) {
      // Take all sections not starting with ".text"
      if (strncmp(sec.first.c_str(), ".text", 5)) {
        sec.second = finalsize;
        printf("Section %-15s @ 0x%8.8x\n", sec.first.c_str(), finalsize);
        finalsize += obj.GetSection(sec.first.c_str())->Size();
      }
    }
    for (auto& sec : sections) {
      // Copy section contents.
      memcpy(&finalout[sec.second], obj.GetSection(sec.first.c_str())->Data(), obj.GetSection(sec.first.c_str())->Size());
    }

    u32 commonbase = (finalsize + 255) & (~255); 
    if (bss) {
      sections[".bss"] = commonbase;
      printf("Section .bss            @ 0x%8.8x\n", commonbase);
      commonbase += bss->Size();
    }

    // Apply text relocations.
    for (u32 i = 0; i < obj.SectionCount(); ++i) {
      if (!strncmp(obj.GetSection(i)->Name().c_str(), ".rel.", 5)) {
        const char* secName = obj.GetSection(i)->Name().c_str() + 4;
        if (sections.find(secName) == sections.end()) continue;
        if (verbose)
          printf("Relocating %s\n", secName);
        Section<bits>* rel = obj.GetSection(i);
        Rel* tr = (Rel*)rel->Data();
        u32 trc = rel->Size() / sizeof(Rel);
        Sym* st = (Sym*)obj.GetSection(rel->Hdr().sh_link)->Data();
        char* sn = (char*)obj.GetSection(obj.GetSection(rel->Hdr().sh_link)->Hdr().sh_link)->Data();
        u32 secoff = sections[secName];
        for (u32 j = 0; j < trc; ++j) {
          u32 sym, type;
          sym = ELF32_R_SYM(tr[j].r_info);
          type = ELF32_R_TYPE(tr[j].r_info);
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
          if (verbose)
            printf(" %3d %4x %2d %3d %-20s %-10s %-10s %4x %4x %x %-s %x\n",
              j, (u32)tr[j].r_offset, type, sym, &symbolNames[symbols[sym].st_name],
              bind, types, (u32)symbols[sym].st_value, (u32)symbols[sym].st_size,
              symbols[sym].st_other, symbols[sym].st_shndx < obj.SectionCount() ? obj.GetSection(symbols[sym].st_shndx)->Name().c_str() : "*",
              symbols[sym].st_shndx);
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
      } else if (!strncmp(obj.GetSection(i)->Name().c_str(), ".rela.", 6)) {
        const char* secName = obj.GetSection(i)->Name().c_str() + 5;
        if (sections.find(secName) == sections.end()) continue;
        if (verbose)
          printf("Relocating %s\n",  secName);
        Section<bits>* rel = obj.GetSection(i);
        Rela* tr = (Rela*)rel->Data();
        u32 trc = rel->Size() / sizeof(Rela);
        Sym* st = (Sym*)obj.GetSection(rel->Hdr().sh_link)->Data();
        char* sn = (char*)obj.GetSection(obj.GetSection(rel->Hdr().sh_link)->Hdr().sh_link)->Data();
        u32 secoff = sections[secName];
        for (u32 j = 0; j < trc; ++j) {
          u32 sym, type;
          sym = ELF64_R_SYM(tr[j].r_info);
          type = ELF64_R_TYPE(tr[j].r_info);
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
          if (verbose)
            printf(" %3d %4x[%4x] %d %3d %-20s %-10s %-10s %4x %4x %x %-s %x\n",
              j, (u32)tr[j].r_offset, (u32)(secoff + tr[j].r_offset), type, sym, &symbolNames[symbols[sym].st_name],
              bind, types, (u32)symbols[sym].st_value, (u32)symbols[sym].st_size,
              symbols[sym].st_other, symbols[sym].st_shndx < obj.SectionCount() ? obj.GetSection(symbols[sym].st_shndx)->Name().c_str() : "*",
              symbols[sym].st_shndx);
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
            b = hashoff + 14 * func + base;
          } else {
            printf("Unknown section %x\n", sec);
          }
          if (type == R_X86_64_64) {
            *(u64*)off += b + 0x10000 + tr[j].r_addend;
          } else if (type == R_X86_64_32) {
            *off += b + 0x10000 + tr[j].r_addend;
          } else if (type == R_X86_64_PC32) {
            *off += b - tr[j].r_offset - secoff - base + tr[j].r_addend;
          } else {
            printf("Unknown type %d\n", type);
          }
        }
      }
    }
    
    FILE* tmpout = fopen("test/tmp", "wb");
    fwrite(finalout, finalsize, 1, tmpout);
    fclose(tmpout);
  
    u8* bin = (u8*)malloc(65536);
    u8* data = (u8*)malloc(65536);
    int ds = 65536;
    Compressor* c = new Compressor();
    CompressionParameters params;
    params.FromString(FlagWithDefault('c', ""));
    c->Compress(&params, finalout, finalsize, data + 8, &ds);
    Invert(data + 8, ds);
    memcpy(data, &Header()[sz - 8], 8);
  
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
    memcpy(bin, Header(), sz);
    memcpy(&bin[sz], data + 8, ds);
    sz += ds;
    // Set pointer to last 4 bytes of compressed data in code. The address may
    // need adjusting if the header assembly changes.
    if (bits == 32) {
      *(u32*)&bin[0xd8] = 0x08000000 + sz - 4;
    } else {
      *(u32*)&bin[0x169] = 0x08000000 + sz - 4;
    }
    // Place size of decompressed data in bits at end of image.
    *(u32*)&bin[sz] = finalsize * 8;
    // Add compression parameters to end of image.
    memcpy(&bin[sz + 4], params.weights, params.contextCount);
    memcpy(&bin[sz + 4 + params.contextCount], params.contexts, params.contextCount);
    sz += 4 + 2 * params.contextCount;
    // Place file size in ELF header.
    if (bits == 32) {
      *(u32*)&bin[0x7c] = sz;
    } else {
      *(u64*)&bin[0xc8] = sz;
    }
  
    FILE* fptr = fopen(FlagWithDefault('o', "c.out"), "wb");
    fwrite(bin, sz, 1, fptr);
    fclose(fptr);
    printf("Wrote %d bytes\n", sz);
  }
private:
  inline const u8* Header();
  inline u32 HeaderSize();
};

template<> const u8* Linker<32>::Header() { return header32; }
template<> const u8* Linker<64>::Header() { return header64; }
template<> u32 Linker<32>::HeaderSize() { return sizeof(header32); }
template<> u32 Linker<64>::HeaderSize() { return sizeof(header64); }

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1]) {
        args[argv[i][1]].insert(std::string(&argv[i][2]));
      }
    } else {
      args['i'].insert(argv[i]);
    }
  }
  verbose = HasFlag("verbose");

  if (args['i'].size() == 0) { printf("No obj specified\n"); return 1; }
  const char* fn = args['i'].begin()->c_str();
  MemFile o;
  if (!o.Load(fn)) {
    return 0;
  }
  u16 arch = *(u16*)&o.Data()[18];
  if (arch == 3) {
    printf("Arch: i386\n");
    Linker<32> l;
    l.Link(&o);
  } else if (arch == 62) {    
    printf("Arch: x86_64\n");
    Linker<64> l;
    l.Link(&o);
  } else {
    printf("Cannot handle architecture in %s (arch = %2.2x)\n", fn, arch);
    return 1;
  }
}
