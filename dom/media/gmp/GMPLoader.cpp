/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPLoader.h"
#include <stdio.h>
#include "mozilla/Attributes.h"
#include "nsExceptionHandler.h"
#include "gmp-entrypoints.h"
#include "prlink.h"
#include "prenv.h"
#include "prerror.h"
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#  include "mozilla/sandboxTarget.h"
#  include "mozilla/sandboxing/SandboxInitialization.h"
#  include "mozilla/sandboxing/sandboxLogging.h"
#endif
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/Sandbox.h"
#  include "mozilla/SandboxInfo.h"
#  include "mozilla/SandboxProfilerObserver.h"
#endif

#include <string>

#ifdef XP_WIN
#  include <windows.h>
#endif

#ifdef MOZ_USEMEMORYMODULEPP
#  include <iostream>
#  include <fstream>
#  include "mozilla/StaticPrefs_media.h"
#endif

namespace mozilla::gmp {
void GMPAdapter::Shutdown() {
  if (mLib) {
    PR_UnloadLibrary(mLib);
  }
#ifdef MOZ_MEMORYMODULEPP
  if (mModule) {
    LdrUnloadDllMemory(mModule);
    mModule = nullptr;
  }
  if (mModuleBuffer) {
    ::VirtualFree(mModuleBuffer, 0, MEM_RELEASE);
    mModuleBuffer = nullptr;
  }
#endif
}

void* GMPAdapter::FindFunctionSymbol(const char* aName) {
  if (mLib) {
    return PR_FindFunctionSymbol(mLib, aName);
  }
#ifdef MOZ_MEMORYMODULEPP
  if (mModule) {
    return ::GetProcAddress(mModule, aName);
  }
#endif
  return nullptr;
}

class PassThroughGMPAdapter : public GMPAdapter {
 public:
  ~PassThroughGMPAdapter() override {
    // Ensure we're always shutdown, even if caller forgets to call
    // GMPShutdown().
    GMPShutdown();
  }

  GMPErr GMPInit(const GMPPlatformAPI* aPlatformAPI) override {
    if (NS_WARN_IF(!HasLibraryOrModule())) {
      MOZ_CRASH("Missing library!");
      return GMPGenericErr;
    }
    GMPInitFunc initFunc =
        reinterpret_cast<GMPInitFunc>(FindFunctionSymbol("GMPInit"));
    if (!initFunc) {
      MOZ_CRASH("Missing init method!");
      return GMPNotImplementedErr;
    }
    return initFunc(aPlatformAPI);
  }

  GMPErr GMPGetAPI(const char* aAPIName, void* aHostAPI, void** aPluginAPI,
                   const nsACString& /* aKeySystem */) override {
    if (!HasLibraryOrModule()) {
      return GMPGenericErr;
    }
    GMPGetAPIFunc getapiFunc =
        reinterpret_cast<GMPGetAPIFunc>(FindFunctionSymbol("GMPGetAPI"));
    if (!getapiFunc) {
      return GMPNotImplementedErr;
    }
    return getapiFunc(aAPIName, aHostAPI, aPluginAPI);
  }

  void GMPShutdown() override {
    GMPShutdownFunc shutdownFunc =
        reinterpret_cast<GMPShutdownFunc>(FindFunctionSymbol("GMPShutdown"));
    if (shutdownFunc) {
      shutdownFunc();
    }
    Shutdown();
  }
};

bool GMPLoader::Load(const char* aUTF8LibPath, uint32_t aUTF8LibPathLen,
                     const GMPPlatformAPI* aPlatformAPI, GMPAdapter* aAdapter) {
  CrashReporter::AutoRecordAnnotation autoLibPath(
      CrashReporter::Annotation::GMPLibraryPath,
      nsDependentCString(aUTF8LibPath));

  if (!getenv("MOZ_DISABLE_GMP_SANDBOX") && mSandboxStarter &&
      !mSandboxStarter->Start(aUTF8LibPath)) {
    MOZ_CRASH("Cannot start sandbox!");
    return false;
  }

  // Load the GMP.
#ifdef MOZ_MEMORYMODULEPP
  if (StaticPrefs::media_gmp_use_memorymodulepp()) {
    std::ifstream file(aUTF8LibPath, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    LPVOID buffer = VirtuaAlloc(0, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    MOZ_RELEASE_ASSERT(file.read(buffer, size));
    HMEMORYMODULE mod = nullptr;
    LoadLibraryMemoryExW(&mod, nullptr, 0, buffer, 0, L"widevinecdm", nullptr);

    if (!mod) {
      MOZ_CRASH_UNSAFE_PRINTF("Cannot load plugin as library %d",
                              GetLastError());
      return false;
    }

    mAdapter.reset((!aAdapter) ? new PassThroughGMPAdapter() : aAdapter);
    mAdapter->SetAdapteeMemoryModule(mod, buffer);
  } else
#endif
  {
    PRLibSpec libSpec;
#ifdef XP_WIN
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, aUTF8LibPath, -1, nullptr, 0);
    if (pathLen == 0) {
      MOZ_CRASH("Cannot get path length as wide char!");
      return false;
    }

    auto widePath = MakeUnique<wchar_t[]>(pathLen);
    if (MultiByteToWideChar(CP_UTF8, 0, aUTF8LibPath, -1, widePath.get(),
                            pathLen) == 0) {
      MOZ_CRASH("Cannot convert path to wide char!");
      return false;
    }

    libSpec.value.pathname_u = widePath.get();
    libSpec.type = PR_LibSpec_PathnameU;
#else
    libSpec.value.pathname = aUTF8LibPath;
    libSpec.type = PR_LibSpec_Pathname;
#endif
    PRLibrary* lib = PR_LoadLibraryWithFlags(libSpec, 0);
    if (!lib) {
      MOZ_CRASH_UNSAFE_PRINTF("Cannot load plugin as library %d %d",
                              PR_GetError(), PR_GetOSError());
      return false;
    }

    mAdapter.reset((!aAdapter) ? new PassThroughGMPAdapter() : aAdapter);
    mAdapter->SetAdaptee(lib);
  }

  if (mAdapter->GMPInit(aPlatformAPI) != GMPNoErr) {
    MOZ_CRASH("Cannot initialize plugin adapter!");
    return false;
  }

  return true;
}

GMPErr GMPLoader::GetAPI(const char* aAPIName, void* aHostAPI,
                         void** aPluginAPI, const nsACString& aKeySystem) {
  return mAdapter->GMPGetAPI(aAPIName, aHostAPI, aPluginAPI, aKeySystem);
}

void GMPLoader::Shutdown() {
  if (mAdapter) {
    mAdapter->GMPShutdown();
  }
}

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
class WinSandboxStarter : public mozilla::gmp::SandboxStarter {
 public:
  bool Start(const char* aLibPath) override {
    // Cause advapi32 to load before the sandbox is turned on, as
    // Widevine version 970 and later require it and the sandbox
    // blocks it on Win7.
    unsigned int dummy_rand;
    rand_s(&dummy_rand);

    mozilla::SandboxTarget::Instance()->StartSandbox();
    return true;
  }
};
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
namespace {
class LinuxSandboxStarter : public mozilla::gmp::SandboxStarter {
 private:
  LinuxSandboxStarter() = default;
  friend mozilla::detail::UniqueSelector<LinuxSandboxStarter>::SingleObject
  mozilla::MakeUnique<LinuxSandboxStarter>();

 public:
  static UniquePtr<SandboxStarter> Make() {
    if (mozilla::SandboxInfo::Get().CanSandboxMedia()) {
      return MakeUnique<LinuxSandboxStarter>();
    }
    // Sandboxing isn't possible, but the parent has already
    // checked that this plugin doesn't require it.  (Bug 1074561)
    return nullptr;
  }
  bool Start(const char* aLibPath) override {
    RegisterProfilerObserversForSandboxProfiler();
    mozilla::SetMediaPluginSandbox(aLibPath);
    return true;
  }
};
}  // anonymous namespace
#endif  // XP_LINUX && MOZ_SANDBOX

static UniquePtr<SandboxStarter> MakeSandboxStarter() {
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  return mozilla::MakeUnique<WinSandboxStarter>();
#elif defined(XP_LINUX) && defined(MOZ_SANDBOX)
  return LinuxSandboxStarter::Make();
#else
  return nullptr;
#endif
}

GMPLoader::GMPLoader() : mSandboxStarter(MakeSandboxStarter()) {}

bool GMPLoader::CanSandbox() const { return !!mSandboxStarter; }

}  // namespace mozilla::gmp
