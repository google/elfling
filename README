Elfling by Minas ^ Calodox

This is an attempt at writing something Crinkler-like for the Linux Scene. It
provides a context-modeling compressing linker that will transform a .o file
into a "valid" ELF binary. This is not quite finished yet, the following
caveats should be taken into account when using:
- It can only link a single .o file for now.
- It assumes that you want to link SDL 1.2 and OpenGL, the flags for specifying
  libraries are currently ignored.
- It may crash if your object file contains some construct it does not expect.
  While I have tested quite a few relocation and data declaration possibilities
  I am absolutely certain to have missed a few :-)
- 32-bit only. While the resulting binaries will run on 64-bit Linux, users will
  need to install the required 32-bit libraries for running your stuff.
  
Have fun :-) Improvements welcome, this is open source after all!

This is not an official Google product.
