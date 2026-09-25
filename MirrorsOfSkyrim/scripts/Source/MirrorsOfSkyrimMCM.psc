Scriptname MirrorsOfSkyrimMCM extends SKI_ConfigBase
{SkyUI MCM page for Mirrors of Skyrim. Values are owned by the SKSE plugin; this script only displays and forwards them.}

Int _zoomOption = 0
Int _wheelZoomOption = -1
Int _resolutionOption = -1
Int _modeOption = -1
Int _targetOption = -1
Int _effectiveOption = -1
Int _loweredEffectiveOption = -1
Bool _handPage = True
Int _qualityLevelOption = -1
Int _everyFrameOption = -1
Int _refreshOption = -1
Int _shadowsOption = -1
Int _shadowResolutionOption = -1
Int _playerShadowOption = -1
Int _loweredOption = -1
Int _raisedResolutionOption = -1
Int _loweredResolutionOption = -1
Int _handEveryFrameOption = -1
Int _handRefreshOption = -1
Int _handLoweredRefreshOption = -1
Int _merchantsOption = -1
Int _craftingOption = -1
Int _homesOption = -1
Int _innsOption = -1

; Built on demand rather than stored. A script variable set in OnConfigInit
; is only correct for saves that ran OnConfigInit with this version of the
; script; an older save reaches OnOptionMenuOpen with it still None, and the
; dropdown comes up empty (owner, 2026-09-15). Three strings cost nothing.
String[] Function QualityNames()
	String[] names = New String[3]
	names[0] = "Low"
	names[1] = "Medium"
	names[2] = "High"
	Return names
EndFunction

String Function QualityName(Int aiLevel)
	If aiLevel < 0 || aiLevel > 2
		Return "Medium"
	EndIf
	Return QualityNames()[aiLevel]
EndFunction

Int Function GetVersion()
	Return 13
EndFunction

Event OnConfigInit()
	ModName = "Mirrors of Skyrim"
	Pages = New String[3]
	Pages[0] = "Hand Mirror"
	Pages[1] = "Placed Mirrors"
	Pages[2] = "Mirrors in World"
EndEvent

Event OnVersionUpdate(Int aiVersion)
	OnConfigInit()
EndEvent

; Realistic Reflections - Mirrors 1.1.1 and later show these settings as the "Mirrors of
; Skyrim" page of their own menu (core menu version 15). With such a core this menu steps
; aside; with an earlier core (1.1) it is the add-on's own menu, as before.
Bool Function CoreHostsPage()
	SKI_ConfigBase core = Game.GetFormFromFile(0x840, "RealisticReflectionsMirrors.esm") as SKI_ConfigBase
	Return core && core.GetVersion() >= 15
EndFunction

; SkyUI's manager announces itself on every game load.
Function OnConfigManagerReady(String a_eventName, String a_strArg, Float a_numArg, Form a_sender)
	SKI_ConfigManager manager = a_sender as SKI_ConfigManager
	If !manager
		Return
	EndIf
	If CoreHostsPage()
		manager.UnregisterMod(Self)
		Return
	EndIf
	; SkyUI's own handler does only this registration. RegisterMod keeps an existing
	; entry, and brings the menu back to a save that stepped aside under a newer core.
	manager.RegisterMod(Self, ModName)
EndFunction

String[] Function ModeNames()
    String[] names = New String[3]
    names[0] = "Automatic"
    names[1] = "Preset"
    names[2] = "Manual"
    Return names
EndFunction

; Native/INI values stay Manual=0, Preset=1, Automatic=2, as in the core MCM.
Int Function ModeMenuIndex(Int nativeMode)
    If nativeMode < 0 || nativeMode > 2
        Return 0
    EndIf
    Return 2 - nativeMode
EndFunction

String Function EffectiveText(Bool raised = True)
    If _handPage && !raised && !MirrorsOfSkyrimNative.GetHandMirrorReflectWhenLowered()
        Return "Disabled"
    EndIf
    String text = ""
    Int hz = 0
    If MirrorsOfSkyrimNative.GetMirrorResolutionFollowsFramebuffer()
        text = "ENB size"
    ElseIf _handPage
        Int size = MirrorsOfSkyrimNative.GetHandMirrorEffectiveResolution(raised)
        text = size + "x" + size
    Else
        text = MirrorsOfSkyrimNative.GetMirrorEffectiveResolution() + "x" + MirrorsOfSkyrimNative.GetMirrorEffectiveHeight()
    EndIf
    If _handPage
        hz = MirrorsOfSkyrimNative.GetHandMirrorEffectiveRefresh(raised)
    Else
        hz = MirrorsOfSkyrimNative.GetMirrorEffectiveRefresh()
    EndIf
    If hz == 0
        Return text + ", every frame"
    EndIf
    Return text + ", " + hz + "Hz"
EndFunction

Event OnPageReset(String asPage)
    _zoomOption = -1
    _wheelZoomOption = -1
    _resolutionOption = -1
    _modeOption = -1
    _targetOption = -1
    _qualityLevelOption = -1
    _effectiveOption = -1
    _loweredEffectiveOption = -1
    _everyFrameOption = -1
    _refreshOption = -1
    _shadowsOption = -1
    _shadowResolutionOption = -1
    _playerShadowOption = -1
    _loweredOption = -1
    _raisedResolutionOption = -1
    _loweredResolutionOption = -1
    _handEveryFrameOption = -1
    _handRefreshOption = -1
    _handLoweredRefreshOption = -1
    _merchantsOption = -1
    _craftingOption = -1
    _homesOption = -1
    _innsOption = -1
    _handPage = asPage != "Placed Mirrors"
    SetCursorFillMode(TOP_TO_BOTTOM)
    If asPage == "Mirrors in World"
        _merchantsOption = AddToggleOption("Buy mirrors from merchants", MirrorsOfSkyrimNative.GetMirrorMerchantsEnabled())
        _craftingOption = AddToggleOption("Craft mirrors", MirrorsOfSkyrimNative.GetMirrorCraftingEnabled())
        _homesOption = AddToggleOption("Mirrors in homes", MirrorsOfSkyrimNative.GetMirrorHomesEnabled())
        _innsOption = AddToggleOption("Mirrors in inns", MirrorsOfSkyrimNative.GetMirrorInnsEnabled())
        Return
    EndIf
    Int mode = MirrorsOfSkyrimNative.GetMirrorQualityMode()
    If mode < 0 || mode > 2
        mode = 2
    EndIf
    _modeOption = AddMenuOption("Quality mode", ModeNames()[ModeMenuIndex(mode)])
    Bool enb = MirrorsOfSkyrimNative.GetMirrorResolutionFollowsFramebuffer()
    If mode == 2
        If enb
            _targetOption = AddTextOption("Target game FPS", "Unavailable with ENB", OPTION_FLAG_DISABLED)
        Else
            _targetOption = AddSliderOption("Target game FPS", MirrorsOfSkyrimNative.GetMirrorTargetFPS() as Float, "{0} FPS")
        EndIf
    ElseIf mode == 1
        _qualityLevelOption = AddMenuOption("Quality preset", QualityName(MirrorsOfSkyrimNative.GetMirrorQualityLevel()))
    Else
        AddManualControls(enb)
    EndIf
    _effectiveOption = AddTextOption("Effective quality", EffectiveText())
    If _handPage
        _loweredEffectiveOption = AddTextOption("Effective quality (lowered)", EffectiveText(False))
        AddHeaderOption("Hand Mirror")
        _loweredOption = AddToggleOption("Reflect when lowered", MirrorsOfSkyrimNative.GetHandMirrorReflectWhenLowered())
        _zoomOption = AddSliderOption("Portrait zoom", MirrorsOfSkyrimNative.GetHandMirrorZoom(), "{2}x")
        _wheelZoomOption = AddToggleOption("Mouse wheel controls mirror zoom", MirrorsOfSkyrimNative.GetHandMirrorWheelZoom())
    EndIf
EndEvent

Function AddManualControls(Bool enb)
    Int resolutionFlags = 0
    If enb
        resolutionFlags = OPTION_FLAG_DISABLED
    EndIf
    Int hz = MirrorsOfSkyrimNative.GetPlacedMirrorRefresh()
    If _handPage
        If !MirrorsOfSkyrimNative.GetHandMirrorResolutionControls()
            resolutionFlags = OPTION_FLAG_DISABLED
        EndIf
        _raisedResolutionOption = AddSliderOption("Raised resolution", MirrorsOfSkyrimNative.GetHandMirrorRaisedResolution() as Float, "{0}", resolutionFlags)
        Int loweredFlags = resolutionFlags
        If !MirrorsOfSkyrimNative.GetHandMirrorReflectWhenLowered()
            loweredFlags = OPTION_FLAG_DISABLED
        EndIf
        _loweredResolutionOption = AddSliderOption("Lowered resolution", MirrorsOfSkyrimNative.GetHandMirrorLoweredResolution() as Float, "{0}", loweredFlags)
        hz = MirrorsOfSkyrimNative.GetHandMirrorRefresh()
        _handEveryFrameOption = AddToggleOption("Update every game frame", hz == 0)
    Else
        _resolutionOption = AddSliderOption("Resolution", MirrorsOfSkyrimNative.GetPlacedMirrorResolution() as Float, "{0}", resolutionFlags)
        _everyFrameOption = AddToggleOption("Update every game frame", hz == 0)
    EndIf
    Int refreshFlags = 0
    If hz == 0
        refreshFlags = OPTION_FLAG_DISABLED
        hz = 30
    EndIf
    If _handPage
        _handRefreshOption = AddSliderOption("Refresh rate", hz as Float, "{0} Hz", refreshFlags)
        If !MirrorsOfSkyrimNative.GetHandMirrorReflectWhenLowered()
            refreshFlags = OPTION_FLAG_DISABLED
        EndIf
        Int loweredHz = MirrorsOfSkyrimNative.GetHandMirrorLoweredRefresh()
        If loweredHz == 0
            loweredHz = 30
        EndIf
        _handLoweredRefreshOption = AddSliderOption("Refresh rate (lowered)", loweredHz as Float, "{0} Hz", refreshFlags)
    Else
        _refreshOption = AddSliderOption("Refresh rate", hz as Float, "{0} Hz", refreshFlags)
    EndIf
    Int shadowFlags = 0
    If !MirrorsOfSkyrimNative.GetMirrorShadowControls()
        shadowFlags = OPTION_FLAG_DISABLED
    EndIf
    Bool shadows = MirrorsOfSkyrimNative.GetPlacedMirrorShadows()
    If _handPage
        shadows = MirrorsOfSkyrimNative.GetHandMirrorShadows()
    EndIf
    _shadowsOption = AddToggleOption("Shadows in reflections", shadows, shadowFlags)
    If !shadows
        shadowFlags = OPTION_FLAG_DISABLED
    EndIf
    _shadowResolutionOption = AddSliderOption("Shadow map resolution", MirrorsOfSkyrimNative.GetMirrorShadowResolution() as Float, "{0}", shadowFlags)
    _playerShadowOption = AddToggleOption("Character shadows on itself", MirrorsOfSkyrimNative.GetMirrorPlayerShadow(), shadowFlags)
EndFunction

Event OnOptionMenuOpen(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _modeOption
        SetMenuDialogOptions(ModeNames())
        SetMenuDialogStartIndex(ModeMenuIndex(MirrorsOfSkyrimNative.GetMirrorQualityMode()))
        SetMenuDialogDefaultIndex(0)
    ElseIf aiOption == _qualityLevelOption
		SetMenuDialogOptions(QualityNames())
		SetMenuDialogStartIndex(MirrorsOfSkyrimNative.GetMirrorQualityLevel())
		SetMenuDialogDefaultIndex(1)
	EndIf
EndEvent

Event OnOptionMenuAccept(Int aiOption, Int auiIndex)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _modeOption
        If auiIndex >= 0 && auiIndex <= 2
            MirrorsOfSkyrimNative.SetMirrorQualityMode(2 - auiIndex)
        EndIf
        ForcePageReset()
    ElseIf aiOption == _qualityLevelOption
		MirrorsOfSkyrimNative.SetMirrorQualityLevel(auiIndex)
		ForcePageReset()
	EndIf
EndEvent

Event OnOptionSelect(Int aiOption)
    If aiOption < 0
        Return
    EndIf
	If aiOption == _homesOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorHomesEnabled(!MirrorsOfSkyrimNative.GetMirrorHomesEnabled()), False)
	ElseIf aiOption == _innsOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorInnsEnabled(!MirrorsOfSkyrimNative.GetMirrorInnsEnabled()), False)
	ElseIf aiOption == _loweredOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetHandMirrorReflectWhenLowered(!MirrorsOfSkyrimNative.GetHandMirrorReflectWhenLowered()), False)
		ForcePageReset()
	ElseIf aiOption == _merchantsOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorMerchantsEnabled(!MirrorsOfSkyrimNative.GetMirrorMerchantsEnabled()), False)
	ElseIf aiOption == _craftingOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorCraftingEnabled(!MirrorsOfSkyrimNative.GetMirrorCraftingEnabled()), False)
	ElseIf aiOption == _everyFrameOption
		Int hz = 0
		If MirrorsOfSkyrimNative.GetPlacedMirrorRefresh() == 0
			hz = 30
		EndIf
		MirrorsOfSkyrimNative.SetPlacedMirrorRefresh(hz)
		ForcePageReset()
	ElseIf aiOption == _handEveryFrameOption
		Int handHz = 0
		If MirrorsOfSkyrimNative.GetHandMirrorRefresh() == 0
			handHz = 30
		EndIf
		MirrorsOfSkyrimNative.SetHandMirrorRefresh(handHz)
		MirrorsOfSkyrimNative.SetHandMirrorLoweredRefresh(handHz)
		ForcePageReset()
	ElseIf aiOption == _wheelZoomOption
		Bool enabled = MirrorsOfSkyrimNative.SetHandMirrorWheelZoom(!MirrorsOfSkyrimNative.GetHandMirrorWheelZoom())
		SetToggleOptionValue(aiOption, enabled, False)
	ElseIf aiOption == _shadowsOption
		If _handPage
			MirrorsOfSkyrimNative.SetHandMirrorShadows(!MirrorsOfSkyrimNative.GetHandMirrorShadows())
		Else
			MirrorsOfSkyrimNative.SetPlacedMirrorShadows(!MirrorsOfSkyrimNative.GetPlacedMirrorShadows())
		EndIf
		ForcePageReset()
	ElseIf aiOption == _playerShadowOption
		MirrorsOfSkyrimNative.SetMirrorPlayerShadow(!MirrorsOfSkyrimNative.GetMirrorPlayerShadow())
		ForcePageReset()
	EndIf
EndEvent

Event OnOptionSliderOpen(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _targetOption
        SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetMirrorTargetFPS() as Float)
        SetSliderDialogDefaultValue(60)
        SetSliderDialogRange(30, 144)
        SetSliderDialogInterval(1)
    ElseIf aiOption == _shadowResolutionOption
        SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetMirrorShadowResolution() as Float)
        SetSliderDialogDefaultValue(2048)
        SetSliderDialogRange(256, 4096)
        SetSliderDialogInterval(256)
    ElseIf aiOption == _handRefreshOption || aiOption == _handLoweredRefreshOption
        If aiOption == _handRefreshOption
            SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetHandMirrorRefresh() as Float)
        Else
            SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetHandMirrorLoweredRefresh() as Float)
        EndIf
        SetSliderDialogDefaultValue(30)
        SetSliderDialogRange(30, 120)
        SetSliderDialogInterval(1)
	ElseIf aiOption == _raisedResolutionOption || aiOption == _loweredResolutionOption
		If aiOption == _raisedResolutionOption
			SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetHandMirrorRaisedResolution() as Float)
			SetSliderDialogDefaultValue(2048)
		Else
			SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetHandMirrorLoweredResolution() as Float)
			SetSliderDialogDefaultValue(1024)
		EndIf
		SetSliderDialogRange(512, 4096)
		SetSliderDialogInterval(512)
	ElseIf aiOption == _resolutionOption
		SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetPlacedMirrorResolution() as Float)
		SetSliderDialogDefaultValue(2048)
		SetSliderDialogRange(512, 4096)
		SetSliderDialogInterval(512)
	ElseIf aiOption == _refreshOption
		SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetPlacedMirrorRefresh() as Float)
		SetSliderDialogDefaultValue(30)
		SetSliderDialogRange(30, 120)
		SetSliderDialogInterval(1)
	ElseIf aiOption == _zoomOption
		SetSliderDialogStartValue(MirrorsOfSkyrimNative.GetHandMirrorZoom())
		SetSliderDialogDefaultValue(MirrorsOfSkyrimNative.GetHandMirrorZoomDefault())
		SetSliderDialogRange(MirrorsOfSkyrimNative.GetHandMirrorZoomMinimum(), MirrorsOfSkyrimNative.GetHandMirrorZoomMaximum())
		SetSliderDialogInterval(0.05)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float afValue)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _targetOption
        MirrorsOfSkyrimNative.SetMirrorTargetFPS(afValue as Int)
    ElseIf aiOption == _shadowResolutionOption
        MirrorsOfSkyrimNative.SetMirrorShadowResolution(afValue as Int)
	ElseIf aiOption == _raisedResolutionOption
		Int raisedSize = MirrorsOfSkyrimNative.SetHandMirrorRaisedResolution(afValue as Int)
		SetSliderOptionValue(aiOption, raisedSize as Float, "{0}", False)
	ElseIf aiOption == _loweredResolutionOption
		Int loweredSize = MirrorsOfSkyrimNative.SetHandMirrorLoweredResolution(afValue as Int)
		SetSliderOptionValue(aiOption, loweredSize as Float, "{0}", False)
	ElseIf aiOption == _resolutionOption
		Int size = MirrorsOfSkyrimNative.SetPlacedMirrorResolution(afValue as Int)
		SetSliderOptionValue(aiOption, size as Float, "{0}", False)
	ElseIf aiOption == _refreshOption
		Int hz = MirrorsOfSkyrimNative.SetPlacedMirrorRefresh(afValue as Int)
		SetSliderOptionValue(aiOption, hz as Float, "{0} Hz", False)
	ElseIf aiOption == _handRefreshOption
		Int handHz = MirrorsOfSkyrimNative.SetHandMirrorRefresh(afValue as Int)
		SetSliderOptionValue(aiOption, handHz as Float, "{0} Hz", False)
	ElseIf aiOption == _handLoweredRefreshOption
		Int loweredHz = MirrorsOfSkyrimNative.SetHandMirrorLoweredRefresh(afValue as Int)
		SetSliderOptionValue(aiOption, loweredHz as Float, "{0} Hz", False)
	ElseIf aiOption == _zoomOption
		Float stored = MirrorsOfSkyrimNative.SetHandMirrorZoom(afValue)
		SetSliderOptionValue(aiOption, stored, "{2}x", False)
	EndIf
    ForcePageReset()
EndEvent

Event OnOptionDefault(Int aiOption)
    If aiOption < 0
        Return
    EndIf
	If aiOption == _homesOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorHomesEnabled(True), False)
	ElseIf aiOption == _innsOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorInnsEnabled(True), False)
	ElseIf aiOption == _loweredOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetHandMirrorReflectWhenLowered(True), False)
		ForcePageReset()
	ElseIf aiOption == _raisedResolutionOption
		Int raisedSize = MirrorsOfSkyrimNative.SetHandMirrorRaisedResolution(2048)
		SetSliderOptionValue(aiOption, raisedSize as Float, "{0}", False)
	ElseIf aiOption == _loweredResolutionOption
		Int loweredSize = MirrorsOfSkyrimNative.SetHandMirrorLoweredResolution(1024)
		SetSliderOptionValue(aiOption, loweredSize as Float, "{0}", False)
	ElseIf aiOption == _merchantsOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorMerchantsEnabled(True), False)
	ElseIf aiOption == _craftingOption
		SetToggleOptionValue(aiOption, MirrorsOfSkyrimNative.SetMirrorCraftingEnabled(True), False)
    ElseIf aiOption == _modeOption
        MirrorsOfSkyrimNative.SetMirrorQualityMode(2)
        ForcePageReset()
    ElseIf aiOption == _targetOption
        MirrorsOfSkyrimNative.SetMirrorTargetFPS(60)
        ForcePageReset()
    ElseIf aiOption == _shadowsOption
        If _handPage
            MirrorsOfSkyrimNative.SetHandMirrorShadows(True)
        Else
            MirrorsOfSkyrimNative.SetPlacedMirrorShadows(True)
        EndIf
        ForcePageReset()
    ElseIf aiOption == _shadowResolutionOption
        MirrorsOfSkyrimNative.SetMirrorShadowResolution(2048)
        ForcePageReset()
    ElseIf aiOption == _playerShadowOption
        MirrorsOfSkyrimNative.SetMirrorPlayerShadow(True)
        ForcePageReset()
    ElseIf aiOption == _qualityLevelOption
        MirrorsOfSkyrimNative.SetMirrorQualityLevel(1)
		ForcePageReset()
	ElseIf aiOption == _resolutionOption
		MirrorsOfSkyrimNative.SetPlacedMirrorResolution(2048)
		ForcePageReset()
	ElseIf aiOption == _everyFrameOption || aiOption == _refreshOption
		MirrorsOfSkyrimNative.SetPlacedMirrorRefresh(30)
		ForcePageReset()
	ElseIf aiOption == _handEveryFrameOption || aiOption == _handRefreshOption || aiOption == _handLoweredRefreshOption
		MirrorsOfSkyrimNative.SetHandMirrorRefresh(30)
		MirrorsOfSkyrimNative.SetHandMirrorLoweredRefresh(30)
		ForcePageReset()
	ElseIf aiOption == _zoomOption
		Float stored = MirrorsOfSkyrimNative.SetHandMirrorZoom(MirrorsOfSkyrimNative.GetHandMirrorZoomDefault())
		SetSliderOptionValue(aiOption, stored, "{2}x", False)
	ElseIf aiOption == _wheelZoomOption
		Bool enabled = MirrorsOfSkyrimNative.SetHandMirrorWheelZoom(True)
		SetToggleOptionValue(aiOption, enabled, False)
	EndIf
EndEvent

Event OnOptionHighlight(Int aiOption)
    If aiOption < 0
        Return
    EndIf
	If aiOption == _homesOption
		SetInfoText("Add mirrors to homes; purchased furnishings must still be unlocked.")
	ElseIf aiOption == _innsOption
		SetInfoText("Add mirrors to inns.")
	ElseIf aiOption == _loweredOption
		SetInfoText("Keep reflecting while lowered; turn off to show a black pane and reduce rendering work.")
	ElseIf aiOption == _raisedResolutionOption
		SetInfoText("Image resolution while the hand mirror is raised, 512 to 4096 in powers of two; lower values reduce GPU work.")
	ElseIf aiOption == _loweredResolutionOption
		SetInfoText("Image resolution while lowered, 512 to 4096 in powers of two; disable Reflect when lowered to turn the reflection off.")
	ElseIf aiOption == _merchantsOption
		SetInfoText("General merchants stock hand and standing mirrors; changes apply when they restock.")
	ElseIf aiOption == _craftingOption
		SetInfoText("Make mirrors at a forge; a wooden hand mirror needs two firewood and one glass.")
    ElseIf aiOption == _modeOption
        SetInfoText("Automatic adjusts quality to performance, Preset offers three quality levels, and Manual allows you to set every option yourself.")
    ElseIf aiOption == _targetOption
        If MirrorsOfSkyrimNative.GetMirrorResolutionFollowsFramebuffer()
            SetInfoText("ENB sizes the reflection to the game framebuffer, so the Automatic frame-rate target does not apply.")
        Else
            SetInfoText("Automatic lowers reflection quality below this frame-rate target and restores it gradually when performance improves.")
        EndIf
    ElseIf aiOption == _effectiveOption || aiOption == _loweredEffectiveOption
        SetInfoText("Shows the reflection resolution and update rate currently in use.")
    ElseIf aiOption == _qualityLevelOption
        SetInfoText("Low reduces resolution and disables shadows, Medium balances detail and update rate, and High uses the largest captures every frame.")
    ElseIf aiOption == _shadowsOption
        SetInfoText("Show outdoor sun shadows in this mirror type; disabling them reduces rendering work.")
    ElseIf aiOption == _shadowResolutionOption
        SetInfoText("Resolution of outdoor scenery shadows for all mirrors; higher values improve definition and use more GPU time.")
    ElseIf aiOption == _playerShadowOption
        SetInfoText("Let your character cast shadows onto itself in reflections, such as the helmet or raised hand mirror on the face; shadows from the world stay either way.")
	ElseIf aiOption == _resolutionOption
		SetInfoText("Image resolution for standing, wall and Creation Kit mirrors. Lower values reduce GPU work and memory use.")
	ElseIf aiOption == _everyFrameOption
		SetInfoText("Every frame follows game FPS; at 60 FPS each visible mirror attempts 60 updates per second.")
	ElseIf aiOption == _refreshOption
		SetInfoText("How often mirrors update, 30 Hz by default. Strong impact on performance.")
	ElseIf aiOption == _handEveryFrameOption
		SetInfoText("Every frame follows game FPS; the hand mirror then attempts a new reflection on every frame it is visible.")
	ElseIf aiOption == _handRefreshOption
		SetInfoText("How often the raised hand mirror updates, 30 Hz by default. Strong impact on performance.")
	ElseIf aiOption == _handLoweredRefreshOption
		SetInfoText("How often the lowered hand mirror updates, 30 Hz by default. Strong impact on performance.")
	ElseIf aiOption == _zoomOption
		SetInfoText("Zoom level for the raised hand mirror: maximum shows just the face, minimum shows the full body")
	ElseIf aiOption == _wheelZoomOption
		SetInfoText("While the hand mirror is raised in first person, scroll up to zoom in and down to zoom out. Prevents the wheel from switching camera view. Menu scrolling is unchanged.")
	EndIf
EndEvent
