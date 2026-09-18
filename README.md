# Realistic Reflections - Mirrors

Plugin source. Version 1.0.0.

Third-party terms are in `THIRD_PARTY_NOTICES.md` and `LICENSES/`.

## Build

Prerequisites: Visual Studio 2022 (v143) x64, CMake >= 3.21, vcpkg at `C:/vcpkg`.

```powershell
cmake --preset default
cmake --build --preset release
```

The DLL is `build/bin/RealisticReflectionsMirrors.dll`. CommonLibSSE-NG is
vendored at commit `8c4025b` (MIT). The plugin uses built-in addresses for
Skyrim SE 1.5.97 and AE 1.7.104.

## Test NIFs

`test-nifs/plain/` are unprepared meshes you can run through Mirror Creator
(Path B) or assign `MOS_MirrorSurface` to in Creation Kit (Path A).

`test-nifs/prepared/` are the same meshes after Path B. In Creation Kit, set
**only** that NIF as the model. Do not also assign `MOS_MirrorSurface`.

Path B for another author's furnishing mod:

1. Copy their existing NIF (the mesh their plugin already points at).
2. Open it in Mirror Creator, choose **Wall mirror**, click the pane.
3. Save the downloaded NIF. Their original file is not overwritten.
4. Either set that NIF as the object's model in their plugin, or install it as
   a mesh replacer at the same `meshes\\...` path their plugin already uses.

`overhaul-cabinet.nif` is a first-party stand-in for that workflow: a wooden
cabinet plus a separate pane. Path B keeps the cabinet and prepares only
the pane as `MOSReflectiveSurface:0`.
