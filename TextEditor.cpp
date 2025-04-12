#include "framework.h"
#include "TextEditor.h"
#include <commctrl.h>
#include <commdlg.h> //  header for OPENFILENAME
#include <shlobj.h>
#include <shlwapi.h>
#include <richedit.h>
#include <vector>
#include <string>
#include <fstream>  // Added for file operations
#include <sstream>  // Added for reading file to string
#include <map>
#include "VersionHistoryManager.h"
#include "TextChange.h"
#include <Windows.h>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

#define WM_POST_APPLY_CHANGE (WM_USER + 100)


// Global Variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hTabCtrl;
HWND hToolBar;

struct EditorTabInfo {
    HWND hEdit = NULL;
    std::wstring filePath = L"";
    std::wstring fileName = L"Untitled";
    std::unique_ptr<VersionHistoryManager> historyManager = nullptr;
    bool isModified = false; // Track modification status
    // Store the text state *before* the current change for diffing
    std::wstring textBeforeChange = L"";
    bool processingChange = false; // Flag to prevent re-entrancy during change handling
};
std::vector<EditorTabInfo> openTabs;
int currentTab = -1;

// Forward declarations
ATOM                MyRegisterClass(HINSTANCE hInstance);
bool                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    CommandPaletteProc(HWND, UINT, WPARAM, LPARAM);
void                CreateTab(HWND hWnd, const WCHAR* title, const WCHAR* filePath);
void                SwitchToTab(int index);
void                CloseTab(int index);
void                CreateToolbar(HWND hWnd);
void                CreateTabControl(HWND hWnd);
HWND                CreateRichEdit(HWND hWnd);
void                ResizeControls(HWND hWnd);
void                ShowCommandPalette(HWND hWnd);
void                ShowPopupMenu(HWND hWnd, int menuId, int x, int y);
bool                LoadFileIntoEditor(HWND hEdit, const WCHAR* filePath, std::wstring&);
bool                SaveEditorContent(int tabIndex, bool saveAs);
void                UpdateTabTitle(int index);

void                ApplyChangeToRichEdit(HWND hEdit, const TextChange& change); 
std::wstring        GetRichEditText(HWND hEdit); 
TextChange          CalculateTextChange(const std::wstring& before, const std::wstring& after, HWND hEdit); 
void                UpdateWindowTitle(HWND hWnd); 
void                ShowHistoryTree(HWND hWnd); // history UI 


const WCHAR* commands[] = {
    L"New File",
    L"Open File",
    L"Save File",
    L"Exit"
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize common controls
    INITCOMMONCONTROLSEX icc = { 0 };
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES | ICC_USEREX_CLASSES; 
    InitCommonControlsEx(&icc);

    // Load Rich Edit library
    LoadLibrary(TEXT("Msftedit.dll")); 

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TEXTEDITOR, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    MSG msg;
    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TEXTEDITOR));

    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TEXTEDITOR));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr; // No main menu bar
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

bool InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr); // Use CW_USEDEFAULT for size

    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

void CreateToolbar(HWND hWnd)
{
    // Create toolbar
    hToolBar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP, 
        0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR, hInst, NULL);

    // Add buttons - Using TBSTYLE_DROPDOWN for menus
    TBBUTTON tbb[4];
    ZeroMemory(tbb, sizeof(tbb));

    // File dropdown
    tbb[0].idCommand = ID_FILE_NEW; // Use ID_FILE_NEW as placeholder ID for the button itself
    tbb[0].fsState = TBSTATE_ENABLED;
    tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN; 
    tbb[0].iString = (INT_PTR)L"File";

    // Theme dropdown
    tbb[1].idCommand = IDM_THEME_MENU;
    tbb[1].fsState = TBSTATE_ENABLED;
    tbb[1].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN; 
    tbb[1].iString = (INT_PTR)L"Theme";

    // Help dropdown
    tbb[2].idCommand = IDM_ABOUT; // Placeholder ID
    tbb[2].fsState = TBSTATE_ENABLED;
    tbb[2].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN; 
    tbb[2].iString = (INT_PTR)L"Help";

    //EDIT dropdown
    tbb[3].idCommand = ID_EDIT_UNDO; // Placeholder ID for the button itself
    tbb[3].fsState = TBSTATE_ENABLED;
    tbb[3].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN;
    tbb[3].iString = (INT_PTR)L"Edit";

    SendMessage(hToolBar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(hToolBar, TB_ADDBUTTONS, (WPARAM)4, (LPARAM)&tbb);
    SendMessage(hToolBar, TB_AUTOSIZE, 0, 0);
}

void CreateTabControl(HWND hWnd)
{
    // Create tab control
    hTabCtrl = CreateWindowEx(0, WC_TABCONTROL, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_TOOLTIPS, 
        0, 0, 0, 0, hWnd, (HMENU)IDC_TABCTRL, hInst, NULL);

    // Set font for tabs
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hTabCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
}

HWND CreateRichEdit(HWND hWnd)
{
    // Ensure Rich Edit library is loaded (redundant if loaded in WinMain, but safe)

    HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, // Added ES_WANTRETUR
        0, 0, 0, 0,
        hWnd, (HMENU)IDC_TEXTBOX, hInst, NULL);

    if (!hEdit) {
        MessageBox(hWnd, L"Failed to create Rich Edit control.", L"Error", MB_OK | MB_ICONERROR);
        return NULL;
    }

    // Set custom font (e.g., Consolas 10pt)
    LOGFONT lf = { 0 };
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Consolas");
    lf.lfHeight = -MulDiv(10, GetDeviceCaps(GetDC(hEdit), LOGPIXELSY), 72); // 10pt
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY; 
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;

    HFONT hFont = CreateFontIndirect(&lf);
    if (hFont) {
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        // Note: Do not delete hFont here if the control uses it. It will be cleaned up later.
        // Or, manage font lifecycle carefully. For simplicity here, we leak it.
    }


    // Enable automatic detection of URLs
    SendMessage(hEdit, EM_AUTOURLDETECT, TRUE, 0);

    // Set event mask to receive notifications (optional, for change tracking etc.)
    // Request EN_CHANGE for text changes and EN_SELCHANGE for selection/cursor moves
    DWORD eventMask = ENM_CHANGE | ENM_SELCHANGE;
   
    // Set the event mask
    SendMessage(hEdit, EM_SETEVENTMASK, 0, eventMask);


    return hEdit;
}

std::wstring GetRichEditText(HWND hEdit) {
    if (!hEdit) return L"";
    int textLen = GetWindowTextLengthW(hEdit);
    if (textLen <= 0) return L""; // Handle 0 length or error

    std::wstring buffer;
    buffer.resize(textLen + 1); // +1 for null terminator safety
    GetWindowTextW(hEdit, &buffer[0], textLen + 1);
    buffer.resize(textLen); // Remove trailing null GetWindowText adds
    return buffer;
}

//void ApplyChangeToRichEdit(HWND hEdit, const TextChange& change) {
//    if (!hEdit) return;
//
//    // This function needs to apply the change *without* triggering
//    // another EN_CHANGE that our history manager would record.
//    // We'll use a flag in the EditorTabInfo for this.
//
//    // Find the tab associated with this hEdit
//    int tabIndex = -1;
//    for (int i = 0; i < openTabs.size(); ++i) {
//        if (openTabs[i].hEdit == hEdit) {
//            tabIndex = i;
//            break;
//        }
//    }
//    if (tabIndex == -1) return; // Should not happen
//
//    // Set flag to ignore the upcoming EN_CHANGE
//    openTabs[tabIndex].processingChange = true;
//
//    // --- Apply the change ---
//    // 1. Set selection to the area to be deleted
//    CHARRANGE cr;
//    cr.cpMin = (LONG)change.position;
//    cr.cpMax = (LONG)(change.position + change.deletedText.length());
//    SendMessage(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
//
//    // 2. Replace selection with the inserted text
//    SendMessage(hEdit, EM_REPLACESEL, TRUE, (LPARAM)change.insertedText.c_str()); // TRUE enables undo (but we disabled it globally)
//
//    // 3. Optionally restore cursor position (if stored in TextChange)
//    if (change.cursorPositionAfter != (size_t)-1) { // Use -1 or similar sentinel if not storing it
//        cr.cpMin = cr.cpMax = (LONG)change.cursorPositionAfter;
//        SendMessage(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
//    }
//    // --- End Apply Change ---
//
//    // Crucially, clear the processing flag *after* the changes settle
//    // Post a custom message to clear the flag slightly later, ensuring
//    // the EN_CHANGE (if any) is processed first while the flag is true.
//    // Define WM_POST_APPLY_CHANGE somewhere (e.g., resource.h or locally)
//#define WM_POST_APPLY_CHANGE (WM_USER + 100)
//    PostMessage(GetParent(hEdit), WM_POST_APPLY_CHANGE, (WPARAM)hEdit, 0);
//
//
//    // Update modification status (applying undo/redo usually reverts to a saved state)
//    // This logic needs refinement: check if the new state matches the last saved state.
//    // For now, assume undo/redo makes it potentially "modified" relative to the file.
//    // A better approach tracks the 'saved' node in the history.
//    openTabs[tabIndex].isModified = true; // Simplistic approach
//    UpdateWindowTitle(GetParent(hEdit)); // Update window title [*] indicator
//}

TextChange CalculateTextChange(const std::wstring& before, const std::wstring& after, HWND hEdit) {
    // This is a VERY basic diff implementation. For a real editor,
    // use a proper diff algorithm (e.g., Myers diff).
    // This basic version finds the first and last differing characters.

    size_t firstDiff = 0;
    while (firstDiff < before.length() && firstDiff < after.length() && before[firstDiff] == after[firstDiff]) {
        firstDiff++;
    }

    size_t lastDiffBefore = before.length();
    size_t lastDiffAfter = after.length();
    while (lastDiffBefore > firstDiff && lastDiffAfter > firstDiff && before[lastDiffBefore - 1] == after[lastDiffAfter - 1]) {
        lastDiffBefore--;
        lastDiffAfter--;
    }

    std::wstring deletedText = (lastDiffBefore > firstDiff) ? before.substr(firstDiff, lastDiffBefore - firstDiff) : L"";
    std::wstring insertedText = (lastDiffAfter > firstDiff) ? after.substr(firstDiff, lastDiffAfter - firstDiff) : L"";

    // Get cursor position AFTER the change
    CHARRANGE cr;
    SendMessage(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    size_t cursorPosAfter = cr.cpMax; // Use end of selection as cursor pos

    return TextChange(firstDiff, insertedText, deletedText, cursorPosAfter);
}

void UpdateWindowTitle(HWND hWnd) {
    std::wstring title = L"TextEditor";
    if (currentTab >= 0 && currentTab < openTabs.size()) {
        title += L" - " + openTabs[currentTab].fileName;
        if (openTabs[currentTab].isModified) {
            title += L" [*]";
        }
    }
    SetWindowTextW(hWnd, title.c_str());
}

void CreateTab(HWND hWnd, const WCHAR* title, const WCHAR* filePath ) {
    HWND hEdit = CreateRichEdit(hWnd);
    if (!hEdit) return;

    EditorTabInfo newTab;
    newTab.hEdit = hEdit;
    newTab.fileName = title;
    newTab.filePath = filePath ? filePath : L"";
    newTab.isModified = false; // Initially not modified

    // Get initial content (empty for new, load for existing)
    std::wstring initialContent = L"";
    bool loadedOk = true;
    if (!newTab.filePath.empty()) {
        loadedOk = LoadFileIntoEditor(hEdit, newTab.filePath.c_str(), initialContent); 
        if (!loadedOk) {
            // Handle error - maybe revert to empty tab?
            MessageBox(hWnd, L"Failed to load file.", L"Error", MB_OK | MB_ICONERROR);
            newTab.filePath = L""; // Clear path if load failed
            newTab.fileName = L"Untitled";
            initialContent = L"";
        }
        else {
            newTab.fileName = PathFindFileNameW(newTab.filePath.c_str()); // Update fileName from path
        }
    }
    else {
        // Ensure editor is empty for "Untitled"
        SetWindowTextW(hEdit, L"");
        initialContent = L"";
        // Reset modification and undo buffer just in case
        SendMessage(hEdit, EM_SETMODIFY, FALSE, 0);
        SendMessage(hEdit, EM_EMPTYUNDOBUFFER, 0, 0); // Still useful to clear default buffer
    }

    // --- Create and store VersionHistoryManager ---
    newTab.historyManager = std::make_unique<VersionHistoryManager>(initialContent);
    newTab.textBeforeChange = initialContent; // Store initial state for first diff
    // --- End HistoryManager creation ---

    // Add tab to UI
    TCITEM tie;
    tie.mask = TCIF_TEXT | TCIF_PARAM;
    tie.pszText = (LPWSTR)newTab.fileName.c_str(); // Use fileName from struct
    tie.lParam = (LPARAM)newTab.hEdit; // Use HWND as param for finding info later
    int index = TabCtrl_GetItemCount(hTabCtrl);
    if (TabCtrl_InsertItem(hTabCtrl, index, &tie) == -1) {
        DestroyWindow(hEdit);
        MessageBox(hWnd, L"Failed to create tab.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    openTabs.push_back(std::move(newTab)); // Add the info struct

    SwitchToTab(index);
    ResizeControls(hWnd);
    UpdateWindowTitle(hWnd); 
}

void SwitchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(openTabs.size())) return;

    // Hide the current editor
    if (currentTab >= 0 && currentTab < static_cast<int>(openTabs.size())) {
        ShowWindow(openTabs[currentTab].hEdit, SW_HIDE);
    }

    // Show the selected text editor
    ShowWindow(openTabs[index].hEdit, SW_SHOW);
    SetFocus(openTabs[index].hEdit); // Focus the editor

    TabCtrl_SetCurSel(hTabCtrl, index);
    currentTab = index;

    ResizeControls(GetParent(hTabCtrl));
    UpdateWindowTitle(GetParent(hTabCtrl)); // Update title for switched tab
}

void CloseTab(int index)
{
    if (index < 0 || index >= static_cast<int>(openTabs.size()))
        return;

    // --- Check for unsaved changes ---
    if (openTabs[index].isModified) {
        std::wstring prompt = L"Save changes to " + openTabs[index].fileName + L"?";
        int result = MessageBoxW(GetParent(hTabCtrl), prompt.c_str(), L"Unsaved Changes", MB_YESNOCANCEL | MB_ICONWARNING);
        if (result == IDCANCEL) {
            return; // User cancelled closing
        }
        else if (result == IDYES) {
            if (!SaveEditorContent(index, false)) { // Try saving
                MessageBoxW(GetParent(hTabCtrl), L"Failed to save. Close cancelled.", L"Save Error", MB_OK | MB_ICONERROR);
                return; // Save failed, cancel close
            }
            // Save successful, continue closing
        }
        // If IDNO, continue closing without saving
    }

    // HistoryManager unique_ptr cleans itself up when struct is erased
    DestroyWindow(openTabs[index].hEdit);
    openTabs.erase(openTabs.begin() + index);

    TabCtrl_DeleteItem(hTabCtrl, index);

    // Adjust currentTab
    int oldCurrentTab = currentTab;
    currentTab = -1; // Invalidate temporarily
    if (oldCurrentTab == index) {
        // If we closed the active tab
        if (openTabs.empty()) {
            // Close Text Editor if it was the last tab
            DestroyWindow(openTabs[index].hEdit);
        }
        else {
            // Select the previous or first tab
            int newIndex = std::max(0, index - 1);
            SwitchToTab(newIndex);
        }
    }
    else if (oldCurrentTab > index) {
        // If we closed a tab before the active one, adjust index
        SwitchToTab(oldCurrentTab - 1); // Reselect the same logical tab
    }
    else {
        // Closed a tab after the active one, currentTab index is still valid
        SwitchToTab(oldCurrentTab); // Just re-apply to be safe
    }

    // If SwitchToTab wasn't called above (e.g., closed non-active tab)
    if (currentTab == -1 && !openTabs.empty()) {
        SwitchToTab(0); // Default to first tab if something went wrong
    }
    else if (openTabs.empty()) {
        UpdateWindowTitle(GetParent(hTabCtrl)); // Update title if all tabs closed
    }

    ResizeControls(GetParent(hTabCtrl));
}

void ResizeControls(HWND hWnd)
{
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    RECT rcToolBar;
    int toolbarHeight = 0;
    if (GetWindowRect(hToolBar, &rcToolBar)) {
        toolbarHeight = rcToolBar.bottom - rcToolBar.top;
    }
    int tabHeight = 15; //

    HDWP hdwp = BeginDeferWindowPos(2 + openTabs.size()); // Toolbar + TabControl + Editors

    if (hdwp) hdwp = DeferWindowPos(hdwp, hToolBar, NULL, 0, 0, rcClient.right, toolbarHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hdwp) hdwp = DeferWindowPos(hdwp, hTabCtrl, NULL, 0, toolbarHeight, rcClient.right, tabHeight, SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rcEditor;
    rcEditor.left = 0;
    rcEditor.top = toolbarHeight + tabHeight;
    rcEditor.right = rcClient.right;
    rcEditor.bottom = rcClient.bottom;

    if (currentTab >= 0 && currentTab < static_cast<int>(openTabs.size())) {
        if (hdwp) hdwp = DeferWindowPos(hdwp, openTabs[currentTab].hEdit, NULL,
            rcEditor.left, rcEditor.top,
            rcEditor.right - rcEditor.left,
            rcEditor.bottom - rcEditor.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (hdwp) EndDeferWindowPos(hdwp);
}


void ShowCommandPalette(HWND hWnd)
{
    DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDPALETTE), hWnd, CommandPaletteProc);
}

//this function isn't really needed as we already had created Menu in .rc file, but I have added it for future use
void ShowPopupMenu(HWND hWnd, int buttonId, int x, int y)
{
    //HMENU hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDC_TEXTEDITOR)); // Load base menu resource (if defined)
    HMENU hMenu = CreateMenu(); // Simpler to always create dynamically
    HMENU hSubMenu = NULL;

    switch (buttonId) {
    case ID_FILE_NEW: // Assuming ID_FILE_NEW is the command ID of the "File" toolbar button
        hSubMenu = CreatePopupMenu();
        AppendMenu(hSubMenu, MF_STRING, ID_FILE_NEW, L"&New\tCtrl+T");
        AppendMenu(hSubMenu, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
        AppendMenu(hSubMenu, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
        AppendMenu(hSubMenu, MF_STRING, ID_FILE_SAVEAS, L"Save &As...");
        AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hSubMenu, MF_STRING, ID_CLOSE_TAB, L"&Close Tab\tCtrl+W");
        AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hSubMenu, MF_STRING, IDM_EXIT, L"E&xit");
        break;

    case IDM_THEME_MENU: // Command ID for the "Theme" button
        hSubMenu = CreatePopupMenu();
        // TODO: Add check marks based on current theme
        AppendMenu(hSubMenu, MF_STRING, IDM_THEME_LIGHT, L"&Light");
        AppendMenu(hSubMenu, MF_STRING, IDM_THEME_DARK, L"&Dark");
        AppendMenu(hSubMenu, MF_STRING, IDM_THEME_SYSTEM, L"&System Default");
        break;

    case IDM_ABOUT: // Command ID for the "Help" button
        hSubMenu = CreatePopupMenu();
        AppendMenu(hSubMenu, MF_STRING, IDM_ABOUT, L"&About TextEditor...");
        // Add other help items if needed
        break;

    case ID_EDIT_UNDO: // "Edit" button ID
        hSubMenu = CreatePopupMenu();
        // Check if undo/redo is possible for the current tab
        bool canUndo = false;
        bool canRedo = false;
        if (currentTab >= 0 && currentTab < openTabs.size() && openTabs[currentTab].historyManager) {
            canUndo = openTabs[currentTab].historyManager->canUndo();
            canRedo = openTabs[currentTab].historyManager->canRedo();
        }
        AppendMenuW(hSubMenu, MF_STRING | (canUndo ? MF_ENABLED : MF_GRAYED), ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
        AppendMenuW(hSubMenu, MF_STRING | (canRedo ? MF_ENABLED : MF_GRAYED), ID_EDIT_REDO, L"&Redo\tCtrl+Y");
        AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hSubMenu, MF_STRING, ID_VIEW_HISTORY, L"View &History...\tF5"); // Add History View option
        // Add Cut, Copy, Paste later if needed (using EM_CUT, EM_COPY, EM_PASTE)
        break;
    }

    if (hSubMenu) {
        // Ensure menu appears correctly relative to the button
        TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, hWnd, NULL);
        DestroyMenu(hSubMenu); // Destroy the dynamically created popup menu
    }

    if (hMenu) DestroyMenu(hMenu); // Destroy the base menu if loaded/created
}

// Helper function to update tab title based on file name
void UpdateTabTitle(int index) {
    if (index < 0 || index >= static_cast<int>(openTabs.size())) return;

    std::wstring title = openTabs[index].fileName;
    if (openTabs[index].isModified) {
        title += L" [*]";
    }

    TCITEM tie;
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)title.c_str(); // Use buffer for safety if title is long
    TabCtrl_SetItem(hTabCtrl, index, &tie);
}


// Helper function to load file content into the editor
bool LoadFileIntoEditor(HWND hEdit, const WCHAR* filePath, std::wstring& outContent) {
    if (!hEdit || !filePath) return false;

    std::wifstream inFile(filePath);
    if (!inFile) return false;

    std::wstringstream buffer;
    buffer << inFile.rdbuf();
    outContent = buffer.str();
    inFile.close();

    // Set text in Rich Edit control
    // Set flag to ignore EN_CHANGE during programmatic text setting
    int tabIndex = -1;
    for (int i = 0; i < openTabs.size(); ++i) {
        if (openTabs[i].hEdit == hEdit) {
            tabIndex = i;
            break;
        }
    }
    if (tabIndex != -1) openTabs[tabIndex].processingChange = true;

    SetWindowTextW(hEdit, outContent.c_str());

    // Reset undo buffer (ours and potentially the default one) and modification state
    SendMessage(hEdit, EM_EMPTYUNDOBUFFER, 0, 0); // Clear RichEdit internal undo
    SendMessage(hEdit, EM_SETMODIFY, FALSE, 0);    // Mark as unmodified

    // Clear the processing flag via message after setting text
    if (tabIndex != -1) PostMessage(GetParent(hEdit), WM_POST_APPLY_CHANGE, (WPARAM)hEdit, 0);

    return true;
}

// Helper function to save editor content to a file
bool SaveEditorContent(int tabIndex, bool saveAs) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(openTabs.size())) return false;

    std::wstring currentFilePath = openTabs[tabIndex].filePath;
    WCHAR szFile[MAX_PATH] = { 0 };
    bool pathChanged = false;

    if (saveAs || currentFilePath.empty()) {
        OPENFILENAME ofn; // ... (setup OPENFILENAME as before) ...
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetParent(hTabCtrl);
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        if (!openTabs[tabIndex].fileName.empty() && openTabs[tabIndex].fileName != L"Untitled") {
            wcscpy_s(szFile, MAX_PATH, openTabs[tabIndex].fileName.c_str());
        }
        ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

        if (!GetSaveFileName(&ofn)) {
            return false; // User cancelled
        }
        // Check if the path actually changed compared to the stored one
        if (currentFilePath != ofn.lpstrFile) {
            pathChanged = true;
            currentFilePath = ofn.lpstrFile; // Update path with the chosen one
        }
    }

    // Get text from editor
    std::wstring contentToSave = GetRichEditText(openTabs[tabIndex].hEdit);

    // Save to file
    std::wofstream outFile(currentFilePath);
    if (outFile) {
        outFile << contentToSave;
        outFile.close();

        // --- Update tab info ---
        openTabs[tabIndex].filePath = currentFilePath;
        // Update file name from path (use PathFindFileName for robustness)
        openTabs[tabIndex].fileName = PathFindFileNameW(currentFilePath.c_str());
        openTabs[tabIndex].isModified = false; // Mark as unmodified *after* successful save
        // TODO: Store the current history node as the "saved" state marker
        UpdateTabTitle(tabIndex);
        UpdateWindowTitle(GetParent(hTabCtrl)); // Update main window title
        // --- End update tab info ---


        // Mark editor control as unmodified (might be redundant but safe)
        SendMessage(openTabs[tabIndex].hEdit, EM_SETMODIFY, FALSE, 0);
        return true;
    }
    else {
        MessageBox(GetParent(hTabCtrl), L"Failed to save the file.", L"Save Error", MB_OK | MB_ICONERROR);
        return false;
    }
}


void ShowHistoryTree(HWND hWnd) {
    if (currentTab < 0 || currentTab >= openTabs.size() || !openTabs[currentTab].historyManager) return;

    // TODO: Implement History Tree Dialog/Panel
    // 1. Create a new dialog resource (e.g., IDD_HISTORY_TREE) with a TreeView control (WC_TREEVIEW).
    // 2. Create a DialogProc for this dialog.
    // 3. In WM_INITDIALOG:
    //    - Get the VersionHistoryManager for the current tab.
    //    - Get the root node: std::shared_ptr<const HistoryNode> root = historyManager->getHistoryTreeRoot();
    //    - Recursively populate the TreeView control using TreeView_InsertItem macro.
    //      - Store the std::shared_ptr<HistoryNode> (or a unique ID) in the lParam of each TreeView item (TVITEM).
    //      - Highlight the item corresponding to historyManager->getCurrentNode().
    // 4. Handle TVN_SELCHANGED notification in the DialogProc:
    //    - When the user selects a node in the TreeView.
    // 5. Handle Button Clicks (e.g., "Switch to this version"):
    //    - Get the selected TreeView item.
    //    - Retrieve the HistoryNode pointer/ID from its lParam.
    //    - Find the corresponding std::shared_ptr<HistoryNode> (might need a map if using IDs).
    //    - Call: std::wstring newState = openTabs[currentTab].historyManager->switchToNode(targetNodePtr);
    //    - Update the main Rich Edit control: SetWindowTextW(openTabs[currentTab].hEdit, newState.c_str());
    //    - Close the history dialog.
    // 6. Show the dialog using DialogBoxParam (passing necessary info like the manager pointer).

    MessageBoxW(hWnd, L"History Tree UI not implemented yet.", L"Info", MB_OK | MB_ICONINFORMATION);
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        CreateToolbar(hWnd);
        CreateTabControl(hWnd);
        CreateTab(hWnd, L"Untitled", nullptr); // Create initial tab
        ResizeControls(hWnd); // Initial sizing
        UpdateWindowTitle(hWnd); // Set initial title
    }
        break;
    case WM_POST_APPLY_CHANGE:
        {
            HWND hEdit = (HWND)wParam;
            for (auto& tab : openTabs) {
                if (tab.hEdit == hEdit) {
                    tab.processingChange = false;
                    break;
                }
            }
        }
        break;

    case WM_SIZE:
        ResizeControls(hWnd);
        break;

    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->hwndFrom == hToolBar && pnmh->code == TBN_DROPDOWN) {
                // Handle toolbar button dropdown clicks
                LPNMTOOLBAR lpnmTB = (LPNMTOOLBAR)lParam;

                // Get button rect
                RECT rc;
                SendMessage(hToolBar, TB_GETRECT, (WPARAM)lpnmTB->iItem, (LPARAM)&rc);

                // Convert to screen coordinates
                POINT pt = { rc.left, rc.bottom };
                ClientToScreen(hToolBar, &pt);

                // Show the appropriate popup menu
                ShowPopupMenu(hWnd, lpnmTB->iItem, pt.x, pt.y);

                return TBDDRET_DEFAULT; // Prevent default handling if needed
            }
            // --- Tab Control Selection Change ---
            else if (pnmh->idFrom == IDC_TABCTRL && pnmh->code == TCN_SELCHANGE) {
                int index = TabCtrl_GetCurSel(hTabCtrl);
                SwitchToTab(index); // Handles showing/hiding, focus, title update
            }
            // --- Tab Control Right Click ---
            else if (pnmh->idFrom == IDC_TABCTRL && pnmh->code == NM_RCLICK) {
                // ... (existing tab right-click handling - show close menu) ...
                // Determine tab index clicked
                POINT pt; GetCursorPos(&pt); ScreenToClient(hTabCtrl, &pt);
                TCHITTESTINFO hti; hti.pt = pt;
                int tabIndex = TabCtrl_HitTest(hTabCtrl, &hti);
                if (tabIndex != -1) {
                    POINT screenPt; GetCursorPos(&screenPt);
                    HMENU hPopup = CreatePopupMenu();
                    AppendMenuW(hPopup, MF_STRING, ID_CLOSE_TAB, L"&Close Tab\tCtrl+W");
                    // Set currentTab *temporarily* for the context menu action handler
                    int previouslyActiveTab = currentTab; // Store the actually active tab
                    currentTab = tabIndex; // Set context for the command
                    BOOL result = TrackPopupMenu(hPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, 0, hWnd, NULL);
                    currentTab = previouslyActiveTab; // Restore the actually active tab
                    DestroyMenu(hPopup);

                    // If a command was selected, post it
                    if (result != 0) {
                        PostMessage(hWnd, WM_COMMAND, result, 0);
                    }
                }
            }
            // --- RICH EDIT NOTIFICATIONS ---
            else if (currentTab >= 0 && currentTab < openTabs.size() && pnmh->hwndFrom == openTabs[currentTab].hEdit) {
                // Check it's the active editor generating the notification
                if (pnmh->code == EN_CHANGE) {
                    // Check processing flag to prevent recursion from undo/redo application
                    if (!openTabs[currentTab].processingChange) {
                        // --- Record Change ---
                        std::wstring textAfter = GetRichEditText(openTabs[currentTab].hEdit);
                        // Retrieve text *before* the change (stored in the tab info)
                        std::wstring textBefore = openTabs[currentTab].textBeforeChange;

                        // Calculate the diff
                        TextChange change = CalculateTextChange(textBefore, textAfter, openTabs[currentTab].hEdit);

                        // Record in history manager
                        if (openTabs[currentTab].historyManager) {
                            openTabs[currentTab].historyManager->recordChange(change);
                        }

                        // Update textBeforeChange for the *next* change event
                        openTabs[currentTab].textBeforeChange = textAfter;

                        // Mark tab as modified
                        openTabs[currentTab].isModified = true; // A change occurred
                        UpdateTabTitle(currentTab);
                        UpdateWindowTitle(hWnd);
                        // --- End Record Change ---
                    }
                }
                else if (pnmh->code == EN_SELCHANGE) {
                    // Handle selection change if needed (e.g., update status bar)
                    // LPNMSELCHANGE lpnmsc = (LPNMSELCHANGE)lParam;
                    // Can get lpnmsc->chrg.cpMin, lpnmsc->chrg.cpMax
                }
            }
        }
        break; // End WM_NOTIFY

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam); // Needed for accelerator check

        // Handle commands only if they are not from accelerators already processed
        // (wmEvent == 0 for menu clicks, wmEvent == 1 for accelerator keys)
        // Or handle all if TranslateAccelerator isn't fully catching everything

        switch (wmId)
        {
            // --- FILE ---
        case ID_FILE_NEW: CreateTab(hWnd, L"Untitled", nullptr); break;
        case ID_FILE_OPEN: 
        {
            OPENFILENAME ofn; // ... setup ofn ...
            WCHAR szFile[MAX_PATH] = { 0 };
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR); // Correct size
            ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

            if (GetOpenFileName(&ofn)) {
                CreateTab(hWnd, PathFindFileNameW(ofn.lpstrFile), ofn.lpstrFile);
            }
        }
        break;
        case ID_FILE_SAVE:   if (currentTab >= 0) SaveEditorContent(currentTab, false); break;
        case ID_FILE_SAVEAS: if (currentTab >= 0) SaveEditorContent(currentTab, true); break;
        case ID_CLOSE_TAB:   if (currentTab >= 0) CloseTab(currentTab); break;

            // --- EDIT ---
        //case ID_EDIT_UNDO:
        //    if (currentTab >= 0 && currentTab < openTabs.size() && openTabs[currentTab].historyManager) {
        //        auto reverseChangeOpt = openTabs[currentTab].historyManager->undo();
        //        if (reverseChangeOpt) {
        //            ApplyChangeToRichEdit(openTabs[currentTab].hEdit, *reverseChangeOpt);
        //            // Update textBeforeChange after applying undo/redo
        //            openTabs[currentTab].textBeforeChange = GetRichEditText(openTabs[currentTab].hEdit);
        //        }
        //    }
        //    break;
        //case ID_EDIT_REDO:
        //    if (currentTab >= 0 && currentTab < openTabs.size() && openTabs[currentTab].historyManager) {
        //        // TODO: If branching, potentially ask user which branch via getRedoBranchDescriptions()
        //        // For now, redo the default (last) branch
        //        auto forwardChangeOpt = openTabs[currentTab].historyManager->redo(); // Default index
        //        if (forwardChangeOpt) {
        //            ApplyChangeToRichEdit(openTabs[currentTab].hEdit, *forwardChangeOpt);
        //            // Update textBeforeChange after applying undo/redo
        //            openTabs[currentTab].textBeforeChange = GetRichEditText(openTabs[currentTab].hEdit);
        //        }
        //    }
        //    break;
        case ID_VIEW_HISTORY:
            ShowHistoryTree(hWnd);
            break;

            // --- THEME ---
        case IDM_THEME_LIGHT: /* ... */ break;
        case IDM_THEME_DARK:  /* ... */ break;
        case IDM_THEME_SYSTEM:/* ... */ break;

            // --- HELP & EXIT ---
        case IDM_ABOUT: DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About); break;
        case IDM_EXIT: DestroyWindow(hWnd); break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break; // End WM_COMMAND

	//TODO: this keyboard shortcuts handling is not working right now, need changes
    case WM_KEYDOWN:
        // Handle accelerators here if not using an accelerator table or for specific needs
        if (GetKeyState(VK_CONTROL) & 0x8000) { // Check if Control key is pressed
            switch (wParam) {
            case 'P': // Ctrl+P
                ShowCommandPalette(hWnd);
                return 0; 
            case 'T': // Ctrl+T
                PostMessage(hWnd, WM_COMMAND, ID_FILE_NEW, 0); // Use PostMessage to avoid re-entrancy issues
                return 0;
            case 'W': // Ctrl+W
                if (currentTab >= 0) {
                    PostMessage(hWnd, WM_COMMAND, ID_CLOSE_TAB, 0);
                }
                return 0; 
            case 'O': // Ctrl+O
                PostMessage(hWnd, WM_COMMAND, ID_FILE_OPEN, 0);
                return 0; 
            case 'S': // Ctrl+S
                if (currentTab >= 0) {
                    // Check Shift key for Save As (Ctrl+Shift+S)
                    if (GetKeyState(VK_SHIFT) & 0x8000) {
                        PostMessage(hWnd, WM_COMMAND, ID_FILE_SAVEAS, 0);
                    }
                    else {
                        PostMessage(hWnd, WM_COMMAND, ID_FILE_SAVE, 0);
                    }
                }
                return 0; 
            }
        }
        return DefWindowProc(hWnd, message, wParam, lParam); // Default handling for other keys


    case WM_CLOSE:
        // Iterate through tabs and check for unsaved changes *before* destroying
        while (!openTabs.empty()) {
            // Try closing the *first* tab repeatedly. CloseTab handles selection logic.
            // If CloseTab is cancelled (e.g., user cancels save), it will return without erasing.
            size_t initialSize = openTabs.size();
            CloseTab(0); // Attempt to close the first tab
            if (openTabs.size() == initialSize) {
                // Close was cancelled, stop trying to close window
                return 0; // Prevent default WM_CLOSE handling (DestroyWindow)
            }
            // If close succeeded, the loop continues with the next tab at index 0
        }
        // If loop finishes, all tabs were closed successfully (or saved/discarded)
        DestroyWindow(hWnd); // Now safe to destroy the main window
        break;

    case WM_DESTROY:
        // Clean up resources like GDI objects (fonts) if necessary
        // ...
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// About dialog procedure 
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// Command Palette dialog procedure 
INT_PTR CALLBACK CommandPaletteProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hListBox;

    switch (message)
    {
    case WM_INITDIALOG:
    { // Scope for variable declaration
        RECT rcDlg; GetClientRect(hDlg, &rcDlg);
        hListBox = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 
            10, 10, rcDlg.right - 20, rcDlg.bottom - 50, // Adjust size dynamically
            hDlg, (HMENU)IDC_COMMANDPALETTE, hInst, NULL);

        if (!hListBox) { EndDialog(hDlg, -1); return (INT_PTR)FALSE; } // Check creation

        for (int i = 0; i < _countof(commands); ++i) {
            SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)commands[i]);
        }
        SetFocus(hListBox); // Set focus to list box
    }
    return (INT_PTR)FALSE; // Return FALSE because we set the focus

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == IDC_COMMANDPALETTE && wmEvent == LBN_DBLCLK) {
            int sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                // Execute command based on selection
                HWND hParent = GetParent(hDlg); // Get main window handle
                EndDialog(hDlg, sel); // Close the dialog first

                switch (sel) {
                case 0: SendMessage(hParent, WM_COMMAND, ID_FILE_NEW, 0); break; // New File
                case 1: SendMessage(hParent, WM_COMMAND, ID_FILE_OPEN, 0); break; // Open File
                case 2: SendMessage(hParent, WM_COMMAND, ID_FILE_SAVE, 0); break; // Save File
                case 3: SendMessage(hParent, WM_COMMAND, IDM_EXIT, 0); break; // Exit
                default: /* Handle other commands if added */ break;
                }
            }
            return (INT_PTR)TRUE;
        }
        else if (wmId == IDOK) { // Handle Enter key press on selection
            int sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                // Post the command execution similar to double-click
                PostMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_COMMANDPALETTE, LBN_DBLCLK), (LPARAM)hListBox);
            }
            return (INT_PTR)TRUE;
        }
        else if (wmId == IDCANCEL) { // Handle Esc key or Close button
            EndDialog(hDlg, -1); // Indicate cancellation
            return (INT_PTR)TRUE;
        }
    }
    break;

    case WM_CLOSE:
        EndDialog(hDlg, -1); // Indicate cancellation
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}