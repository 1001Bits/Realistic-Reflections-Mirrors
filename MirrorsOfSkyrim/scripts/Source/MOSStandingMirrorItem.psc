Scriptname MOSStandingMirrorItem extends ObjectReference
{Inventory bridge for the native Mirrors of Skyrim placement controller.}

MiscObject Property PlacementItem Auto

Event OnEquipped(Actor akActor)
	If akActor == Game.GetPlayer() && PlacementItem
		MOSStandingMirrorNative.BeginPlacement(PlacementItem.GetFormID())
	EndIf
EndEvent
