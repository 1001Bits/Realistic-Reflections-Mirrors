Scriptname RRMirrorFurnishings extends Quest

Event OnInit()
    RegisterForSingleUpdate(2.0)
EndEvent

Event OnUpdate()
    RealisticReflectionsMirrorsNative.RefreshMirrorFurnishings()
    RegisterForSingleUpdate(2.0)
EndEvent
