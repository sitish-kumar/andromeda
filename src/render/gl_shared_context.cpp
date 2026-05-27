#include "render/gl_shared_context.h"

#include "core/log.h"

#include <format>
#include <stdexcept>

namespace {

  constexpr Logger kLog("gl");

  constexpr EGLint kConfigAttributes[] = {
      EGL_SURFACE_TYPE,
      EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      8,
      EGL_NONE,
  };

  constexpr EGLint kContextAttributes[] = {
      EGL_CONTEXT_CLIENT_VERSION,
      2,
      EGL_NONE,
  };

} // namespace

GlSharedContext::~GlSharedContext() { cleanup(); }

void GlSharedContext::initialize(wl_display* display) {
  if (display == nullptr) {
    throw std::runtime_error("GlSharedContext requires a valid Wayland display");
  }

  m_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display));
  if (m_display == EGL_NO_DISPLAY) {
    throw std::runtime_error("eglGetDisplay failed");
  }

  EGLint major = 0;
  EGLint minor = 0;
  if (eglInitialize(m_display, &major, &minor) != EGL_TRUE) {
    throw std::runtime_error("eglInitialize failed");
  }

  if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
    throw std::runtime_error("eglBindAPI failed");
  }

  EGLint configCount = 0;
  if (eglChooseConfig(m_display, kConfigAttributes, &m_config, 1, &configCount) != EGL_TRUE || configCount != 1) {
    throw std::runtime_error("eglChooseConfig failed");
  }

  m_rootContext = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, kContextAttributes);
  if (m_rootContext == EGL_NO_CONTEXT) {
    throw std::runtime_error("eglCreateContext (root) failed");
  }

  constexpr EGLint kPbufferAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  m_pbuffer = eglCreatePbufferSurface(m_display, m_config, kPbufferAttributes);
  if (m_pbuffer != EGL_NO_SURFACE) {
    kLog.info("initialized EGL {}.{} with shared root context (pbuffer)", major, minor);
  } else {
    kLog.info("initialized EGL {}.{} with shared root context (surfaceless)", major, minor);
  }
}

void GlSharedContext::makeCurrentSurfaceless() const {
  if (m_display == EGL_NO_DISPLAY || m_rootContext == EGL_NO_CONTEXT) {
    return;
  }
  EGLSurface draw = m_pbuffer != EGL_NO_SURFACE ? m_pbuffer : EGL_NO_SURFACE;
  if (eglMakeCurrent(m_display, draw, draw, m_rootContext) != EGL_TRUE) {
    throw std::runtime_error(
        std::format("eglMakeCurrent (root) failed (EGL error 0x{:04x})", static_cast<unsigned>(eglGetError()))
    );
  }
}

void GlSharedContext::cleanup() {
  if (m_display == EGL_NO_DISPLAY) {
    return;
  }

  eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (m_pbuffer != EGL_NO_SURFACE) {
    eglDestroySurface(m_display, m_pbuffer);
    m_pbuffer = EGL_NO_SURFACE;
  }

  if (m_rootContext != EGL_NO_CONTEXT) {
    eglDestroyContext(m_display, m_rootContext);
    m_rootContext = EGL_NO_CONTEXT;
  }

  eglTerminate(m_display);
  m_display = EGL_NO_DISPLAY;
  m_config = nullptr;
}
