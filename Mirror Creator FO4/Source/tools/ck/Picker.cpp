#include "Picker.h"
#include "Win32.h"
#include <CommCtrl.h>

namespace MirrorCK {
namespace {
constexpr int kParts = 7101, kMessage = 7102;
struct State { const Model* model; int selected{-1}; };
HWND Control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    auto child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return child;
}
INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(l); SetWindowLongPtrW(dialog, DWLP_USER, l);
        SetWindowTextW(dialog, L"Alternate Textures — Realistic Reflections - Mirrors");
        RECT owner{}, rect{}; GetWindowRect(GetParent(dialog), &owner); GetWindowRect(dialog, &rect);
        SetWindowPos(dialog, nullptr, owner.left+((owner.right-owner.left)-(rect.right-rect.left))/2, owner.top+30, 780, 460, SWP_NOZORDER);
        Control(dialog, L"STATIC", L"Select the flat pane that should reflect.", 0, 16, 14, 740, 24, -1);
        auto list = Control(dialog, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 16, 42, 740, 265, kParts);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        int col{}; for (auto [label, width] : {std::pair{L"Mesh part", 200}, {L"Pane material", 320}, {L"Status", 195}}) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(label); column.cx = width; ListView_InsertColumn(list, col++, &column);
        }
        for (int i = 0; i < static_cast<int>(state->model->panes.size()); ++i) {
            const auto& pane = state->model->panes[i]; auto name = Wide(pane.name) + L" [" + std::to_wstring(pane.block) + L"]";
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = i; item.pszText = name.data(); ListView_InsertItem(list, &item);
            auto material = Wide(pane.material); ListView_SetItemText(list, i, 1, material.data());
            std::wstring status = pane.eligible() ? L"Flat pane — available" : L"Unavailable"; ListView_SetItemText(list, i, 2, status.data());
        }
        Control(dialog, L"STATIC", L"", 0, 16, 315, 740, 42, kMessage);
        Control(dialog, L"STATIC", L"Texture Set", 0, 16, 365, 80, 24, -1);
        auto choice = Control(dialog, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 100, 361, 300, 100, 7103);
        SendMessageW(choice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"MOF_MirrorSurface")); SendMessageW(choice, CB_SETCURSEL, 0, 0);
        Control(dialog, L"BUTTON", L"Assign", BS_DEFPUSHBUTTON | WS_TABSTOP, 556, 367, 94, 28, IDOK);
        Control(dialog, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 662, 367, 94, 28, IDCANCEL);
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        SetDlgItemTextW(dialog, kMessage, L"One flat, static part per object. The rest of the model keeps its materials.");
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(l)->idFrom == kParts) {
        int index = ListView_GetNextItem(GetDlgItem(dialog, kParts), -1, LVNI_SELECTED);
        state->selected = index; bool valid = index >= 0 && index < static_cast<int>(state->model->panes.size());
        if (valid) { const auto& pane = state->model->panes[index]; valid = pane.eligible();
            auto messageText = Wide(valid ? "This flat part can use MOF_MirrorSurface." : pane.reason); SetDlgItemTextW(dialog, kMessage, messageText.c_str()); }
        EnableWindow(GetDlgItem(dialog, IDOK), valid); return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(w) == IDOK && state->selected >= 0 &&
        state->selected < static_cast<int>(state->model->panes.size()) && state->model->panes[state->selected].eligible()) {
        EndDialog(dialog, state->selected+100); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}
}
int PickPane(HWND owner, const Model& model) {
    struct Template { DLGTEMPLATE dialog; WORD menu, type, title; };
    Template value{}; value.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME; value.dialog.cx = 480; value.dialog.cy = 280;
    State state{&model}; auto result = DialogBoxIndirectParamW(nullptr, &value.dialog, owner, Proc, reinterpret_cast<LPARAM>(&state));
    return result >= 100 ? static_cast<int>(result-100) : -1;
}
}
