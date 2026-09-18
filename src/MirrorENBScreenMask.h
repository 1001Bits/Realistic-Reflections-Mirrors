#pragma once

namespace MirrorENBScreenMask
{
    // Default-off ENB t37 correction. Install after the renderer exists.
    void OnInputLoaded() noexcept;
    // True only after the native wrapper/context and neutral-mask hook are ready.
    bool Ready() noexcept;
    void LogDiagnostics(const char* reason) noexcept;
}
