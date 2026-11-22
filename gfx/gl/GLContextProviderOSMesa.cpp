/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Offscreen GL through Mesa's OSMesa (llvmpipe), used on Haiku where EGL
 * doesn't initialise and there is no GLX. libOSMesa is loaded at run time,
 * so it is optional: without it every Create* returns null and WebGL is
 * unavailable, as with the Null provider.
 *
 * Contexts render into a small malloc'd buffer that is never shown; WebGL
 * draws into its own framebuffer object (GLContext::InitOffscreen), and the
 * basic compositor reads the pixels back. There are no window contexts.
 */

#include "GLContextProvider.h"
#include "GLContext.h"
#include "GLLibraryLoader.h"
#include "gfxPrefs.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticMutex.h"
#include "prlink.h"

#include <cstdlib>

namespace mozilla {
namespace gl {

using namespace mozilla::gfx;
using namespace mozilla::widget;

// From GL/osmesa.h (not included: it pulls in the system GL/gl.h).
#define LOCAL_OSMESA_BGRA                    0x1
#define LOCAL_OSMESA_FORMAT                  0x22
#define LOCAL_OSMESA_DEPTH_BITS              0x30
#define LOCAL_OSMESA_STENCIL_BITS            0x31
#define LOCAL_OSMESA_PROFILE                 0x33
#define LOCAL_OSMESA_CORE_PROFILE            0x34
#define LOCAL_OSMESA_COMPAT_PROFILE          0x35
#define LOCAL_OSMESA_CONTEXT_MAJOR_VERSION   0x36
#define LOCAL_OSMESA_CONTEXT_MINOR_VERSION   0x37

typedef struct osmesa_context* OSMesaContext;
typedef OSMesaContext (*PFNOSMESACREATECONTEXTATTRIBS)(const int*, OSMesaContext);
typedef void (*PFNOSMESADESTROYCONTEXT)(OSMesaContext);
typedef unsigned char (*PFNOSMESAMAKECURRENT)(OSMesaContext, void*, GLenum,
                                              GLsizei, GLsizei);
typedef OSMesaContext (*PFNOSMESAGETCURRENTCONTEXT)();
typedef PRFuncPtr (*PFNOSMESAGETPROCADDRESS)(const char*);

class OSMesaLibrary
{
public:
    bool EnsureInitialized()
    {
        StaticMutexAutoLock lock(sMutex);
        if (mTried) {
            return !!mLibrary;
        }
        mTried = true;

        if (!Preferences::GetBool("gfx.haiku.osmesa.enabled", true)) {
            return false;
        }

        const char* names[] = { "libOSMesa.so.8", "libOSMesa.so" };
        for (const char* name : names) {
            mLibrary = PR_LoadLibrary(name);
            if (mLibrary) {
                break;
            }
        }
        if (!mLibrary) {
            return false;
        }

#define LOAD(sym, type) \
        sym = (type)PR_FindFunctionSymbol(mLibrary, "OSMesa" #sym)
        LOAD(CreateContextAttribs, PFNOSMESACREATECONTEXTATTRIBS);
        LOAD(DestroyContext, PFNOSMESADESTROYCONTEXT);
        LOAD(MakeCurrent, PFNOSMESAMAKECURRENT);
        LOAD(GetCurrentContext, PFNOSMESAGETCURRENTCONTEXT);
        LOAD(GetProcAddress, PFNOSMESAGETPROCADDRESS);
#undef LOAD

        if (!CreateContextAttribs || !DestroyContext || !MakeCurrent ||
            !GetCurrentContext || !GetProcAddress) {
            PR_UnloadLibrary(mLibrary);
            mLibrary = nullptr;
        }
        return !!mLibrary;
    }

    PRLibrary* mLibrary = nullptr;
    bool mTried = false;
    PFNOSMESACREATECONTEXTATTRIBS CreateContextAttribs = nullptr;
    PFNOSMESADESTROYCONTEXT DestroyContext = nullptr;
    PFNOSMESAMAKECURRENT MakeCurrent = nullptr;
    PFNOSMESAGETCURRENTCONTEXT GetCurrentContext = nullptr;
    PFNOSMESAGETPROCADDRESS GetProcAddress = nullptr;

    static StaticMutex sMutex;
};

StaticMutex OSMesaLibrary::sMutex;
static OSMesaLibrary sOSMesaLibrary;

static PRFuncPtr GLAPIENTRY
OSMesaLookup(const char* aName)
{
    return sOSMesaLibrary.GetProcAddress(aName);
}

class GLContextOSMesa final : public GLContext
{
public:
    MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GLContextOSMesa, override)

    GLContextOSMesa(CreateContextFlags aFlags, const SurfaceCaps& aCaps,
                    OSMesaContext aContext, ContextProfile aProfile)
        : GLContext(aFlags, aCaps, nullptr, true)
        , mContext(aContext)
    {
        SetProfileVersion(aProfile, 0);
        // The context's own buffer is never shown; keep it tiny.
        mBuffer = calloc(kBufferSize * kBufferSize, 4);
    }

    ~GLContextOSMesa() override
    {
        MarkDestroyed();
        if (mContext) {
            sOSMesaLibrary.DestroyContext(mContext);
        }
        free(mBuffer);
    }

    GLContextType GetContextType() const override {
        return GLContextType::OSMesa;
    }

    bool Init() override
    {
        if (!mBuffer || !MakeCurrent(true)) {
            return false;
        }
        SetupLookupFunction();
        if (!InitWithPrefix("gl", true)) {
            return false;
        }
        return IsSupported(GLFeature::framebuffer_object) ||
               IsExtensionSupported(EXT_framebuffer_object);
    }

    bool MakeCurrentImpl(bool aForce) override
    {
        if (!aForce && IsCurrent()) {
            return true;
        }
        return sOSMesaLibrary.MakeCurrent(mContext, mBuffer,
                                          LOCAL_GL_UNSIGNED_BYTE,
                                          kBufferSize, kBufferSize);
    }

    bool IsCurrent() override {
        return sOSMesaLibrary.GetCurrentContext() == mContext;
    }

    bool SetupLookupFunction() override
    {
        mLibrary = sOSMesaLibrary.mLibrary;
        mLookupFunc = (PlatformLookupFunction)&OSMesaLookup;
        return true;
    }

    bool SupportsRobustness() const override { return false; }
    bool SwapBuffers() override { return false; }
    void GetWSIInfo(nsCString* const out) const override {
        out->AppendLiteral("OSMesa (Mesa llvmpipe, offscreen)");
    }

private:
    static const int kBufferSize = 16;

    OSMesaContext mContext;
    void* mBuffer = nullptr;
};

static already_AddRefed<GLContextOSMesa>
CreateOSMesaContext(CreateContextFlags aFlags, const SurfaceCaps& aCaps,
                    nsACString* const aOutFailureId)
{
    if (!sOSMesaLibrary.EnsureInitialized()) {
        aOutFailureId->AssignLiteral("FEATURE_FAILURE_OSMESA_LIB");
        return nullptr;
    }

    bool compat = !!(aFlags & CreateContextFlags::REQUIRE_COMPAT_PROFILE);
    ContextProfile profile = compat ? ContextProfile::OpenGLCompatibility
                                    : ContextProfile::OpenGLCore;
    // llvmpipe offers a 4.5 core profile; fall back to a compatibility
    // context if a core one can't be made.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const int attribs[] = {
            LOCAL_OSMESA_FORMAT, LOCAL_OSMESA_BGRA,
            LOCAL_OSMESA_DEPTH_BITS, 24,
            LOCAL_OSMESA_STENCIL_BITS, 8,
            LOCAL_OSMESA_PROFILE,
            profile == ContextProfile::OpenGLCore ? LOCAL_OSMESA_CORE_PROFILE
                                                  : LOCAL_OSMESA_COMPAT_PROFILE,
            LOCAL_OSMESA_CONTEXT_MAJOR_VERSION,
            profile == ContextProfile::OpenGLCore ? 3 : 2,
            LOCAL_OSMESA_CONTEXT_MINOR_VERSION,
            profile == ContextProfile::OpenGLCore ? 2 : 1,
            0
        };
        OSMesaContext context =
            sOSMesaLibrary.CreateContextAttribs(attribs, nullptr);
        if (context) {
            RefPtr<GLContextOSMesa> gl =
                new GLContextOSMesa(aFlags, aCaps, context, profile);
            if (gl->Init()) {
                return gl.forget();
            }
        }
        if (profile == ContextProfile::OpenGLCompatibility) {
            break;
        }
        profile = ContextProfile::OpenGLCompatibility;
    }

    aOutFailureId->AssignLiteral("FEATURE_FAILURE_OSMESA_CREATE");
    return nullptr;
}

already_AddRefed<GLContext>
GLContextProviderOSMesa::CreateForCompositorWidget(
    CompositorWidget* aCompositorWidget, bool aForceAccelerated)
{
    // No window contexts: compositing stays on the basic compositor.
    return nullptr;
}

already_AddRefed<GLContext>
GLContextProviderOSMesa::CreateForWindow(nsIWidget* aWidget,
                                         bool aForceAccelerated)
{
    return nullptr;
}

already_AddRefed<GLContext>
GLContextProviderOSMesa::CreateWrappingExisting(void*, void*)
{
    return nullptr;
}

already_AddRefed<GLContext>
GLContextProviderOSMesa::CreateHeadless(CreateContextFlags aFlags,
                                        nsACString* const aOutFailureId)
{
    RefPtr<GLContext> gl =
        CreateOSMesaContext(aFlags, SurfaceCaps::Any(), aOutFailureId);
    return gl.forget();
}

already_AddRefed<GLContext>
GLContextProviderOSMesa::CreateOffscreen(const IntSize& aSize,
                                         const SurfaceCaps& aMinCaps,
                                         CreateContextFlags aFlags,
                                         nsACString* const aOutFailureId)
{
    RefPtr<GLContext> gl = CreateHeadless(aFlags, aOutFailureId);
    if (!gl) {
        return nullptr;
    }
    if (!gl->InitOffscreen(aSize, aMinCaps)) {
        aOutFailureId->AssignLiteral("FEATURE_FAILURE_OSMESA_INIT");
        return nullptr;
    }
    return gl.forget();
}

GLContext*
GLContextProviderOSMesa::GetGlobalContext()
{
    // No context sharing.
    return nullptr;
}

void
GLContextProviderOSMesa::Shutdown()
{
}

} // namespace gl
} // namespace mozilla
