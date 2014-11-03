// Port of flow2 to be compiled and linked by dnload.py, platform-independent.
// Both original comments and those from Amand Tihon copied verbatim.

// This has been ported back to not requiring dnload.py, so that it can be linked
// by elfling.

// Linux x86_64 port of flow2, to be linked with the bold linker.
// Ported by Amand Tihon (alrj).
// Original comments in flow2 source code copied verbatim

// Chris Thornborrow (auld/sek/taj/copabars)
// If you use this code please credit...blahblah
// Example OGL + shaders in 1k
// Requires crinkler - magnificent tool
// VS2005 modifications by benny!weltenkonstrukteur.de from dbf
//    Greets!
// NOTE: DX will beat this no problem at all due to OpenGL forced
// to import shader calls as variables..nontheless we dont need
// d3dxblahblah to be loaded on users machine.

#include <SDL/SDL.h>
#include <GL/gl.h>

// NOTE: in glsl it is legal to have a fragment shader without a vertex shader
//  Infact ATi/AMD  drivers allow this but unwisely forget to set up variables for
// the fragment shader - thus all GLSL programs must have a vertex shader :-(
// Thanks ATI/AMD

// This is pretty dirty...note we do not transform the rectangle but we do use
// glRotatef to pass in a value we can use to animate...avoids one more getProcAddress later
const GLchar *vsh="\
varying vec4 p;\
void main(){\
p=sin(gl_ModelViewMatrix[1]*9.0);\
gl_Position=gl_Vertex;\
}";

// an iterative function for colour
const GLchar *fsh="\
varying vec4 p;\
void main(){\
float r,t,j;\
vec4 v=gl_FragCoord/400.0-1.0;\
r=v.x*p.r;\
for(int j=0;j<7;j++){\
t=v.x+p.r*p.g;\
v.x=t*t-v.y*v.y+r;\
v.y=p.g*3.0*t*v.y+v.y;\
}\
gl_FragColor=vec4(mix(p,vec4(t),max(t,v.x)));\
}";
//p.g*3.0*t*v.y+i;\

static void setShaders() {

  GLuint v = glCreateShader(GL_VERTEX_SHADER);
  GLuint f = glCreateShader(GL_FRAGMENT_SHADER);
  GLuint p = glCreateProgram();

  glShaderSource(v, 1, &vsh, NULL);
  glCompileShader(v);
  glAttachShader(p, v);
  glShaderSource(f, 1, &fsh, NULL);
  glCompileShader(f);
  glAttachShader(p, f);
  glLinkProgram(p);
  glUseProgram(p);
}

void _start()
{
  static SDL_Event e;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_SetVideoMode(1024, 768, 0, SDL_OPENGL);

  SDL_ShowCursor(SDL_DISABLE);
  setShaders();
   //**********************
   // NOW THE MAIN LOOP...
   //**********************
   // there is no depth test or clear screen...as we draw in order and cover
   // the whole area of the screen.
  do
  {
    glRotatef(0.3f, 1, 1, 1);
    glRecti(-1, -1, 1, 1);
    SDL_GL_SwapBuffers();
    
    SDL_PollEvent(&e);
    
    if (e.type == SDL_KEYDOWN)
      break;
  } while (e.type != SDL_QUIT);

  __asm ("mov $1, %eax\n"
    "xor %ebx, %ebx\n"
    "int $0x80");
}
