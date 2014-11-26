all: bin/prt bin/prt2

bin:
	mkdir bin

bin/bin2h: bin bin2h.c
	gcc -std=c99 bin2h.c -o bin/bin2h 
	
header32.h: header.asm bin/bin2h
	nasm header.asm -f bin -o bin/header32.bin
	bin/bin2h bin/header32.bin header32.h header32
	ls -al bin/header32.bin

header64.h: header64.asm bin/bin2h
	nasm header64.asm -f bin -o bin/header64.bin
	bin/bin2h bin/header64.bin header64.h header64
	ls -al bin/header64.bin

bin/elfling: elfling.cpp header32.h header64.h pack.cpp unpack.cpp pack.h
	gcc -std=c++11 -O3 -g pack.cpp -c -o bin/pack.o
	gcc -std=c++11 -O3 -g unpack.cpp -c -o bin/unpack.o
	g++ -std=c++11 -g elfling.cpp bin/pack.o bin/unpack.o -o bin/elfling

bin/crunkler_2: crunkler_2.cpp
	g++ -std=c++11 -g crunkler_2.cpp  -o bin/crunkler_2
	
bin/prt.o: prt.c bin
	gcc -Os -c prt.c -fomit-frame-pointer -fno-exceptions -o bin/prt.o -m32

bin/prt: bin/prt.o
	gcc bin/prt.o -s  -nostartfiles -o bin/prt -m32
	
bin/prt2: bin/prt.o bin/elfling
	bin/elfling bin/prt.o -obin/prt2  -llibc.so.6 #-fnosh
	chmod 755 bin/prt2

bin/prt_64.o: prt.c bin
	gcc -Os -c prt.c -fomit-frame-pointer -fno-exceptions -o bin/prt_64.o

bin/prt_64: bin/prt_64.o
	gcc bin/prt_64.o -s  -nostartfiles -o bin/prt_64
	
bin/prt2_64: bin/prt_64.o bin/elfling
	bin/elfling bin/prt_64.o -obin/prt2_64  -llibc.so.6 #-fnosh
	chmod 755 bin/prt2_64

bin/flow2: flow2.c bin/elfling
	gcc -Os -c flow2.c -fomit-frame-pointer -fno-exceptions -ffast-math -fsingle-precision-constant -o bin/flow2.o -m32
	bin/elfling bin/flow2.o -obin/flow2  -llibSDL-1.2.so.0 -llibGL.so.1 -c0801010205070b0b0734273ac30403158d
	chmod 755 bin/flow2

bin/flow2_64: flow2.c bin/elfling
	gcc -Os -c flow2.c -fomit-frame-pointer -fno-exceptions -ffast-math -fsingle-precision-constant -o bin/flow2_64.o
	bin/elfling bin/flow2_64.o -obin/flow2_64  -llibSDL-1.2.so.0 -llibGL.so.1 -c080101020503070bc303c9361704030a2b
	chmod 755 bin/flow2_64

bin/flow_64: flow2.c
	gcc -Os -c flow2.c -fomit-frame-pointer -fno-exceptions -ffast-math -fsingle-precision-constant -o bin/flow2_64.o
	gcc bin/flow2_64.o -s  -nostartfiles -o bin/flow_64 -lGL -lSDL

bin/packer: packer.cpp unpack.cpp pack.cpp pack.h
	g++ -std=c++11 -g packer.cpp -c -o bin/packer.o -m32
	gcc -std=c++11 -g unpack.cpp -c -o bin/unpack.o -m32
	gcc -std=c++11 -O3 -g pack.cpp -c -o bin/pack.o -m32
	g++ -std=c++11 -g bin/packer.o bin/pack.o bin/unpack.o -o bin/packer -m32
	
packtest: bin/packer bin/prt
	bin/packer bin/prt
	bin/packer bin/prt.pack
	diff bin/prt bin/prt.unpack

