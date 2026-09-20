unit ValidateCKHelper;
var Errors, Lines: TStringList; Checks: Integer;

procedure Require(OK: Boolean; MessageText: string);
begin
  Inc(Checks);
  if not OK then Errors.Add(MessageText);
end;

procedure CheckTree(e: IInterface);
var i: Integer; MessageText: string;
begin
  Inc(Checks); MessageText := Check(e);
  if MessageText <> '' then Errors.Add(FullPath(e) + ': ' + MessageText);
  for i := 0 to Pred(ElementCount(e)) do CheckTree(ElementByIndex(e, i));
end;

function Initialize: Integer;
var Plugin, Group, Item, Assignment, Swap, Entries, Entry: IInterface;
    i,j,k,Statics,Swaps,Markers: Integer; EDID,Original,Replacement: string;
begin
  Result := 1; Checks := 0; Statics := 0; Swaps := 0;
  Errors := TStringList.Create; Lines := TStringList.Create;
  try
    try
      Plugin := FileByName('MirrorsOfFallout-HelperTest.esp');
      Require(Assigned(Plugin), 'Native CK-saved fixture missing');
      Require(MasterCount(Plugin) = 2, 'CK must retain Fallout4.esm and the shared mirror ESP');
      for i := 0 to Pred(ElementCount(Plugin)) do begin
        Group := ElementByIndex(Plugin,i); CheckTree(Group);
        if ElementType(Group) = etGroupRecord then
          for j := 0 to Pred(ElementCount(Group)) do begin
            Item := ElementByIndex(Group,j); EDID := EditorID(Item);
            if Signature(Item) = 'STAT' then begin
              Inc(Statics); Assignment := ElementByPath(Item,'Model\MODS');
              if (EDID = 'MOFCKHelperNew') or (EDID = 'MOFCKHelperPreserve') then begin
                Require(Assigned(Assignment),EDID+': mirror swap missing');
                Swap := LinksTo(Assignment); Require(Signature(Swap) = 'MSWP',EDID+': swap link does not resolve');
                Entries := ElementByPath(Swap,'Material Substitutions'); Markers := 0;
                for k := 0 to Pred(ElementCount(Entries)) do begin
                  Entry := ElementByIndex(Entries,k);
                  Original := LowerCase(GetElementEditValues(Entry,'BNAM'));
                  Replacement := LowerCase(GetElementEditValues(Entry,'SNAM'));
                  if Replacement = 'mirrorsoffallout\authoring\mof_mirrorsurface.bgsm' then begin
                    Inc(Markers);
                    Require(Original = 'setdressing\playerhouse\playerhouse_bathroommirrorenv01.bgsm',EDID+': wrong tagged pane');
                  end;
                end;
                Require(Markers = 1,EDID+': exactly one marker required');
                if EDID = 'MOFCKHelperPreserve' then Require(ElementCount(Entries) = 3,'Existing remap/unused entries lost')
                else Require(ElementCount(Entries) = 1,'Unexpected new swap entries');
              end else if EDID = 'MOFCKHelperSharedSwapUser' then
                Require(EditorID(LinksTo(Assignment)) = 'MOFCKHelperFrameSwap','Shared swap user was changed')
              else Require(not Assigned(Assignment),'Canceled/rejected Static was tagged');
            end else if Signature(Item) = 'MSWP' then Inc(Swaps)
            else Errors.Add('Unexpected record: '+Signature(Item));
          end;
      end;
      Require(Statics = 6,'Expected six test Statics');
      Require(Swaps = 3,'Expected source swap and two committed custom swaps; no orphan after cancel');
      if Errors.Count = 0 then Lines.Add('PASS: '+IntToStr(Checks)+' checks of the native CK-saved ESP, resolved material swaps, and canceled/rejected records')
      else Lines.Add('FAIL: '+Errors.Text);
    except on E: Exception do Lines.Add('FAIL: '+E.Message); end;
    Lines.SaveToFile(DataPath+'result.txt');
  finally Lines.Free; Errors.Free; end;
end;
end.
