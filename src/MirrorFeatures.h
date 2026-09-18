#pragma once
#include "MirrorFeatureDefaults.h"
#if defined(MIRRORS_OF_SKYRIM_STANDALONE)
#include <REL/Module.h>
#include "SupportedRuntimePolicy.h"
#endif

namespace MirrorFeatures
{
inline bool Enabled(std::wstring_view legacyName) noexcept
{
#if defined(MIRRORS_OF_SKYRIM_STANDALONE)
    using Runtime = MirrorFeatureDefaults::Runtime;
    static const Runtime runtime = [] {
        const auto version = REL::Module::get().version();
        if (REL::Module::IsSE() && version == REL::Version{1,5,97,0}) return Runtime::SE;
        if (REL::Module::IsAE() && SupportedRuntimePolicy::IsSupportedAEVersion(version)) return Runtime::AE;
        if (REL::Module::IsVR() && version == REL::Version{1,4,15,0}) return Runtime::VR;
        return Runtime::Other;
    }();
    return MirrorFeatureDefaults::Enabled(runtime, legacyName);
#else
    (void)legacyName;
    return false; // The separate research DLL retains its explicit opt-ins.
#endif
}
}
