#include "Win32.h"
#include <CommDlg.h>
#include <Shellapi.h>

namespace {
struct Editor { std::filesystem::path path; DWORD pid{}, thread{}; HWND window{}; };
void Error(const wchar_t* text) { MessageBoxW(nullptr, text, L"Realistic Reflections - Mirrors — Creation Kit helper", MB_OK | MB_ICONINFORMATION); }
BOOL CALLBACK Find(HWND window, LPARAM param) {
    auto& editor = *reinterpret_cast<Editor*>(param); wchar_t kind[128]{}; GetClassNameW(window, kind, 128);
    if (wcscmp(kind, L"Creation Kit")) return TRUE;
    DWORD pid{}; auto thread = GetWindowThreadProcessId(window, &pid); auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    std::wstring path(32768, L'\0'); DWORD n = static_cast<DWORD>(path.size()); bool read = QueryFullProcessImageNameW(process, 0, path.data(), &n); CloseHandle(process);
    if (!read) return TRUE; path.resize(n);
    if (_wcsicmp(path.c_str(), editor.path.c_str())) return TRUE;
    editor.pid = pid; editor.thread = thread; editor.window = window; return FALSE;
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        Editor editor; auto folder = MirrorCK::ModulePath().parent_path(); editor.path = folder.parent_path()/L"CreationKit.exe";
        int count{}; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (args && count == 3 && !wcscmp(args[1], L"--ck")) editor.path = args[2];
        else if (count > 1) { LocalFree(args); Error(L"Usage: MirrorsOfFalloutCK.exe [--ck \"path\\CreationKit.exe\"]"); return 1; }
        if (args) LocalFree(args);
        if (!std::filesystem::exists(editor.path)) {
            wchar_t path[32768]{}; OPENFILENAMEW choose{}; choose.lStructSize = sizeof(choose); choose.lpstrFile = path; choose.nMaxFile = 32768;
            choose.lpstrFilter = L"Fallout 4 Creation Kit\0CreationKit.exe\0"; choose.lpstrTitle = L"Choose Fallout 4 CreationKit.exe";
            choose.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameW(&choose)) return 0; editor.path = path;
        }
        editor.path = std::filesystem::absolute(editor.path).lexically_normal();
        if (!MirrorCK::SupportedEditor(editor.path)) { Error(L"This helper supports the verified Fallout 4 Creation Kit 1.11.240.0 executable. Select that editor's original CreationKit.exe."); return 1; }
        auto dll = folder/L"MirrorsOfFalloutCK.dll";
        auto module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) { Error(L"MirrorsOfFalloutCK.dll could not be loaded. Keep the EXE and DLL together."); return 1; }
        auto callback = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "MirrorCKHook"));
        if (!callback) { Error(L"The helper DLL is incompatible. Reinstall the authoring helper package."); FreeLibrary(module); return 1; }
        EnumWindows(Find, reinterpret_cast<LPARAM>(&editor));
        HANDLE process{};
        if (!editor.pid) {
            auto command = L"\""+editor.path.wstring()+L"\""; STARTUPINFOW start{}; start.cb = sizeof(start); PROCESS_INFORMATION created{};
            if (!CreateProcessW(editor.path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, editor.path.parent_path().c_str(), &start, &created)) {
                Error(L"Creation Kit could not be started."); FreeLibrary(module); return 1;
            }
            process = created.hProcess; editor.pid = created.dwProcessId; editor.thread = created.dwThreadId; CloseHandle(created.hThread);
            WaitForInputIdle(process, 10000);
        } else process = OpenProcess(SYNCHRONIZE, FALSE, editor.pid);
        if (!process) { Error(L"Unable to attach to this Creation Kit session."); FreeLibrary(module); return 1; }
        auto mutexName = L"Local\\MirrorsOfFalloutCK-"+std::to_wstring(editor.pid);
        auto mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (mutex) CloseHandle(mutex); CloseHandle(process); FreeLibrary(module); Error(L"The mirror helper is already active in this CK session."); return 0;
        }
        auto hook = SetWindowsHookExW(WH_CALLWNDPROCRET, callback, module, editor.thread);
        if (!hook) { Error(L"Unable to attach the helper. Run CK and this helper with the same Windows permissions."); CloseHandle(mutex); CloseHandle(process); FreeLibrary(module); return 1; }
        if (editor.window) { ShowWindow(editor.window, SW_RESTORE); SetForegroundWindow(editor.window); }
        // The thread-specific hook ends with this editor. It never loads into a game.
        MSG message{}; bool running = true;
        while (running) {
            auto wait = MsgWaitForMultipleObjects(1, &process, FALSE, INFINITE, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) running = false;
                else { TranslateMessage(&message); DispatchMessageW(&message); }
            }
        }
        UnhookWindowsHookEx(hook); CloseHandle(mutex); CloseHandle(process); FreeLibrary(module); return 0;
    } catch (...) { Error(L"The Creation Kit helper could not initialize. Check the selected editor and authoring files."); return 1; }
}
