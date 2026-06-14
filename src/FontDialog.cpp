#include "FontDialog.h"

#include <strsafe.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "MessageDialog.h"
#include "UiSupport.h"

namespace NativePad {
LOGFONTW LogFontFromEditorFont(const NativePad::EditorFontSpec& font, UINT dpi) {
    LOGFONTW logFont{};
    logFont.lfHeight = -MulDiv(static_cast<int>(std::round(font.sizeDips * 72.0f / 96.0f)), static_cast<int>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi), 72);
    logFont.lfWeight = font.weight;
    logFont.lfItalic = font.italic ? TRUE : FALSE;
    StringCchCopyW(logFont.lfFaceName, std::size(logFont.lfFaceName), font.family.c_str());
    return logFont;
}

namespace {

constexpr wchar_t kFontDialogClass[] = L"NativePadFontDialog";
constexpr int kFontFamilyEditId = 50201;
constexpr int kFontFamilyListId = 50202;
constexpr int kFontStyleEditId = 50203;
constexpr int kFontStyleListId = 50204;
constexpr int kFontSizeEditId = 50205;
constexpr int kFontSizeListId = 50206;
constexpr int kFontPreviewId = 50207;
NativePad::EditorFontSpec EditorFontFromLogFont(const LOGFONTW& logFont, int pointSizeTenths, UINT dpi) {
    const float pointSize = pointSizeTenths > 0
                                ? static_cast<float>(pointSizeTenths) / 10.0f
                                : static_cast<float>(std::abs(logFont.lfHeight) * 72) / static_cast<float>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi);

    NativePad::EditorFontSpec font{};
    font.family = logFont.lfFaceName[0] != L'\0' ? logFont.lfFaceName : L"Consolas";
    font.sizeDips = std::max(6.0f, pointSize * 96.0f / 72.0f);
    font.weight = logFont.lfWeight == 0 ? FW_NORMAL : logFont.lfWeight;
    font.italic = logFont.lfItalic != FALSE;
    return font;
}

struct FontStyleChoice {
    const wchar_t* name;
    LONG weight;
    bool italic;
};

constexpr std::array<FontStyleChoice, 4> kFontStyleChoices{{
    {L"Regular", FW_NORMAL, false},
    {L"Italic", FW_NORMAL, true},
    {L"Bold", FW_BOLD, false},
    {L"Bold Italic", FW_BOLD, true},
}};

struct FontDialogState {
    // Custom replacement for ChooseFont. It gives us control over dark-mode list
    // painting, DPI layout, and the resizable sample preview.
    HWND hwnd{};
    HWND owner{};
    HWND familyLabel{};
    HWND familyEdit{};
    HWND familyList{};
    HWND styleLabel{};
    HWND styleEdit{};
    HWND styleList{};
    HWND sizeLabel{};
    HWND sizeEdit{};
    HWND sizeList{};
    HWND sampleLabel{};
    HWND sample{};
    HWND ok{};
    HWND cancel{};
    HINSTANCE instance{};
    HFONT uiFont{};
    HFONT previewFont{};
    HBRUSH backgroundBrush{};
    HBRUSH controlBrush{};
    UINT dpi{USER_DEFAULT_SCREEN_DPI};
    bool dark{false};
    bool accepted{false};
    NativePad::EditorFontSpec initialFont{};
    NativePad::EditorFontSpec selectedFont{};
    std::vector<std::wstring> families;
    std::vector<int> sizes;
};

std::array<HWND, 13> FontDialogControls(FontDialogState* state) {
    if (state == nullptr) {
        return {};
    }

    return {
        state->familyLabel,
        state->familyEdit,
        state->familyList,
        state->styleLabel,
        state->styleEdit,
        state->styleList,
        state->sizeLabel,
        state->sizeEdit,
        state->sizeList,
        state->sampleLabel,
        state->sample,
        state->ok,
        state->cancel,
    };
}

void RedrawFontDialogAfterLayout(FontDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    for (HWND control : FontDialogControls(state)) {
        if (control != nullptr) {
            RedrawWindow(control, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
    }

    RedrawWindow(state->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

float FontPointSize(const NativePad::EditorFontSpec& font) {
    return font.sizeDips * 72.0f / 96.0f;
}

std::wstring FormatPointSize(float pointSize) {
    wchar_t buffer[32]{};
    const float rounded = std::round(pointSize);
    if (std::fabs(pointSize - rounded) < 0.05f) {
        StringCchPrintfW(buffer, std::size(buffer), L"%d", static_cast<int>(rounded));
    } else {
        StringCchPrintfW(buffer, std::size(buffer), L"%.1f", pointSize);
    }
    return buffer;
}

std::wstring Trim(std::wstring text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    }).base();

    if (first >= last) {
        return {};
    }

    return std::wstring(first, last);
}

bool TryParsePointSize(const std::wstring& text, float& pointSize) {
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    wchar_t* end = nullptr;
    const float value = wcstof(trimmed.c_str(), &end);
    while (end != nullptr && std::iswspace(*end) != 0) {
        ++end;
    }

    if (end == trimmed.c_str() || (end != nullptr && *end != L'\0') || !std::isfinite(value)) {
        return false;
    }

    pointSize = value;
    return pointSize >= 1.0f && pointSize <= 200.0f;
}

int CALLBACK EnumFontFamilyProc(const LOGFONTW* logFont, const TEXTMETRICW*, DWORD, LPARAM param) {
    auto* families = reinterpret_cast<std::vector<std::wstring>*>(param);
    if (families == nullptr || logFont == nullptr || logFont->lfFaceName[0] == L'\0' || logFont->lfFaceName[0] == L'@') {
        return 1;
    }

    families->push_back(logFont->lfFaceName);
    return 1;
}

std::vector<std::wstring> EnumerateFontFamilies() {
    std::vector<std::wstring> families;
    HDC dc = GetDC(nullptr);
    if (dc != nullptr) {
        LOGFONTW query{};
        query.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExW(dc, &query, EnumFontFamilyProc, reinterpret_cast<LPARAM>(&families), 0);
        ReleaseDC(nullptr, dc);
    }

    std::sort(families.begin(), families.end(), [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    families.erase(std::unique(families.begin(), families.end(), [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }), families.end());

    if (families.empty()) {
        families.push_back(L"Consolas");
    }

    return families;
}

int FontStyleIndexFor(const NativePad::EditorFontSpec& font) {
    const bool bold = font.weight >= FW_SEMIBOLD;
    if (bold && font.italic) {
        return 3;
    }
    if (bold) {
        return 2;
    }
    if (font.italic) {
        return 1;
    }
    return 0;
}

void FillFontSizeChoices(FontDialogState* state) {
    state->sizes = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
    const int current = static_cast<int>(std::round(FontPointSize(state->initialFont)));
    if (current > 0 && std::find(state->sizes.begin(), state->sizes.end(), current) == state->sizes.end()) {
        state->sizes.push_back(current);
        std::sort(state->sizes.begin(), state->sizes.end());
    }
}

void SelectListBoxText(HWND list, const std::wstring& text) {
    if (list == nullptr) {
        return;
    }

    LRESULT index = SendMessageW(list, LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
    if (index == LB_ERR) {
        index = SendMessageW(list, LB_FINDSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str()));
    }
    if (index != LB_ERR) {
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }
}

void PopulateFontDialog(FontDialogState* state) {
    state->families = EnumerateFontFamilies();
    for (const auto& family : state->families) {
        SendMessageW(state->familyList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(family.c_str()));
    }

    for (const auto& style : kFontStyleChoices) {
        SendMessageW(state->styleList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(style.name));
    }

    FillFontSizeChoices(state);
    for (int size : state->sizes) {
        const std::wstring text = std::to_wstring(size);
        SendMessageW(state->sizeList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    SetControlText(state->familyEdit, state->initialFont.family);
    SelectListBoxText(state->familyList, state->initialFont.family);

    const int styleIndex = FontStyleIndexFor(state->initialFont);
    SendMessageW(state->styleList, LB_SETCURSEL, static_cast<WPARAM>(styleIndex), 0);
    SetControlText(state->styleEdit, kFontStyleChoices[static_cast<size_t>(styleIndex)].name);

    const std::wstring sizeText = FormatPointSize(FontPointSize(state->initialFont));
    SetControlText(state->sizeEdit, sizeText);
    SelectListBoxText(state->sizeList, sizeText);
}

bool ReadFontDialogSelection(FontDialogState* state, NativePad::EditorFontSpec& font, bool showErrors) {
    std::wstring family = Trim(ControlText(state->familyEdit));
    if (family.empty()) {
        if (showErrors) {
            ShowMessageDialog(
                state->hwnd,
                state->instance,
                state->dpi,
                state->dark,
                L"NativePad - Font",
                L"Choose a font family.",
                MessageDialogIcon::Information);
            SetFocus(state->familyEdit);
        }
        return false;
    }

    float pointSize = 0.0f;
    if (!TryParsePointSize(ControlText(state->sizeEdit), pointSize)) {
        if (showErrors) {
            ShowMessageDialog(
                state->hwnd,
                state->instance,
                state->dpi,
                state->dark,
                L"NativePad - Font",
                L"Font size must be between 1 and 200 points.",
                MessageDialogIcon::Information);
            SetFocus(state->sizeEdit);
            SendMessageW(state->sizeEdit, EM_SETSEL, 0, -1);
        }
        return false;
    }

    LRESULT styleIndex = SendMessageW(state->styleList, LB_GETCURSEL, 0, 0);
    if (styleIndex == LB_ERR) {
        styleIndex = 0;
    }
    const auto& style = kFontStyleChoices[static_cast<size_t>(std::clamp<LRESULT>(styleIndex, 0, static_cast<LRESULT>(kFontStyleChoices.size() - 1)))];

    font.family = family;
    font.sizeDips = pointSize * 96.0f / 72.0f;
    font.weight = style.weight;
    font.italic = style.italic;
    return true;
}

void UpdateFontPreview(FontDialogState* state) {
    if (state == nullptr) {
        return;
    }

    NativePad::EditorFontSpec font{};
    if (!ReadFontDialogSelection(state, font, false)) {
        font = state->initialFont;
    }

    LOGFONTW logFont = LogFontFromEditorFont(font, state->dpi);
    HFONT preview = CreateFontIndirectW(&logFont);
    if (preview != nullptr) {
        if (state->previewFont != nullptr) {
            DeleteObject(state->previewFont);
        }
        state->previewFont = preview;
    }

    InvalidateRect(state->sample, nullptr, TRUE);
}

void LayoutFontDialog(FontDialogState* state) {
    // Extra height is shared between the list boxes and preview area, while OK
    // and Cancel stay anchored to the lower-right corner.
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int margin = ScaleForDpi(14, state->dpi);
    const int gap = ScaleForDpi(10, state->dpi);
    const int labelHeight = ScaleForDpi(18, state->dpi);
    const int editHeight = ScaleForDpi(24, state->dpi);
    const int buttonWidth = ScaleForDpi(82, state->dpi);
    const int buttonHeight = ScaleForDpi(28, state->dpi);
    const int sizeWidth = ScaleForDpi(78, state->dpi);
    const int styleWidth = ScaleForDpi(132, state->dpi);
    const int clientWidth = static_cast<int>(client.right - client.left);
    const int clientHeight = static_cast<int>(client.bottom - client.top);
    const int fullWidth = std::max(ScaleForDpi(390, state->dpi), clientWidth - (margin * 2));
    const int familyWidth = std::max(ScaleForDpi(160, state->dpi), fullWidth - styleWidth - sizeWidth - (gap * 2));
    const int top = margin;
    const int editTop = top + labelHeight + ScaleForDpi(2, state->dpi);
    const int listTop = editTop + editHeight + ScaleForDpi(4, state->dpi);
    const int styleLeft = margin + familyWidth + gap;
    const int sizeLeft = styleLeft + styleWidth + gap;
    const int buttonTop = clientHeight - margin - buttonHeight;
    const int sampleMinHeight = ScaleForDpi(84, state->dpi);
    const int listMinHeight = ScaleForDpi(148, state->dpi);
    const int listMaxHeight = ScaleForDpi(320, state->dpi);
    const int baseClientHeight = ScaleForDpi(397, state->dpi);
    const int extraHeight = std::max(0, clientHeight - baseClientHeight);
    const int listHeight = std::clamp(listMinHeight + (extraHeight / 2), listMinHeight, listMaxHeight);

    MoveWindow(state->familyLabel, margin, top, familyWidth, labelHeight, TRUE);
    MoveBorderedControl(state->familyEdit, margin, editTop, familyWidth, editHeight);
    MoveBorderedControl(state->familyList, margin, listTop, familyWidth, listHeight);

    MoveWindow(state->styleLabel, styleLeft, top, styleWidth, labelHeight, TRUE);
    MoveBorderedControl(state->styleEdit, styleLeft, editTop, styleWidth, editHeight);
    MoveBorderedControl(state->styleList, styleLeft, listTop, styleWidth, listHeight);

    MoveWindow(state->sizeLabel, sizeLeft, top, sizeWidth, labelHeight, TRUE);
    MoveBorderedControl(state->sizeEdit, sizeLeft, editTop, sizeWidth, editHeight);
    MoveBorderedControl(state->sizeList, sizeLeft, listTop, sizeWidth, listHeight);

    const int sampleLabelTop = listTop + listHeight + ScaleForDpi(6, state->dpi);
    const int sampleTop = sampleLabelTop + labelHeight + ScaleForDpi(4, state->dpi);
    const int sampleHeight = std::max<int>(
        sampleMinHeight,
        buttonTop - sampleTop - ScaleForDpi(16, state->dpi));
    MoveWindow(state->sampleLabel, margin, sampleLabelTop, fullWidth, labelHeight, TRUE);
    MoveWindow(state->sample, margin, sampleTop, fullWidth, sampleHeight, TRUE);

    MoveWindow(state->cancel, clientWidth - margin - buttonWidth, buttonTop, buttonWidth, buttonHeight, TRUE);
    MoveWindow(state->ok, clientWidth - margin - (buttonWidth * 2) - gap, buttonTop, buttonWidth, buttonHeight, TRUE);
    RedrawFontDialogAfterLayout(state);
}

LRESULT FontDialogCtlColor(FontDialogState* state, UINT message, WPARAM wParam) {
    if (state == nullptr) {
        return 0;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, colors.text);
    SetBkMode(dc, TRANSPARENT);

    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        SetBkColor(dc, colors.controlBackground);
        return reinterpret_cast<LRESULT>(state->controlBrush);
    }

    SetBkColor(dc, colors.background);
    return reinterpret_cast<LRESULT>(state->backgroundBrush);
}

void DrawFontPreview(FontDialogState* state, DRAWITEMSTRUCT* draw) {
    if (state == nullptr || draw == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    HBRUSH background = CreateSolidBrush(colors.controlBackground);
    FillRect(draw->hDC, &draw->rcItem, background);
    DeleteObject(background);

    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.border);
    HGDIOBJ oldPen = SelectObject(draw->hDC, borderPen);
    HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
    SelectObject(draw->hDC, oldBrush);
    SelectObject(draw->hDC, oldPen);
    DeleteObject(borderPen);

    HFONT font = state->previewFont != nullptr ? state->previewFont : state->uiFont;
    HGDIOBJ oldFont = SelectObject(draw->hDC, font);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, colors.text);
    RECT textRect = draw->rcItem;
    InflateRect(&textRect, -ScaleForDpi(12, state->dpi), -ScaleForDpi(8, state->dpi));
    DrawTextW(draw->hDC, L"AaBbYyZz", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(draw->hDC, oldFont);
}

bool IsFontDialogListId(UINT id) {
    return id == kFontFamilyListId || id == kFontStyleListId || id == kFontSizeListId;
}

void DrawFontListItem(FontDialogState* state, DRAWITEMSTRUCT* draw) {
    // Owner-draw avoids light listbox backgrounds and stale native focus marks
    // while scrolling or resizing in dark mode.
    if (state == nullptr || draw == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
    const COLORREF backgroundColor = selected ? colors.selectionBackground : colors.controlBackground;
    const COLORREF textColor = disabled ? colors.disabledText : (selected ? colors.selectionText : colors.text);

    HBRUSH background = CreateSolidBrush(backgroundColor);
    FillRect(draw->hDC, &draw->rcItem, background);
    DeleteObject(background);

    if (draw->itemID != static_cast<UINT>(-1)) {
        const LRESULT length = SendMessageW(draw->hwndItem, LB_GETTEXTLEN, draw->itemID, 0);
        if (length != LB_ERR) {
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<size_t>(length));

            HFONT font = state->uiFont != nullptr ? state->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(draw->hDC, font);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, textColor);
            RECT textRect = draw->rcItem;
            textRect.left += ScaleForDpi(4, state->dpi);
            textRect.right -= ScaleForDpi(4, state->dpi);
            DrawTextW(draw->hDC, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(draw->hDC, oldFont);
        }
    }
}

void ApplyFontDialogTheme(FontDialogState* state) {
    if (state == nullptr) {
        return;
    }

    ApplyDarkFrame(state->hwnd, state->dark);
    const std::array<HWND, 15> controls{
        state->familyLabel,
        state->familyEdit,
        state->familyList,
        state->styleLabel,
        state->styleEdit,
        state->styleList,
        state->sizeLabel,
        state->sizeEdit,
        state->sizeList,
        state->sampleLabel,
        state->sample,
        state->ok,
        state->cancel,
        nullptr,
        nullptr,
    };
    for (HWND control : controls) {
        ApplyDialogControlTheme(control, state->dark);
    }
}

void PaintFontDialog(FontDialogState* state) {
    if (state == nullptr || state->hwnd == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(state->hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    const DialogColors colors = DialogColorsForTheme(state->dark);
    FillRect(hdc, &ps.rcPaint, state->backgroundBrush);
    DrawDialogChildBorder(state->hwnd, state->familyEdit, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->familyList, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->styleEdit, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->styleList, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->sizeEdit, hdc, colors);
    DrawDialogChildBorder(state->hwnd, state->sizeList, hdc, colors);
    EndPaint(state->hwnd, &ps);
}

LRESULT CALLBACK FontDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<FontDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<FontDialogState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = state != nullptr ? state->dpi : USER_DEFAULT_SCREEN_DPI;
        info->ptMinTrackSize.x = ScaleForDpi(532, dpi);
        info->ptMinTrackSize.y = ScaleForDpi(438, dpi);
        return 0;
    }
    case WM_CREATE: {
        const DialogColors colors = DialogColorsForTheme(state->dark);
        state->backgroundBrush = CreateSolidBrush(colors.background);
        state->controlBrush = CreateSolidBrush(colors.controlBackground);
        state->uiFont = CreateUiFontForDpi(state->dpi);

        state->familyLabel = CreateWindowExW(0, L"STATIC", L"Font:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->familyEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontFamilyEditId)), nullptr, nullptr);
        state->familyList = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontFamilyListId)), nullptr, nullptr);

        state->styleLabel = CreateWindowExW(0, L"STATIC", L"Font style:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->styleEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontStyleEditId)), nullptr, nullptr);
        state->styleList = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontStyleListId)), nullptr, nullptr);

        state->sizeLabel = CreateWindowExW(0, L"STATIC", L"Size:", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->sizeEdit = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontSizeEditId)), nullptr, nullptr);
        state->sizeList = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontSizeListId)), nullptr, nullptr);

        state->sampleLabel = CreateWindowExW(0, L"STATIC", L"Sample", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        state->sample = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontPreviewId)), nullptr, nullptr);
        state->ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        state->cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        const std::array<HWND, 15> controls{
            state->familyLabel,
            state->familyEdit,
            state->familyList,
            state->styleLabel,
            state->styleEdit,
            state->styleList,
            state->sizeLabel,
            state->sizeEdit,
            state->sizeList,
            state->sampleLabel,
            state->sample,
            state->ok,
            state->cancel,
            nullptr,
            nullptr,
        };
        for (HWND control : controls) {
            SetControlFont(control, state->uiFont);
        }

        PopulateFontDialog(state);
        ApplyFontDialogTheme(state);
        LayoutFontDialog(state);
        UpdateFontPreview(state);
        SetFocus(state->familyEdit);
        SendMessageW(state->familyEdit, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        return 0;
    }
    case WM_SIZE:
        LayoutFontDialog(state);
        return 0;
    case WM_PAINT:
        PaintFontDialog(state);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            NativePad::EditorFontSpec font{};
            if (ReadFontDialogSelection(state, font, true)) {
                state->selectedFont = font;
                state->accepted = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        case kFontFamilyListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(state->familyList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    std::array<wchar_t, LF_FACESIZE> family{};
                    SendMessageW(state->familyList, LB_GETTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(family.data()));
                    SetControlText(state->familyEdit, family.data());
                }
                UpdateFontPreview(state);
            }
            return 0;
        case kFontStyleListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(state->styleList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    SetControlText(state->styleEdit, kFontStyleChoices[static_cast<size_t>(index)].name);
                }
                UpdateFontPreview(state);
            }
            return 0;
        case kFontSizeListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT index = SendMessageW(state->sizeList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    std::array<wchar_t, 32> size{};
                    SendMessageW(state->sizeList, LB_GETTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(size.data()));
                    SetControlText(state->sizeEdit, size.data());
                }
                UpdateFontPreview(state);
            }
            return 0;
        case kFontFamilyEditId:
        case kFontSizeEditId:
            if (HIWORD(wParam) == EN_CHANGE) {
                UpdateFontPreview(state);
            }
            return 0;
        default:
            break;
        }
        break;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure != nullptr && IsFontDialogListId(measure->CtlID)) {
            measure->itemHeight = static_cast<UINT>(ScaleForDpi(24, state != nullptr ? state->dpi : USER_DEFAULT_SCREEN_DPI));
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw == nullptr) {
            return FALSE;
        }
        if (draw->CtlID == kFontPreviewId) {
            DrawFontPreview(state, draw);
            return TRUE;
        }
        if (IsFontDialogListId(draw->CtlID)) {
            DrawFontListItem(state, draw);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return FontDialogCtlColor(state, message, wParam);
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        DeleteUiFont(state != nullptr ? state->uiFont : nullptr);
        if (state != nullptr) {
            if (state->previewFont != nullptr) {
                DeleteObject(state->previewFont);
            }
            if (state->backgroundBrush != nullptr) {
                DeleteObject(state->backgroundBrush);
            }
            if (state->controlBrush != nullptr) {
                DeleteObject(state->controlBrush);
            }
            state->uiFont = nullptr;
            state->previewFont = nullptr;
            state->backgroundBrush = nullptr;
            state->controlBrush = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace
std::optional<NativePad::EditorFontSpec> ShowFontDialog(HWND owner, HINSTANCE instance, const NativePad::EditorFontSpec& currentFont, UINT dpi, bool dark) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &FontDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kFontDialogClass;
    AssignWindowClassIcons(wc, instance);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ShowMessageDialog(owner, instance, dpi, dark, L"NativePad - Font", GetLastErrorText(), MessageDialogIcon::Error);
        return std::nullopt;
    }

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    dpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
    const int width = ScaleForDpi(532, dpi);
    const int height = ScaleForDpi(438, dpi);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left - width) / 2);
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top - height) / 2);

    FontDialogState state{};
    state.owner = owner;
    state.instance = instance;
    state.dpi = dpi;
    state.dark = dark;
    state.initialFont = currentFont;
    state.selectedFont = currentFont;

    HWND dialog = CreateWindowExW(
        WS_EX_WINDOWEDGE | WS_EX_CONTROLPARENT,
        kFontDialogClass,
        L"Font",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr) {
        ShowMessageDialog(owner, instance, dpi, dark, L"NativePad - Font", GetLastErrorText(), MessageDialogIcon::Error);
        return std::nullopt;
    }
    ApplyWindowIcons(dialog, instance);

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (MessageTargetsWindow(dialog, message) && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
            DestroyWindow(dialog);
            continue;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetFocus(owner);

    if (state.accepted) {
        return state.selectedFont;
    }

    return std::nullopt;
}
} // namespace NativePad
