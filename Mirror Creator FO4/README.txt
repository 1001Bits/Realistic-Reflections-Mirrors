Realistic Reflections - Mirrors 1.0 - Fallout 4 Creators

This download contains two authoring tools. Players install the main mod.
Use the matching player DLL package from this release, or a newer build with
prepared-NIF support; earlier DLLs do not recognise Mirror Creator exports.

MIRROR CREATOR
Extract the complete ZIP anywhere you can write, then open Mirror Creator and
double-click Start Mirror Creator.cmd. Select a Fallout NIF, click its pane,
Shift-click additional flat areas if needed, and Save. Use the prepared NIF as
a Static object's model in your own mod. Python and the browser viewer are
included. See Mirror Creator/README.txt and its docs/MIRROR_CREATOR.md.

CREATION KIT HELPER
To assign an existing whole model part in CK instead, copy Data and MirrorAuthoring
beside CreationKit.exe, then start MirrorAuthoring/MirrorsOfFalloutCK.exe.
Load Realistic Reflections - Mirrors.esm with your own plugin active. In a Static
object's Model Data > Alternate Textures, select a flat pane and assign
MOF_MirrorSurface. Place the object and save. This helper requires CK 1.11.240.0.
Do not additionally assign that material to a NIF saved by Mirror Creator.
The CK version restriction applies to the helper, not the standalone NIF tool.

Both workflows require the main mod and its enabled ESM in game, plus your
content files. The Data assets in this kit match the player package. Only one
flat reflection per Static object is supported. Test your result in game before
publishing. See Data/README-MirrorAuthoring.txt for geometry limitations.

The Creator source and licenses are in Mirror Creator. The compiled CK helper's
corresponding source is in Source, with its build instructions in
MirrorAuthoring/README.txt. This ZIP does not contain the F4SE player plugin.
