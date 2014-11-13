; Copyright 2014 Google Inc. All Rights Reserved.
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;     http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.
;
; elfling - a linking compressor for ELF files by Minas ^ Calodox

bits 32
base equ 0x08000000
ccount equ 8 ; Number of contexts.

; Elf32_Ehdr
db 0x7f, 'ELF'  ; e_ident (the part that is validated)
; db 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 ; e_ident (the part no one cares about)
strtab:
db 0   ; e_ident (the part no one cares about), initial zero of strtab
libgl:
db 'libGL.so.1', 0  ; Ideally I'd like to find a lib that includes both this and SDL, but there is none :-(
dw 2  ; e_type ET_EXEC
dw 3  ; e_machine EM_386
dd 1  ; e_version EV_CURRENT
dd base + entry ; e_entry
dd phoff  ; e_phoff
dd 0 ; e_shoff (ignored)
dd 0  ; e_flags
dw 0x34  ; e_ehsize
dw 0x20  ; e_phentsize
; The following 4 fields can overlap with the first phdr, since they are not validated, and
; e_phnum happens to have the same value as PT_INTERP..
;dw 3  ; e_phnum
;dw 0  ; e_shentsize
;dw 0  ; e_shnum
;dw 0  ; e_shstrndx

; Elf32_Phdr, 3 times. 
; This is the biggest space-waster here, but we need one for the interpreter, one for the dynamic
; table, and one for the actual loaded data (which can group everything else, and be writable).
phoff:
dd 3 ; p_type PT_INTERP
dd interp ; p_offset
dd base + interp ; p_vaddr
dd 0 ; p_paddr (ignored)
dd 19 ; p_filesz
dd 19 ; p_memsz
dd 4 ; p_flags PF_R
dd 1 ; p_align

dd 2 ; p_type PT_DYNAMIC
dd dynamic ; p_offset
dd base + dynamic ; p_vaddr
dd 0 ; p_paddr (ignored)
dd 6 * 8 ; p_filesz
dd 6 * 8 ; p_memsz
dd 4 ; p_flags PF_R
dd 1 ; p_align

dd 1 ; p_type PT_LOAD
dd 0 ; p_offset
dd base ; p_vaddr
dd 0 ; p_paddr (ignored)
dd 0xffffffff ; p_filesz, replaced by crunkler
dd 161 * 1024 * 1024 ; p_memsz
dd 7 ; p_flags PF_R | PF_W | PF_X
; dd 1  ; p_align

dynamic:  ; Overlap align of last ph with DT_NEEDED, which has the same value 
dd 1 ; DT_NEEDED
dd libgl - strtab
dd 1 ; DT_NEEDED
dd libsdl - strtab
dd 0x15 ; DT_DEBUG
debug:
dd 0
dd 5 ; DT_STRTAB
dd strtab + base
dd 6 ; DT_SYMTAB
dd strtab + base
dd 0 ; DT_NULL
;dd 0 ; This can be a random value.

interp:
db '/lib/ld-linux.so.2', 0
libsdl:
db 'libSDL-1.2.so.0', 0  ; Gets us libc, but no OpenGL.

v_dataend equ 0 ; Last dword of compressed data
v_osize equ 4 ; This is written in by elfling
v_weights equ 8 ; This is written in by elfling
v_contexts equ v_weights + ccount ; This is written in by elfling
v_archive equ v_contexts + ccount ; This is the start of our runtime variables
v_x2 equ v_archive + 4
v_x1 equ v_x2 + 4
v_cp equ v_x1 + 4
v_counters equ v_cp + 4 * ccount

entry:
; The destination is placed at 64k, so the compressed data cannot be larger than that. We also need
; some space for the variables. 
mov ebp, 0xffffffff ; Keep variables just after compressed data, replaced by crunkler
mov edi, base + 0x10000  ; Dest [keep in edi]
push edi ; Push dest address for jumping to when we have finished decompression.
inc byte [edi] ; Set first output byte to 1.
mov dword [ebp + v_archive], ebp ; Current input pointer.

xor ecx, ecx
mov cl, ccount
mov dl, 9
.nextCounter:
mov byte [ebp + v_counters + ecx * 4 - 4 + 3], dl ; Address is 0x09000000 (and then 0x0a, 0x0b, 0x0c, ...)
mov byte [ebp + v_cp + ecx * 4 - 4 + 3], dl
inc dl
loop .nextCounter

mov [ebp + v_x1], ecx ; x1 = 0
dec ecx
mov [ebp + v_x2], ecx ; x2 = 0xffffffff
inc ecx

.iterate:
xor eax, eax
xor edx, edx
inc edx ; n0 = 1 [edx]
lea ebx, [2 * edx] ;  n0 + n1 = 2 [ebx]
mov cl, ccount
.nextweight:
mov esi, [ebp + v_cp + ecx * 4 - 4]
lodsb
mul byte [ebp + v_weights + ecx - 1]
add edx, eax ; n0
add ebx, eax ; n0 + n1
lodsb
mul byte [ebp + v_weights + ecx - 1]
add ebx, eax ; n0 + n1
loop .nextweight

; u32 xmid = x1 + n0 * (u64)(x2 - x1) / (n0 + n1);
mov eax, edx ; n0 [eax]
mov edx, [ebp + v_x2]
sub edx, [ebp + v_x1] ; (x2 - x1) [edx]
mul edx
div ebx
add eax, [ebp + v_x1] ; xmid [eax]

xor edx, edx
mov esi, [ebp + v_archive] ; archive
cmp eax, [esi] ; Compare *(u32*)archive with xmid
jb .isone ; The comparison sets the carry flag the way we want to, set if our bit is a one, clear otherwise. jb jumps on carry as desired (JB = JC).
mov [ebp + v_x2], eax
jmp .iszero
.isone:
inc eax
mov [ebp + v_x1], eax
inc edx
.iszero:

rcl byte[edi], 1 ; Shift top bit of edi into carry, shift in carry (which contains the bit we want to add).
jnc .notyet ; If we shifted out a one, the byte is complete, move on to next byte.
inc edi
inc byte [edi]
.notyet:

mov cl, ccount
.nextmodel:
mov esi, [ebp + v_cp + ecx * 4 - 4]
add esi, edx ; Select counter matching our bit
add byte[esi], 1 ; Increment counter by one
sbb byte[esi], 0 ; If we overflow, return it to 255
xor esi, 1 ; Select the other counter.
cmp byte [esi], 2
jb .noadjust
shr byte [esi], 1
inc byte [esi]
.noadjust:
xor eax, eax
mov bl, 1 ; Mask bit
mov bh, [ebp + v_contexts + ecx - 1]
.nextcontextbyte:
test bh, bl
jz .notused
shl eax, 8
mov al, byte [edi]
.notused:
dec edi
shl bl, 1
jnz .nextcontextbyte
add edi, 8
mov esi, [ebp + v_counters + ecx * 4 - 4]
.nextval:
cmp dword [esi], eax
je .foundentry
cmp dword [esi], 0
je .foundentry
add esi, 6
jmp .nextval
.foundentry:
mov dword [esi], eax
add esi, 4
mov [ebp + v_cp + ecx * 4 - 4], esi
loop .nextmodel

.killbits:
mov edx, [ebp + v_x1]
xor edx, [ebp + v_x2]
shr edx, 24
jnz .donekilling
shl dword [ebp + v_x1], 8
shl dword [ebp + v_x2], 8
dec byte [ebp + v_x2]
dec dword [ebp + v_archive]
jmp .killbits
.donekilling:
dec dword [ebp + v_osize]  ; Size
jnz .iterate
ret

db'XXXX-Compressed code here-XXXX'

; The code that comes after here is actually compressed. It contains the dynamic linker.

compentry:
mov ebp, base + 0x10000 + .hashes - compentry + 1
.nexthash:
mov ebx, base + debug  ; DT_DEBUG value offset
mov ebx, [ebx] ; Load debug table offset
mov ebx, [ebx + 4] ; Load pointer to first library (r_map), ebx points to library structure.
xor eax, eax ; al is used for loading chars in function name, we need upper bits to be zero for hashing.
.nextlib:
mov esi, [ebx + 4] ; Point esi to library name
or byte [esi], al ; If library name is empty, we skip it. Weird things live there.
jz .nameless

mov ecx, [ebx + 8] ; l_ld, use ecx for walking through dynamic table
.nt:
add ecx, 8 ; Assume that the dynamic table never starts off with DT_STRTAB. This is true for all libraries I looked at.
cmp byte [ecx], 5 ; DT_STRTAB
jne .nt

.found:
mov edx, [ecx + 4] ; String base [edx]
mov ecx, [ecx + 12] ; Symbols [ecx]. Assume that this immediately follows DT_STRTAB
.nsym:
mov esi, [ecx]  ; Read symbol name offset (st_name)
add esi, edx ; Add string table offset.
xor edi, edi  ; Hash = 0
.nchar:
lodsb ; Read char from symbol name.
xor edi, eax 
rol edi, 5
test al, al ; Test if null terminator
jnz .nchar
cmp edi, dword [ebp] ; Hash of import.
je .hashfound

.nf:                               
add ecx, 16
cmp ecx, edx
jb .nsym

.nameless:
mov ebx, [ebx + 12] ; l_next
jmp .nextlib ; We don't know how to handle symbols that can't be found. Just continuing will cause us to crash.

.hashfound:
mov edi, [ecx + 4]  ; st_value
test edi, edi ; Test if zero
jz .nf ; And reject any symbols with zero value.
add edi, [ebx] ; l_addr
sub edi, ebp
sub edi, 4
mov [ebp], edi
add ebp, 5  
cmp dword [ebp], 0
jnz .nexthash
jmp 0x12345678 ; Jump to real entry point.

.hashes:
; The hash table is placed here.

