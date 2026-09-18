Scriptname RealisticReflectionsMirrorsMCM extends SKI_ConfigBase
{Quality settings for authored reflective surfaces.}

Int _mode = -1
Int _inns = -1
Int _homes = -1
Int _target = -1
Int _quality = -1
Int _resolution = -1
Int _everyFrame = -1
Int _refresh = -1
Int _effective = -1
Int _debug = -1

Int Function GetVersion()
    Return 10
EndFunction

String[] Function ModeNames()
    String[] names = New String[3]
    names[0] = "Automatic"
    names[1] = "Preset"
    names[2] = "Manual"
    Return names
EndFunction

; Native/INI values stay Manual=0, Preset=1, Automatic=2 for saved settings.
Int Function ModeMenuIndex(Int nativeMode)
    If nativeMode < 0 || nativeMode > 2
        Return 0
    EndIf
    Return 2 - nativeMode
EndFunction

String[] Function QualityNames()
    String[] names = New String[3]
    names[0] = "Low"
    names[1] = "Medium"
    names[2] = "High"
    Return names
EndFunction

Function ResetPages()
    ModName = "Realistic Reflections"
EndFunction

Function ApplyFactoryDefaults()
    RealisticReflectionsMirrorsNative.SetMirrorInnsEnabled(True)
    RealisticReflectionsMirrorsNative.SetMirrorHomesEnabled(True)
    RealisticReflectionsMirrorsNative.SetMirrorDebugHotkeys(False)
    RealisticReflectionsMirrorsNative.SetMirrorQualityMode(2)
EndFunction

String Function EffectiveText()
    String text = ""
    If RealisticReflectionsMirrorsNative.GetMirrorResolutionFollowsFramebuffer()
        text = "ENB size"
    Else
        text = RealisticReflectionsMirrorsNative.GetMirrorEffectiveResolution() + "x" + RealisticReflectionsMirrorsNative.GetMirrorEffectiveHeight()
    EndIf
    Int hz = RealisticReflectionsMirrorsNative.GetMirrorEffectiveRefresh()
    If hz == 0
        text = text + ", every frame"
    Else
        text = text + ", " + hz + "Hz"
    EndIf
    Return text
EndFunction

Event OnConfigInit()
    ResetPages()
    ApplyFactoryDefaults()
EndEvent

Event OnVersionUpdate(Int aiVersion)
    ResetPages()
    If aiVersion >= 8 && CurrentVersion < 8
        ApplyFactoryDefaults()
    EndIf
    If CurrentVersion < 10
        RealisticReflectionsMirrorsNative.SetMirrorQualityMode(2)
    EndIf
EndEvent

Event OnPageReset(String asPage)
    _inns = -1
    _homes = -1
    _mode = -1
    _target = -1
    _quality = -1
    _resolution = -1
    _everyFrame = -1
    _refresh = -1
    _effective = -1
    _debug = -1
    SetCursorFillMode(TOP_TO_BOTTOM)
    _inns = AddToggleOption("Enable mirrors in inns", RealisticReflectionsMirrorsNative.GetMirrorInnsEnabled())
    _homes = AddToggleOption("Enable mirrors in player homes", RealisticReflectionsMirrorsNative.GetMirrorHomesEnabled())
    Int mode = RealisticReflectionsMirrorsNative.GetMirrorQualityMode()
    If mode < 0 || mode > 2
        mode = 2
    EndIf
    _mode = AddMenuOption("Quality mode", ModeNames()[ModeMenuIndex(mode)])
    Bool enb = RealisticReflectionsMirrorsNative.GetMirrorResolutionFollowsFramebuffer()
    If mode == 2
        If enb
            _target = AddTextOption("Target game FPS", "Unavailable with ENB", OPTION_FLAG_DISABLED)
        Else
            _target = AddSliderOption("Target game FPS", RealisticReflectionsMirrorsNative.GetMirrorTargetFPS() as Float, "{0} FPS")
        EndIf
    ElseIf mode == 1
        Int level = RealisticReflectionsMirrorsNative.GetMirrorQualityLevel()
        If level < 0 || level > 2
            level = 1
        EndIf
        _quality = AddMenuOption("Quality preset", QualityNames()[level])
    Else
        Int resolutionFlags = 0
        If enb
            resolutionFlags = OPTION_FLAG_DISABLED
        EndIf
        _resolution = AddSliderOption("Resolution", RealisticReflectionsMirrorsNative.GetPlacedMirrorResolution() as Float, "{0}", resolutionFlags)
        Int hz = RealisticReflectionsMirrorsNative.GetPlacedMirrorRefresh()
        _everyFrame = AddToggleOption("Update every game frame", hz == 0)
        Int refreshFlags = 0
        If hz == 0
            refreshFlags = OPTION_FLAG_DISABLED
            hz = 30
        EndIf
        _refresh = AddSliderOption("Refresh rate", hz as Float, "{0} Hz", refreshFlags)
    EndIf
    _effective = AddTextOption("Effective quality", EffectiveText())
    _debug = AddToggleOption("Development menu", RealisticReflectionsMirrorsNative.GetMirrorDebugHotkeys())
EndEvent

Event OnOptionSelect(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _inns
        RealisticReflectionsMirrorsNative.SetMirrorInnsEnabled(!RealisticReflectionsMirrorsNative.GetMirrorInnsEnabled())
    ElseIf aiOption == _homes
        RealisticReflectionsMirrorsNative.SetMirrorHomesEnabled(!RealisticReflectionsMirrorsNative.GetMirrorHomesEnabled())
    ElseIf aiOption == _everyFrame
        Int hz = 0
        If RealisticReflectionsMirrorsNative.GetPlacedMirrorRefresh() == 0
            hz = 30
        EndIf
        RealisticReflectionsMirrorsNative.SetPlacedMirrorRefresh(hz)
    ElseIf aiOption == _debug
        RealisticReflectionsMirrorsNative.SetMirrorDebugHotkeys(!RealisticReflectionsMirrorsNative.GetMirrorDebugHotkeys())
    EndIf
    ForcePageReset()
EndEvent

Event OnOptionMenuOpen(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _mode
        SetMenuDialogOptions(ModeNames())
        SetMenuDialogStartIndex(ModeMenuIndex(RealisticReflectionsMirrorsNative.GetMirrorQualityMode()))
        SetMenuDialogDefaultIndex(0)
    ElseIf aiOption == _quality
        SetMenuDialogOptions(QualityNames())
        SetMenuDialogStartIndex(RealisticReflectionsMirrorsNative.GetMirrorQualityLevel())
        SetMenuDialogDefaultIndex(1)
    EndIf
EndEvent

Event OnOptionMenuAccept(Int aiOption, Int auiIndex)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _mode
        If auiIndex >= 0 && auiIndex <= 2
            RealisticReflectionsMirrorsNative.SetMirrorQualityMode(2 - auiIndex)
        EndIf
    ElseIf aiOption == _quality
        RealisticReflectionsMirrorsNative.SetMirrorQualityLevel(auiIndex)
    EndIf
    ForcePageReset()
EndEvent

Event OnOptionSliderOpen(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _target
        SetSliderDialogStartValue(RealisticReflectionsMirrorsNative.GetMirrorTargetFPS() as Float)
        SetSliderDialogDefaultValue(60)
        SetSliderDialogRange(30, 144)
        SetSliderDialogInterval(1)
    ElseIf aiOption == _resolution
        SetSliderDialogStartValue(RealisticReflectionsMirrorsNative.GetPlacedMirrorResolution() as Float)
        SetSliderDialogDefaultValue(2048)
        SetSliderDialogRange(512, 4096)
        SetSliderDialogInterval(512)
    ElseIf aiOption == _refresh
        SetSliderDialogStartValue(RealisticReflectionsMirrorsNative.GetPlacedMirrorRefresh() as Float)
        SetSliderDialogDefaultValue(30)
        SetSliderDialogRange(30, 120)
        SetSliderDialogInterval(1)
    EndIf
EndEvent

Event OnOptionSliderAccept(Int aiOption, Float afValue)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _target
        RealisticReflectionsMirrorsNative.SetMirrorTargetFPS(afValue as Int)
    ElseIf aiOption == _resolution
        RealisticReflectionsMirrorsNative.SetPlacedMirrorResolution(afValue as Int)
    ElseIf aiOption == _refresh
        RealisticReflectionsMirrorsNative.SetPlacedMirrorRefresh(afValue as Int)
    EndIf
    ForcePageReset()
EndEvent

Event OnOptionHighlight(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _inns
        SetInfoText("Show or hide the mirrors added to inns.")
    ElseIf aiOption == _homes
        SetInfoText("Show or hide the mirrors added to player homes.")
    ElseIf aiOption == _mode
        SetInfoText("Automatic adjusts quality to performance, Preset offers three quality levels, and Manual allows you to set every option yourself.")
    ElseIf aiOption == _target
        If RealisticReflectionsMirrorsNative.GetMirrorResolutionFollowsFramebuffer()
            SetInfoText("ENB sizes the reflection to the game framebuffer, so the Automatic frame-rate target does not apply.")
        Else
            SetInfoText("Automatic lowers reflection quality below this frame-rate target and restores it gradually when performance improves.")
        EndIf
    ElseIf aiOption == _quality
        SetInfoText("Low uses 1024 at 30 Hz without shadows, Medium uses 2048 at 60 Hz, and High uses 4096 every frame.")
    ElseIf aiOption == _refresh || aiOption == _everyFrame
        SetInfoText("Choose how often the reflection updates, up to the game frame rate.")
    ElseIf aiOption == _resolution
        SetInfoText("Set the maximum reflection resolution, with smaller captures when the mirror covers less of the screen.")
    ElseIf aiOption == _effective
        SetInfoText("Shows the reflection resolution and update rate currently in use.")
    ElseIf aiOption == _debug
        SetInfoText("Enable the F8 development panel and its benchmark and performance controls.")
    EndIf
EndEvent

Event OnOptionDefault(Int aiOption)
    If aiOption < 0
        Return
    EndIf
    If aiOption == _inns
        RealisticReflectionsMirrorsNative.SetMirrorInnsEnabled(True)
    ElseIf aiOption == _homes
        RealisticReflectionsMirrorsNative.SetMirrorHomesEnabled(True)
    ElseIf aiOption == _mode
        RealisticReflectionsMirrorsNative.SetMirrorQualityMode(2)
    ElseIf aiOption == _target
        RealisticReflectionsMirrorsNative.SetMirrorTargetFPS(60)
    ElseIf aiOption == _quality
        RealisticReflectionsMirrorsNative.SetMirrorQualityLevel(1)
    ElseIf aiOption == _resolution
        RealisticReflectionsMirrorsNative.SetPlacedMirrorResolution(2048)
    ElseIf aiOption == _everyFrame || aiOption == _refresh
        RealisticReflectionsMirrorsNative.SetPlacedMirrorRefresh(30)
    ElseIf aiOption == _debug
        RealisticReflectionsMirrorsNative.SetMirrorDebugHotkeys(False)
    EndIf
    ForcePageReset()
EndEvent
