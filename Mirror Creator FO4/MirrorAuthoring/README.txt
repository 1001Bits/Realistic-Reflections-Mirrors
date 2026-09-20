# Realistic Reflections - Mirrors Creation Kit helper

The **Creators** download also includes the standalone Fallout **Mirror Creator**.
Use **Mirror Creator/Start Mirror Creator.cmd** to select a flat area in a NIF,
Shift-click additional areas on the same plane, and save a prepared model.
Use that NIF as a Static object's model without also assigning a material with
this CK helper. The standalone tool has no CK version requirement; see its README.

Start `MirrorsOfFalloutCK.exe` to open Fallout Creation Kit with the mirror picker.
If CK is already open, the helper connects to that session. Keep the EXE and DLL
together in `MirrorAuthoring`, beside the game's `Data` folder. With MO2, add this
EXE as a tool and run it through MO2.

1. Load `Realistic Reflections - Mirrors.esm` and make your own ESP the active file.
2. Open a Static object's **Model Data → Alternate Textures...**.
3. Select its flat pane, choose **MOF_MirrorSurface**, and click **Assign**.
4. Confirm Model Data and Static with **OK**, place the object, and save your ESP.

The shared ESM has its master flag set so CK preserves dependencies correctly.
Keep that flag and its filename unchanged; a second library plugin is not needed.

The helper creates the necessary material swap in your plugin. Other material
substitutions and color remaps are preserved. Canceling the Static dialog discards
the assignment. A pane that shares its material path with another part cannot be
selected; give it a unique BGSM in the NIF first. One flat, static part per object
is supported. Moving the assignment to another pane restores the old pane's
original NIF material; it does not recover a replacement you previously overwrote
on that pane.

The verified editor is **Fallout Creation Kit 1.11.240.0**. The helper checks the
editor executable and the native functions it uses; other versions are refused.
It runs only in Creation Kit. Players need Realistic Reflections - Mirrors with its single
`Realistic Reflections - Mirrors.esm` enabled. It contains the workshop mirrors and
`MOF_MirrorSurface`. They do not need this helper or a separate authoring ESM.

If Model Data was already open when you started the helper, close and reopen that
dialog. Diagnostics are written to `MirrorsOfFalloutCK.log` beside CreationKit.exe.
If archives provide conflicting versions of a model, extract the version CK uses
as a loose NIF before authoring.

Authored mirrors work automatically with the current main mod, its ESM and your
saved plugin enabled. Start the game through the matching F4SE and visit your
placed object to check the reflection. See `Data/README-MirrorAuthoring.txt` in
the full kit for supported geometry and the complete guide.

## Source and building

The full kit includes the helper's source, original asset tests, build files and
the stb decompressor source used by the helper. Build with Visual Studio 2022's
Desktop development with C++ workload, CMake and Python 3:

```powershell
cmake -S Source -B Source/build/ALL -G "Visual Studio 17 2022" -A x64
cmake --build Source/build/ALL --config Release
ctest --test-dir Source/build/ALL -C Release --output-on-failure
```

In the mod repository, use the existing ALL preset and its build lock. Targets
are `MirrorsOfFalloutCK`, `MirrorCKLauncher` and `mirror_ck_asset_tests`. Outputs
are in `build/ALL/tools/ck/Release`. No game executable is distributed or patched.

The helper follows the repository's GNU GPL v3 license in `COPYING`. stb is
provided under the MIT license in `THIRD-PARTY-NOTICES.txt`.
