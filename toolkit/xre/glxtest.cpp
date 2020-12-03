/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//////////////////////////////////////////////////////////////////////////////
//
// Explanation: See bug 639842. Safely getting GL driver info on X11 is hard,
// because the only way to do that is to create a GL context and call
// glGetString(), but with bad drivers, just creating a GL context may crash.
//
// This file implements the idea to do that in a separate process.
//
// The only non-static function here is fire_glxtest_process(). It creates a
// pipe, publishes its 'read' end as the mozilla::widget::glxtest_pipe global
// variable, forks, and runs that GLX probe in the child process, which runs the
// childgltest() static function. This creates a X connection, a GLX context,
// calls glGetString, and writes that to the 'write' end of the pipe.

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <dlfcn.h>
#include "nscore.h"
#include <fcntl.h>
#include "stdint.h"

#ifdef __SUNPRO_CC
#  include <stdio.h>
#endif

#include "X11/Xlib.h"
#include "X11/Xutil.h"

#include "mozilla/Unused.h"

#ifdef MOZ_WAYLAND
#  include "nsAppRunner.h"  // for IsWaylandDisabled
#  include "mozilla/widget/mozwayland.h"
#endif

// stuff from glx.h
typedef struct __GLXcontextRec* GLXContext;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
/* GLX 1.3 and later */
typedef struct __GLXFBConfigRec* GLXFBConfig;
typedef XID GLXFBConfigID;
typedef XID GLXContextID;
typedef XID GLXWindow;
typedef XID GLXPbuffer;
#define GLX_RGBA 4
#define GLX_RED_SIZE 8
#define GLX_GREEN_SIZE 9
#define GLX_BLUE_SIZE 10

// stuff from gl.h
typedef uint8_t GLubyte;
typedef uint32_t GLenum;
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

// GLX_MESA_query_renderer
// clang-format off
#define GLX_RENDERER_VENDOR_ID_MESA                            0x8183
#define GLX_RENDERER_DEVICE_ID_MESA                            0x8184
#define GLX_RENDERER_VERSION_MESA                              0x8185
#define GLX_RENDERER_ACCELERATED_MESA                          0x8186
#define GLX_RENDERER_VIDEO_MEMORY_MESA                         0x8187
#define GLX_RENDERER_UNIFIED_MEMORY_ARCHITECTURE_MESA          0x8188
#define GLX_RENDERER_PREFERRED_PROFILE_MESA                    0x8189
#define GLX_RENDERER_OPENGL_CORE_PROFILE_VERSION_MESA          0x818A
#define GLX_RENDERER_OPENGL_COMPATIBILITY_PROFILE_VERSION_MESA 0x818B
#define GLX_RENDERER_OPENGL_ES_PROFILE_VERSION_MESA            0x818C
#define GLX_RENDERER_OPENGL_ES2_PROFILE_VERSION_MESA           0x818D
#define GLX_RENDERER_ID_MESA                                   0x818E
// clang-format on

// stuff from egl.h
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_NONE 0x3038
#define EGL_VENDOR 0x3053
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_NO_CONTEXT nullptr

#define EXIT_FAILURE_BUFFER_TOO_SMALL 2

namespace mozilla {
namespace widget {
// the read end of the pipe, which will be used by GfxInfo
extern int glxtest_pipe;
// the PID of the glxtest process, to pass to waitpid()
extern pid_t glxtest_pid;
}  // namespace widget
}  // namespace mozilla

// the write end of the pipe, which we're going to write to
static int write_end_of_the_pipe = -1;

// our buffer, size and used length
static char* glxtest_buf = nullptr;
static int glxtest_bufsize = 0;
static int glxtest_length = 0;

// C++ standard collides with C standard in that it doesn't allow casting void*
// to function pointer types. So the work-around is to convert first to size_t.
// http://www.trilithium.com/johan/2004/12/problem-with-dlsym/
template <typename func_ptr_type>
static func_ptr_type cast(void* ptr) {
  return reinterpret_cast<func_ptr_type>(reinterpret_cast<size_t>(ptr));
}

static void record_value(const char* format, ...) {
  // Don't add more if the buffer is full.
  if (glxtest_bufsize <= glxtest_length) {
    return;
  }

  // Append the new values to the buffer, not to exceed the remaining space.
  int remaining = glxtest_bufsize - glxtest_length;
  va_list args;
  va_start(args, format);
  int max_added =
      vsnprintf(glxtest_buf + glxtest_length, remaining, format, args);
  va_end(args);

  // snprintf returns how many char it could have added, not how many it added.
  // It is important to get this right since it will control how many chars we
  // will attempt to write to the pipe fd.
  if (max_added > remaining) {
    glxtest_length += remaining;
  } else {
    glxtest_length += max_added;
  }
}

static void record_error(const char* str) { record_value("ERROR\n%s\n", str); }

static void record_warning(const char* str) {
  record_value("WARNING\n%s\n", str);
}

static void record_flush() {
  mozilla::Unused << write(write_end_of_the_pipe, glxtest_buf, glxtest_length);
}

static int x_error_handler(Display*, XErrorEvent* ev) {
  record_value(
      "ERROR\nX error, error_code=%d, "
      "request_code=%d, minor_code=%d\n",
      ev->error_code, ev->request_code, ev->minor_code);
  record_flush();
  _exit(EXIT_FAILURE);
  return 0;
}

// childgltest is declared inside extern "C" so that the name is not mangled.
// The name is used in build/valgrind/x86_64-pc-linux-gnu.sup to suppress
// memory leak errors because we run it inside a short lived fork and we don't
// care about leaking memory
extern "C" {

#define PCI_FILL_IDENT 0x0001
#define PCI_FILL_CLASS 0x0020
#define PCI_BASE_CLASS_DISPLAY 0x03

static void get_pci_status() {
  void* libpci = dlopen("libpci.so.3", RTLD_LAZY);
  if (!libpci) {
    libpci = dlopen("libpci.so", RTLD_LAZY);
  }
  if (!libpci) {
    record_warning("libpci missing");
    return;
  }

  typedef struct pci_dev {
    struct pci_dev* next;
    uint16_t domain_16;
    uint8_t bus, dev, func;
    unsigned int known_fields;
    uint16_t vendor_id, device_id;
    uint16_t device_class;
  } pci_dev;

  typedef struct pci_access {
    unsigned int method;
    int writeable;
    int buscentric;
    char* id_file_name;
    int free_id_name;
    int numeric_ids;
    unsigned int id_lookup_mode;
    int debugging;
    void* error;
    void* warning;
    void* debug;
    pci_dev* devices;
  } pci_access;

  typedef pci_access* (*PCIALLOC)(void);
  PCIALLOC pci_alloc = cast<PCIALLOC>(dlsym(libpci, "pci_alloc"));

  typedef void (*PCIINIT)(pci_access*);
  PCIINIT pci_init = cast<PCIINIT>(dlsym(libpci, "pci_init"));

  typedef void (*PCICLEANUP)(pci_access*);
  PCICLEANUP pci_cleanup = cast<PCICLEANUP>(dlsym(libpci, "pci_cleanup"));

  typedef void (*PCISCANBUS)(pci_access*);
  PCISCANBUS pci_scan_bus = cast<PCISCANBUS>(dlsym(libpci, "pci_scan_bus"));

  typedef void (*PCIFILLINFO)(pci_dev*, int);
  PCIFILLINFO pci_fill_info = cast<PCIFILLINFO>(dlsym(libpci, "pci_fill_info"));

  if (!pci_alloc || !pci_cleanup || !pci_scan_bus || !pci_fill_info) {
    dlclose(libpci);
    record_warning("libpci missing methods");
    return;
  }

  pci_access* pacc = pci_alloc();
  if (!pacc) {
    dlclose(libpci);
    record_warning("libpci alloc failed");
    return;
  }

  pci_init(pacc);
  pci_scan_bus(pacc);

  for (pci_dev* dev = pacc->devices; dev; dev = dev->next) {
    pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_CLASS);
    if (dev->device_class >> 8 == PCI_BASE_CLASS_DISPLAY && dev->vendor_id &&
        dev->device_id) {
      record_value("PCI_VENDOR_ID\n0x%04x\nPCI_DEVICE_ID\n0x%04x\n",
                   dev->vendor_id, dev->device_id);
    }
  }

  pci_cleanup(pacc);
  dlclose(libpci);
}

typedef void* EGLNativeDisplayType;
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef int EGLBoolean;
typedef int EGLint;
typedef void* (*PFNEGLGETPROCADDRESS)(const char*);
typedef EGLDisplay (*PFNEGLGETDISPLAYPROC)(void* native_display);
typedef EGLBoolean (*PFNEGLINITIALIZEPROC)(EGLDisplay dpy, EGLint* major,
                                           EGLint* minor);
typedef EGLBoolean (*PFNEGLTERMINATEPROC)(EGLDisplay dpy);
typedef EGLBoolean (*PFNEGLCHOOSECONFIGPROC)(EGLDisplay dpy,
                                             EGLint const* attrib_list,
                                             EGLConfig* configs,
                                             EGLint config_size,
                                             EGLint* num_config);
typedef EGLContext (*PFNEGLCREATECONTEXTPROC)(EGLDisplay dpy, EGLConfig config,
                                              EGLContext share_context,
                                              EGLint const* attrib_list);
typedef EGLSurface (*PFNEGLCREATEPBUFFERSURFACEPROC)(EGLDisplay dpy,
                                                     EGLConfig config,
                                                     EGLint const* attrib_list);
typedef EGLBoolean (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy, EGLSurface draw,
                                            EGLSurface read,
                                            EGLContext context);
typedef const char* (*PFNEGLGETDISPLAYDRIVERNAMEPROC)(EGLDisplay dpy);
typedef GLubyte* (*PFNGLGETSTRING)(GLenum);

typedef struct TestLibEGL {
  void* lib;
  int valid;
  PFNEGLGETPROCADDRESS GetProcAddress;
  PFNEGLGETDISPLAYPROC GetDisplay;
  PFNEGLINITIALIZEPROC Initialize;
  PFNEGLTERMINATEPROC Terminate;
  PFNEGLCHOOSECONFIGPROC ChooseConfig;
  PFNEGLCREATECONTEXTPROC CreateContext;
  PFNEGLCREATEPBUFFERSURFACEPROC CreatePbufferSurface;
  PFNEGLMAKECURRENTPROC MakeCurrent;
  PFNEGLGETDISPLAYDRIVERNAMEPROC GetDisplayDriverName;
  PFNGLGETSTRING GetString;
} TestLibEGL;

static void init_egl(TestLibEGL* egl) {
  memset(egl, 0, sizeof(TestLibEGL));

  egl->lib = dlopen("libEGL.so.1", RTLD_LAZY);
  if (!egl->lib) {
    egl->lib = dlopen("libEGL.so", RTLD_LAZY);
  }
  if (!egl->lib) {
    record_warning("libEGL missing");
    return;
  }

  egl->GetProcAddress =
      cast<PFNEGLGETPROCADDRESS>(dlsym(egl->lib, "eglGetProcAddress"));
  if (!egl->GetProcAddress) {
    record_warning("no eglGetProcAddress");
    return;
  }

  egl->GetDisplay =
      cast<PFNEGLGETDISPLAYPROC>(egl->GetProcAddress("eglGetDisplay"));
  egl->Initialize =
      cast<PFNEGLINITIALIZEPROC>(egl->GetProcAddress("eglInitialize"));
  egl->Terminate =
      cast<PFNEGLTERMINATEPROC>(egl->GetProcAddress("eglTerminate"));
  if (!egl->GetDisplay || !egl->Initialize || !egl->Terminate) {
    record_warning("libEGL missing base methods");
    return;
  }

  egl->ChooseConfig =
      cast<PFNEGLCHOOSECONFIGPROC>(egl->GetProcAddress("eglChooseConfig"));
  egl->CreateContext =
      cast<PFNEGLCREATECONTEXTPROC>(egl->GetProcAddress("eglCreateContext"));
  egl->CreatePbufferSurface = cast<PFNEGLCREATEPBUFFERSURFACEPROC>(
      egl->GetProcAddress("eglCreatePbufferSurface"));
  egl->MakeCurrent =
      cast<PFNEGLMAKECURRENTPROC>(egl->GetProcAddress("eglMakeCurrent"));

  if (!egl->ChooseConfig || !egl->CreateContext || !egl->CreatePbufferSurface ||
      !egl->MakeCurrent) {
    record_warning("libEGL missing methods for GLES test");
    return;
  }

  // This is optional and accessed during gles initialization.
  egl->GetString = cast<PFNGLGETSTRING>(egl->GetProcAddress("glGetString"));

  // This is optional and used to get the driver name, if possible.
  egl->GetDisplayDriverName = cast<PFNEGLGETDISPLAYDRIVERNAMEPROC>(
      egl->GetProcAddress("eglGetDisplayDriverName"));

  egl->valid = 1;
}

typedef struct TestLibGLES {
  void* lib;
  int valid;
  PFNGLGETSTRING GetString;
} TestLibGLES;

static void init_gles(TestLibEGL* egl, TestLibGLES* gles) {
  memset(gles, 0, sizeof(TestLibGLES));

  gles->lib = dlopen("libGLESv2.so.2", RTLD_LAZY);
  if (!gles->lib) {
    gles->lib = dlopen("libGLESv2.so", RTLD_LAZY);
  }
  if (!gles->lib) {
    record_warning("libGLESv2 missing");
    return;
  }

  if (egl->valid && egl->GetString) {
    gles->GetString = egl->GetString;
  } else {
    // Implementations disagree about whether eglGetProcAddress or dlsym
    // should be used for getting functions from the actual API, see
    // https://github.com/anholt/libepoxy/commit/14f24485e33816139398d1bd170d617703473738
    gles->GetString = cast<PFNGLGETSTRING>(dlsym(gles->lib, "glGetString"));
  }

  if (!gles->GetString) {
    record_error("libGLESv2 glGetString missing");
    return;
  }

  gles->valid = 1;
}

typedef void* (*PFNGLXGETPROCADDRESS)(const char*);
typedef GLXFBConfig* (*PFNGLXQUERYEXTENSION)(Display*, int*, int*);
typedef GLXFBConfig* (*PFNGLXQUERYVERSION)(Display*, int*, int*);
typedef XVisualInfo* (*PFNGLXCHOOSEVISUAL)(Display*, int, int*);
typedef GLXContext (*PFNGLXCREATECONTEXT)(Display*, XVisualInfo*, GLXContext,
                                          Bool);
typedef Bool (*PFNGLXMAKECURRENT)(Display*, GLXDrawable, GLXContext);
typedef void (*PFNGLXDESTROYCONTEXT)(Display*, GLXContext);
typedef Bool (*PFNGLXQUERYCURRENTRENDERERINTEGERMESAPROC)(int attribute,
                                                          unsigned int* value);
typedef const char* (*PFNGLXGETSCREENDRIVERPROC)(Display* dpy, int scrNum);

typedef struct TestLibGLX {
  void* lib;
  int valid;
  PFNGLXGETPROCADDRESS GetProcAddress;
  PFNGLXQUERYEXTENSION QueryExtension;
  PFNGLXQUERYVERSION QueryVersion;
  PFNGLXCHOOSEVISUAL ChooseVisual;
  PFNGLXCREATECONTEXT CreateContext;
  PFNGLXMAKECURRENT MakeCurrent;
  PFNGLXDESTROYCONTEXT DestroyContext;
  PFNGLGETSTRING GetString;
  PFNGLXQUERYCURRENTRENDERERINTEGERMESAPROC QueryCurrentRendererIntegerMESAProc;
  PFNGLXGETSCREENDRIVERPROC GetScreenDriverProc;
} TestLibGLX;

static void init_glx(TestLibGLX* glx) {
  memset(glx, 0, sizeof(TestLibGLX));

  ///// Open libGL and load needed symbols /////
#if defined(__OpenBSD__) || defined(__NetBSD__)
#  define LIBGL_FILENAME "libGL.so"
#else
#  define LIBGL_FILENAME "libGL.so.1"
#endif
  glx->lib = dlopen(LIBGL_FILENAME, RTLD_LAZY);
  if (!glx->lib) {
    record_warning(LIBGL_FILENAME " missing");
    return;
  }

  glx->GetProcAddress =
      cast<PFNGLXGETPROCADDRESS>(dlsym(glx->lib, "glXGetProcAddress"));
  if (!glx->GetProcAddress) {
    record_warning("no glXGetProcAddress");
    return;
  }

  glx->QueryExtension =
      cast<PFNGLXQUERYEXTENSION>(glx->GetProcAddress("glXQueryExtension"));
  glx->QueryVersion =
      cast<PFNGLXQUERYVERSION>(dlsym(glx->lib, "glXQueryVersion"));
  glx->ChooseVisual =
      cast<PFNGLXCHOOSEVISUAL>(glx->GetProcAddress("glXChooseVisual"));
  glx->CreateContext =
      cast<PFNGLXCREATECONTEXT>(glx->GetProcAddress("glXCreateContext"));
  glx->MakeCurrent =
      cast<PFNGLXMAKECURRENT>(glx->GetProcAddress("glXMakeCurrent"));
  glx->DestroyContext =
      cast<PFNGLXDESTROYCONTEXT>(glx->GetProcAddress("glXDestroyContext"));
  glx->GetString = cast<PFNGLGETSTRING>(glx->GetProcAddress("glGetString"));

  if (!glx->QueryExtension || !glx->QueryVersion || !glx->ChooseVisual ||
      !glx->CreateContext || !glx->MakeCurrent || !glx->DestroyContext ||
      !glx->GetString) {
    record_warning(LIBGL_FILENAME " missing base methods");
    return;
  }

  glx->QueryCurrentRendererIntegerMESAProc =
      cast<PFNGLXQUERYCURRENTRENDERERINTEGERMESAPROC>(
          glx->GetProcAddress("glXQueryCurrentRendererIntegerMESA"));
  glx->GetScreenDriverProc = cast<PFNGLXGETSCREENDRIVERPROC>(
      glx->GetProcAddress("glXGetScreenDriver"));

  glx->valid = 1;
}

static void get_gles_status(TestLibEGL* egl, TestLibGLES* gles,
                            EGLDisplay dpy) {
  if (!egl || !egl->valid || !gles || !gles->valid) {
    return;
  }

  EGLint config_attrs[] = {EGL_RED_SIZE,  8, EGL_GREEN_SIZE, 8,
                           EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint num_config;
  egl->ChooseConfig(dpy, config_attrs, &config, 1, &num_config);
  EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext ectx = egl->CreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attrs);
  EGLSurface pbuf = egl->CreatePbufferSurface(dpy, config, nullptr);
  egl->MakeCurrent(dpy, pbuf, pbuf, ectx);

  const GLubyte* versionString = gles->GetString(GL_VERSION);
  const GLubyte* vendorString = gles->GetString(GL_VENDOR);
  const GLubyte* rendererString = gles->GetString(GL_RENDERER);

  if (versionString && vendorString && rendererString) {
    record_value("VENDOR\n%s\nRENDERER\n%s\nVERSION\n%s\nTFP\nTRUE\n",
                 vendorString, rendererString, versionString);
  } else {
    record_error("libGLESv2 glGetString returned null");
  }
}

static void get_egl_status(TestLibEGL* egl, TestLibGLES* gles,
                           EGLNativeDisplayType native_dpy) {
  if (!egl || !egl->valid) {
    return;
  }

  EGLDisplay dpy = egl->GetDisplay(native_dpy);
  if (!dpy) {
    record_warning("libEGL no display");
    return;
  }

  EGLint major, minor;
  if (!egl->Initialize(dpy, &major, &minor)) {
    record_warning("libEGL initialize failed");
    return;
  }

  if (gles) {
    get_gles_status(egl, gles, dpy);
  }

  if (egl->GetDisplayDriverName) {
    const char* driDriver = egl->GetDisplayDriverName(dpy);
    if (driDriver) {
      record_value("DRI_DRIVER\n%s\n", driDriver);
    }
  }

  egl->Terminate(dpy);
}

static void get_glx_status(TestLibGLX* glx, int* gotGlxInfo,
                           int* gotDriDriver) {
  if (!glx || !glx->valid) {
    return;
  }

  ///// Open a connection to the X server /////
  Display* dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    record_error("Unable to open a connection to the X server");
    return;
  }

  ///// Check that the GLX extension is present /////
  if (!glx->QueryExtension(dpy, nullptr, nullptr)) {
    record_error("GLX extension missing");
    return;
  }

  XSetErrorHandler(x_error_handler);

  ///// Get a visual /////
  int attribs[] = {GLX_RGBA, GLX_RED_SIZE,  1, GLX_GREEN_SIZE,
                   1,        GLX_BLUE_SIZE, 1, None};
  XVisualInfo* vInfo = glx->ChooseVisual(dpy, DefaultScreen(dpy), attribs);
  if (!vInfo) {
    record_error("No visuals found");
    return;
  }

  // using a X11 Window instead of a GLXPixmap does not crash
  // fglrx in indirect rendering. bug 680644
  Window window;
  XSetWindowAttributes swa;
  swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vInfo->screen),
                                 vInfo->visual, AllocNone);

  swa.border_pixel = 0;
  window = XCreateWindow(dpy, RootWindow(dpy, vInfo->screen), 0, 0, 16, 16, 0,
                         vInfo->depth, InputOutput, vInfo->visual,
                         CWBorderPixel | CWColormap, &swa);

  ///// Get a GL context and make it current //////
  GLXContext context = glx->CreateContext(dpy, vInfo, nullptr, True);
  glx->MakeCurrent(dpy, window, context);

  ///// Look for this symbol to determine texture_from_pixmap support /////
  void* glXBindTexImageEXT = glx->GetProcAddress("glXBindTexImageEXT");

  ///// Get GL vendor/renderer/versions strings /////
  const GLubyte* versionString = glx->GetString(GL_VERSION);
  const GLubyte* vendorString = glx->GetString(GL_VENDOR);
  const GLubyte* rendererString = glx->GetString(GL_RENDERER);

  if (versionString && vendorString && rendererString) {
    record_value("VENDOR\n%s\nRENDERER\n%s\nVERSION\n%s\nTFP\n%s\n",
                 vendorString, rendererString, versionString,
                 glXBindTexImageEXT ? "TRUE" : "FALSE");
    *gotGlxInfo = 1;
  } else {
    record_error("glGetString returned null");
  }

  // If GLX_MESA_query_renderer is available, populate additional data.
  if (glx->QueryCurrentRendererIntegerMESAProc) {
    unsigned int vendorId, deviceId, accelerated, videoMemoryMB;
    glx->QueryCurrentRendererIntegerMESAProc(GLX_RENDERER_VENDOR_ID_MESA,
                                             &vendorId);
    glx->QueryCurrentRendererIntegerMESAProc(GLX_RENDERER_DEVICE_ID_MESA,
                                             &deviceId);
    glx->QueryCurrentRendererIntegerMESAProc(GLX_RENDERER_ACCELERATED_MESA,
                                             &accelerated);
    glx->QueryCurrentRendererIntegerMESAProc(GLX_RENDERER_VIDEO_MEMORY_MESA,
                                             &videoMemoryMB);

    // Truncate IDs to 4 digits- that's all PCI IDs are.
    vendorId &= 0xFFFF;
    deviceId &= 0xFFFF;

    record_value(
        "MESA_VENDOR_ID\n0x%04x\n"
        "MESA_DEVICE_ID\n0x%04x\n"
        "MESA_ACCELERATED\n%s\n"
        "MESA_VRAM\n%dMB\n",
        vendorId, deviceId, accelerated ? "TRUE" : "FALSE", videoMemoryMB);
  }

  // From Mesa's GL/internal/dri_interface.h, to be used by DRI clients.
  if (glx->GetScreenDriverProc) {
    const char* driDriver = glx->GetScreenDriverProc(dpy, DefaultScreen(dpy));
    if (driDriver) {
      *gotDriDriver = 1;
      record_value("DRI_DRIVER\n%s\n", driDriver);
    }
  }

  // Get monitor information

  int screenCount = ScreenCount(dpy);
  int defaultScreen = DefaultScreen(dpy);
  if (screenCount != 0) {
    record_value("SCREEN_INFO\n");
    for (int idx = 0; idx < screenCount; idx++) {
      Screen* scrn = ScreenOfDisplay(dpy, idx);
      int current_height = scrn->height;
      int current_width = scrn->width;

      record_value("%dx%d:%d%s", current_width, current_height,
                   idx == defaultScreen ? 1 : 0,
                   idx == screenCount - 1 ? ";\n" : ";");
    }
  }

  ///// Clean up. Indeed, the parent process might fail to kill us (e.g. if it
  ///// doesn't need to check GL info) so we might be staying alive for longer
  ///// than expected, so it's important to consume as little memory as
  ///// possible. Also we want to check that we're able to do that too without
  ///// generating X errors.
  glx->MakeCurrent(
      dpy, None,
      nullptr);  // must release the GL context before destroying it
  glx->DestroyContext(dpy, context);
  XDestroyWindow(dpy, window);
  XFreeColormap(dpy, swa.colormap);

#ifdef NS_FREE_PERMANENT_DATA  // conditionally defined in nscore.h, don't
                               // forget to #include it above
  XCloseDisplay(dpy);
#else
  // This XSync call wanted to be instead:
  //   XCloseDisplay(dpy);
  // but this can cause 1-minute stalls on certain setups using Nouveau, see bug
  // 973192
  XSync(dpy, False);
#endif
}

static void close_logging() {
  // we want to redirect to /dev/null stdout, stderr, and while we're at it,
  // any PR logging file descriptors. To that effect, we redirect all positive
  // file descriptors up to what open() returns here. In particular, 1 is stdout
  // and 2 is stderr.
  int fd = open("/dev/null", O_WRONLY);
  for (int i = 1; i < fd; i++) dup2(fd, i);
  close(fd);

  if (getenv("MOZ_AVOID_OPENGL_ALTOGETHER")) {
    const char* msg = "ERROR\nMOZ_AVOID_OPENGL_ALTOGETHER envvar set";
    mozilla::Unused << write(write_end_of_the_pipe, msg, strlen(msg));
    exit(EXIT_FAILURE);
  }
}

#ifdef MOZ_WAYLAND
static bool wayland_egltest(TestLibEGL* egl, TestLibGLES* gles) {
  // NOTE: returns false to fall back to X11 when the Wayland socket doesn't
  // exist but fails with record_error if something actually went wrong
  struct wl_display* dpy = wl_display_connect(nullptr);
  if (!dpy) {
    return false;
  }

  get_egl_status(egl, gles, (EGLNativeDisplayType)dpy);
  return true;
}
#endif

static void glxtest(TestLibGLX* glx, TestLibEGL* egl, TestLibGLES* gles) {
  int gotGlxInfo = 0;
  int gotDriDriver = 0;

  get_glx_status(glx, &gotGlxInfo, &gotDriDriver);
  if (!gotGlxInfo) {
    get_egl_status(egl, gles, nullptr);
  } else if (!gotDriDriver) {
    // If we failed to get the driver name from X, try via
    // EGL_MESA_query_driver. We are probably using Wayland.
    get_egl_status(egl, nullptr, nullptr);
  }
}

int childgltest() {
  enum { bufsize = 2048 };
  char buf[bufsize];

  // We save it as a global so that the X error handler can flush the buffer
  // before early exiting.
  glxtest_buf = buf;
  glxtest_bufsize = bufsize;

  // Get a list of all GPUs from the PCI bus.
  get_pci_status();

  // Attempt to load each of the libraries to verify they are available and have
  // all of the expected methods.
  TestLibEGL egl;
  init_egl(&egl);
  record_value("EGL_VALID\n%s\n", egl.valid ? "TRUE" : "FALSE");

  TestLibGLES gles;
  init_gles(&egl, &gles);
  record_value("GLES_VALID\n%s\n", gles.valid ? "TRUE" : "FALSE");

  TestLibGLX glx;
  init_glx(&glx);
  record_value("GLX_VALID\n%s\n", glx.valid ? "TRUE" : "FALSE");

  // TODO: --display command line argument is not properly handled
  // NOTE: prefers X for now because eglQueryRendererIntegerMESA does not
  // exist yet
#ifdef MOZ_WAYLAND
  if (IsWaylandDisabled() || getenv("DISPLAY") || !wayland_egltest(&egl, &gles))
#endif
  {
    glxtest(&glx, &egl, &gles);
  }

  // Finally write buffered data to the pipe.
  record_flush();

  // If we completely filled the buffer, we need to tell the parent.
  if (glxtest_length >= glxtest_bufsize) {
    return EXIT_FAILURE_BUFFER_TOO_SMALL;
  }

  return EXIT_SUCCESS;
}

}  // extern "C"

/** \returns true in the child glxtest process, false in the parent process */
bool fire_glxtest_process() {
  int pfd[2];
  if (pipe(pfd) == -1) {
    perror("pipe");
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    close(pfd[0]);
    close(pfd[1]);
    return false;
  }
  // The child exits early to avoid running the full shutdown sequence and avoid
  // conflicting with threads we have already spawned (like the profiler).
  if (pid == 0) {
    close(pfd[0]);
    write_end_of_the_pipe = pfd[1];
    close_logging();
    int rv = childgltest();
    close(pfd[1]);
    _exit(rv);
  }

  close(pfd[1]);
  mozilla::widget::glxtest_pipe = pfd[0];
  mozilla::widget::glxtest_pid = pid;
  return false;
}
