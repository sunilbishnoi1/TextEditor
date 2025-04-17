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
#define IDLE_HISTORY_TIMEOUT_MS 4000
#define SIGNIFICANT_CHANGE_THRESHOLD 101

// --- Theme Support -------------------------------------------------------------------------------

enum class Theme {
    Light,
    Dark,
    System
};
static Theme g_currentTheme = Theme::System;
static COLORREF g_lightBg   = RGB(255,255,255);
static COLORREF g_lightText = RGB(  0,  0,  0);
static COLORREF g_darkBg    = RGB( 32, 32, 32);
static COLORREF g_darkText  = RGB(224,224,224);

// Forward declarations
void ApplyThemeToRichEdit(HWND hEdit);
void ApplyTheme(HWND hWnd);

 //-----------------------------------------------------------------------------=--------------------------------

// Global Variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hTabCtrl;
HWND hToolBar;
HWND g_hWnd = NULL;

struct EditorTabInfo {
    HWND hEdit = NULL;
    std::wstring filePath = L"";
    std::wstring fileName = L"Untitled";
    std::unique_ptr<VersionHistoryManager> historyManager = nullptr;
    bool isModified = false; // Track modification status
    // Store the text state *before* the current change for diffing
    std::wstring textBeforeChange = L"";
    //bool processingChange = false; // Flag to prevent re-entrancy during change handling

	UINT_PTR idleTimerId = 0; // Timer ID for idle state
    bool changesSinceLastHistoryPoint = false; //Tracks if modification occurred
    //Optimization: Store the text state of the *last recorded history point*
    //This avoids reconstructing from root to calculate the next diff.
	std::wstring textAtLastHistoryPoint = L"";
     bool processingHistoryAction = false; // 
	 size_t totalChangeSize = 0; // total size of changes since last history point
};
std::vector<EditorTabInfo> openTabs;
int currentTab = -1;

// Structure to pass data to the History Dialog Procedure
struct HistoryDialogParams {
    VersionHistoryManager* historyManager = nullptr;
    int tabIndex = -1; // To access openTabs[tabIndex] if needed
    HWND hParent = NULL; // Main window handle
    // Map to associate TreeView items with their corresponding HistoryNode shared_ptrs
    // This is safer than storing pointers directly in lParam.
    std::map<HTREEITEM, std::shared_ptr<const HistoryNode>> treeItemNodeMap;
    // Store the node that corresponds to the state currently shown in the editor when the dialog opened
    std::shared_ptr<const HistoryNode> nodeAtEditorState = nullptr;
};

struct ChooseChildDialogParams {
    VersionHistoryManager* historyManager = nullptr;
    std::vector<std::wstring> descriptions;
    int selectedIndex = -1; // To store the result
};


// Structure to pass data to/from Commit Message Dialog
struct CommitMessageParams {
    std::wstring* pCommitMessage = nullptr; // Pointer to store the result
};

// Forward declarations
ATOM                MyRegisterClass(HINSTANCE hInstance);
bool                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    CommandPaletteProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    CommitMessageDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    HistoryDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    ChooseChildCommitDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
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

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TEXTEDITOR));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(g_hWnd, hAccelTable, &msg)) {
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
	wcex.lpszMenuName = MAKEINTRESOURCE(IDC_TEXTEDITOR); //load menu from resource
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

bool InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    g_hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, hInstance, nullptr); // Use CW_USEDEFAULT for size

    if (!g_hWnd)
        return FALSE;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    return TRUE;
}

void CreateToolbar(HWND hWnd)
{
    // Create toolbar
    hToolBar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP, 
        0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR, hInst, NULL);

    //// Add buttons - Using TBSTYLE_DROPDOWN for menus
    //TBBUTTON tbb[4];
    //ZeroMemory(tbb, sizeof(tbb));

    //// File dropdown
    //tbb[0].idCommand = ID_FILE_NEW; // Use ID_FILE_NEW as placeholder ID for the button itself
    //tbb[0].fsState = TBSTATE_ENABLED;
    //tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN; 
    //tbb[0].iString = (INT_PTR)L"File";

    //// Theme dropdown
    //tbb[1].idCommand = IDM_THEME_MENU;
    //tbb[1].fsState = TBSTATE_ENABLED;
    //tbb[1].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN; 
    //tbb[1].iString = (INT_PTR)L"Theme";

    //// Help dropdown
    //tbb[2].idCommand = IDM_ABOUT; // Placeholder ID
    //tbb[2].fsState = TBSTATE_ENABLED;
    //tbb[2].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN; 
    //tbb[2].iString = (INT_PTR)L"Help";

    ////EDIT dropdown
    //tbb[3].idCommand = ID_EDIT_UNDO; // Placeholder ID for the button itself
    //tbb[3].fsState = TBSTATE_ENABLED;
    //tbb[3].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_DROPDOWN;
    //tbb[3].iString = (INT_PTR)L"Edit";

    //SendMessage(hToolBar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    //SendMessage(hToolBar, TB_ADDBUTTONS, (WPARAM)4, (LPARAM)&tbb);
    /*SendMessage(hToolBar, TB_AUTOSIZE, 0, 0);*/  //this might need later
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

// ---------------------------------------------------------------------------------------------------------------------------------
	
// Apply theme to a single RichEdit control
void ApplyThemeToRichEdit(HWND hEdit) {
    if (!hEdit) return;
    COLORREF bg = (g_currentTheme == Theme::Dark ? g_darkBg : g_lightBg);
    SendMessage(hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)bg);
    CHARFORMAT2 cf = { sizeof(cf) };
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = (g_currentTheme == Theme::Dark ? g_darkText : g_lightText);
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

	// Apply theme across the entire UI
	
void ApplyTheme(HWND hWnd) {
    // Main window background
    static HBRUSH hbrOld = NULL;
    HBRUSH hbr = CreateSolidBrush(
        g_currentTheme == Theme::Dark ? g_darkBg : g_lightBg
    );
    if (hbrOld) DeleteObject(hbrOld);
    hbrOld = hbr;
    SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)hbr);

    // Tab control redraw
    InvalidateRect(hTabCtrl, NULL, TRUE);

    // All open RichEdits
    for (auto& tab : openTabs) {
        ApplyThemeToRichEdit(tab.hEdit);
    }

    InvalidateRect(hWnd, NULL, TRUE);
}


// ------------------------------------------------------------------------------------

// In WndProc: handle WM_CTLCOLOR* messages
// inside WndProc switch(message):
// case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
//      HDC hdc = (HDC)wParam;
//      HWND ctl = (HWND)lParam;
//      COLORREF bg = (g_currentTheme==Theme::Dark?g_darkBg:g_lightBg);
//      COLORREF fg = (g_currentTheme==Theme::Dark?g_darkText:g_lightText);
//      SetBkColor(hdc,bg);
//      SetTextColor(hdc,fg);
//      static HBRUSH hbrCtl = NULL;
//      if(hbrCtl) DeleteObject(hbrCtl);
//      hbrCtl = CreateSolidBrush(bg);
//      return (LRESULT)hbrCtl;

// Then in WM_CREATE of WndProc, after creating initial tab:
// ApplyTheme(hWnd);

// And in WM_COMMAND install:
// case IDM_THEME_LIGHT: g_currentTheme=Theme::Light; ApplyTheme(hWnd); break;
// case IDM_THEME_DARK:  g_currentTheme=Theme::Dark;  ApplyTheme(hWnd); break;
// case IDM_THEME_SYSTEM:g_currentTheme=Theme::System;ApplyTheme(hWnd); break;

// The rest of your existing code continues unchanged, integrating the above helpers where indicated...

// [Insert the full rest of your code here, including WndProc, dialog procs, etc.,
// with the snippets above merged into the proper places.]

// ---------------------------------------------------------------------------------------------------------------------------------------

    // Set custom font (e.g., Consolas 10pt)
    LOGFONT lf = { 0 };
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Consolas");
    lf.lfHeight = -MulDiv(10, GetDeviceCaps(GetDC(hEdit), LOGPIXELSY), 50); //font size
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
    OutputDebugStringW((L"CalculateTextChange:\n  Before: [" + before + L"]\n  After:  [" + after + L"]\n").c_str());

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

    // Log calculated results
    OutputDebugStringW((L"  Result:\n    Pos: " + std::to_wstring(firstDiff)
        + L"\n    Inserted: [" + insertedText + L"] (" + std::to_wstring(insertedText.length()) + L" chars)"
        + L"\n    Deleted:  [" + deletedText + L"] (" + std::to_wstring(deletedText.length()) + L" chars)\n").c_str());

    return TextChange(firstDiff, insertedText, deletedText, cursorPosAfter);
}

// Finds the corresponding tab and updates its state after text is set programmatically
void UpdateTabStateAfterHistoryAction(HWND hEdit, const std::wstring& newText, std::shared_ptr<const HistoryNode> targetNode) {
    int tabIndex = -1;
    for (int i = 0; i < openTabs.size(); ++i) {
        if (openTabs[i].hEdit == hEdit) {
            tabIndex = i;
            break;
        }
    }
    if (tabIndex == -1) return;

    auto& tab = openTabs[tabIndex];

    // Update baseline text *critical*
    tab.textAtLastHistoryPoint = newText;
    tab.textBeforeChange = newText; // Keep this in sync too
    tab.changesSinceLastHistoryPoint = false; // State now matches a specific history point
    tab.totalChangeSize = 0; // Reset accumulated size

    // TODO: Update modification status - compare newText to saved state if tracked,
    // otherwise, assume jumping in history makes it potentially "unsaved"
    // For simplicity now, let's mark it unmodified relative to the history point,
    // but potentially modified relative to the file on disk (needs better tracking).
    SendMessage(hEdit, EM_SETMODIFY, FALSE, 0); // Mark control as unmodified
    tab.isModified = true; // Mark tab as potentially modified relative to file (needs refinement)
    UpdateTabTitle(tabIndex);
    UpdateWindowTitle(GetParent(hEdit));

    // Update the history manager's internal pointer *if* a targetNode was provided
    // Do this AFTER setting text and updating our baselines.
    if (targetNode && tab.historyManager) {
        // Need const_cast if setCurrentNode requires non-const shared_ptr
        tab.historyManager->setCurrentNode(std::const_pointer_cast<HistoryNode>(targetNode));
    }

    // Ensure the flag is cleared *after* potential EN_CHANGE is processed
    PostMessage(GetParent(hEdit), WM_POST_APPLY_CHANGE, (WPARAM)hEdit, 0);
}



//Helper to set text, clear RichEdit undo, and manage flags Pass the targetNode to restore cursor position accurately.
void SetRichEditText(HWND hEdit, const std::wstring& text, std::shared_ptr<const HistoryNode> targetNode = nullptr) {
    if (!hEdit) return;

    // Find the tab associated with this hEdit
    int tabIndex = -1;
    for (int i = 0; i < openTabs.size(); ++i) {
        if (openTabs[i].hEdit == hEdit) {
            tabIndex = i;
            break;
        }
    }
    if (tabIndex == -1) return; // Should not happen

    auto& tab = openTabs[tabIndex];
    tab.processingHistoryAction = true; // Set flag BEFORE changing text

    // Store old selection *if needed* (usually not when jumping history)
    // CHARRANGE oldSel; SendMessage(hEdit, EM_EXGETSEL, 0, (LPARAM)&oldSel);

    SetWindowTextW(hEdit, text.c_str());

    // --- CRUCIAL: Clear Rich Edit's internal undo buffer ---
    // This prevents conflicts when jumping to an arbitrary history state.
    SendMessage(hEdit, EM_EMPTYUNDOBUFFER, 0, 0);

    // Restore cursor position based on the target node's info
    CHARRANGE newSel = { 0, 0 }; // Default to start
    if (targetNode && !targetNode->isRoot() && targetNode->changeFromParent.cursorPositionAfter != (size_t)-1) {
        // Use the position stored *after* the change that LED to this node was applied
        newSel.cpMin = newSel.cpMax = (LONG)targetNode->changeFromParent.cursorPositionAfter;

        // Boundary check: ensure cursor position is within the new text length
        GETTEXTLENGTHEX gtl = { GTL_DEFAULT, CP_ACP }; // Use default code page
        LRESULT textLen = SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
        if (textLen >= 0 && newSel.cpMin > textLen) {
            newSel.cpMin = newSel.cpMax = (LONG)textLen; // Move to end if out of bounds
        }
        else if (textLen < 0) {
            // Error getting length, default to 0,0
            newSel.cpMin = newSel.cpMax = 0;
        }

    }
    else {
        // Root node or no cursor info, default to start of document
        newSel.cpMin = newSel.cpMax = 0;
    }
    SendMessage(hEdit, EM_EXSETSEL, 0, (LPARAM)&newSel);


    UpdateTabStateAfterHistoryAction(hEdit, text, targetNode);
    //// Mark control as unmodified (since it now matches a specific history state)
    //// Note: tab.isModified should be updated based on whether this state matches the saved state on disk.
    //SendMessage(hEdit, EM_SETMODIFY, FALSE, 0);

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
        OutputDebugStringW((L"CreateTab: Attempting to load file: " + newTab.filePath + L"\n").c_str());
        loadedOk = LoadFileIntoEditor(hEdit, newTab.filePath.c_str(), initialContent); 
        if (!loadedOk) {
            // Handle error - maybe revert to empty tab?
            OutputDebugStringW(L"CreateTab: Failed to load file.\n");

            MessageBox(hWnd, L"Failed to load file.", L"Error", MB_OK | MB_ICONERROR);
            newTab.filePath = L""; // Clear path if load failed
            newTab.fileName = L"Untitled";
            initialContent = L"";
        }
        else {
            OutputDebugStringW((L"CreateTab: File loaded successfully. InitialContent Length: " + std::to_wstring(initialContent.length()) + L"\n").c_str());
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

    OutputDebugStringW((L"CreateTab: BEFORE creating HistoryManager. InitialContent Length: " + std::to_wstring(initialContent.length()) + L"\n").c_str());
    OutputDebugStringW((L"CreateTab: BEFORE creating HistoryManager. InitialContent Start: [" + initialContent.substr(0, 100) + L"]\n").c_str()); // Log first 100 chars

    // --- Create and store VersionHistoryManager ---
    newTab.historyManager = std::make_unique<VersionHistoryManager>(initialContent);
    // Initialize the baseline text for the *first* diff calculation
    newTab.textAtLastHistoryPoint = initialContent;
    newTab.textBeforeChange = initialContent; // Also init this
    newTab.changesSinceLastHistoryPoint = false; // No changes initially
    newTab.processingHistoryAction = false; // Not processing initially
    // --- End HistoryManager creation ---

    newTab.historyManager = std::make_unique<VersionHistoryManager>(initialContent);
    newTab.textAtLastHistoryPoint = initialContent;
    newTab.textBeforeChange = initialContent; // Initialize for first EN_CHANGE
    newTab.totalChangeSize = 0; // Initialize cumulative size
    newTab.changesSinceLastHistoryPoint = false;
    newTab.processingHistoryAction = false;
    newTab.idleTimerId = 0;



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

    // Ensure editor is set AFTER history manager is initialized
    SetWindowTextW(hEdit, initialContent.c_str()); // Set initial text
    SendMessage(hEdit, EM_SETMODIFY, FALSE, 0); // Mark as unmodified
    SendMessage(hEdit, EM_EMPTYUNDOBUFFER, 0, 0); // Clear default buffer after setting tex

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
    // --- Add Timer Cleanup ---
    if (openTabs[index].idleTimerId != 0) {
        KillTimer(GetParent(hTabCtrl), reinterpret_cast<UINT_PTR>(openTabs[index].hEdit));
        openTabs[index].idleTimerId = 0;
    }
    // --- End Timer Cleanup ---

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
            /*DestroyWindow(openTabs[index].hEdit);*/
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


 //   /*RECT rcToolBar;
 //   int toolbarHeight = 0;
 //   if (GetWindowRect(hToolBar, &rcToolBar)) {
 //       toolbarHeight = rcToolBar.bottom - rcToolBar.top;
 //   }*/
	//int toolbarHeight = 40; // adjust as needed, 28 pixels


	int tabHeight = 22; // adjust as needed

    HDWP hdwp = BeginDeferWindowPos(2); // Toolbar + TabControl + Editors

    /*if (hdwp) hdwp = DeferWindowPos(hdwp, hToolBar, NULL, 0, 0, rcClient.right, toolbarHeight, SWP_NOZORDER | SWP_NOACTIVATE);*/
    if (hdwp) {
        hdwp = DeferWindowPos(hdwp, hTabCtrl, NULL, 0, 0, rcClient.right, tabHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    else {
        SetWindowPos(hTabCtrl, NULL, 0, 0, rcClient.right, tabHeight,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    RECT rcEditor;
    rcEditor.left = 0;
    rcEditor.top = tabHeight;
    rcEditor.right = rcClient.right;
    rcEditor.bottom = rcClient.bottom;

    HWND hActiveEdit = NULL; // Keep track of the active editor handle
    if (currentTab >= 0 && currentTab < static_cast<int>(openTabs.size())) {
        hActiveEdit = openTabs[currentTab].hEdit;
        if (hActiveEdit) {
            if (hdwp) {
                hdwp = DeferWindowPos(hdwp, hActiveEdit, NULL, rcEditor.left, rcEditor.top,
                    rcEditor.right - rcEditor.left, rcEditor.bottom - rcEditor.top,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            else {
                SetWindowPos(hActiveEdit, NULL, rcEditor.left, rcEditor.top,
                    rcEditor.right - rcEditor.left, rcEditor.bottom - rcEditor.top,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }
        else {
            OutputDebugStringW(L"ResizeControls: Active editor handle is NULL!\n");
        }
    }
    if (hdwp) EndDeferWindowPos(hdwp);

    // Check if we have a valid handle for the active editor
    if (hActiveEdit) {
        int margin = 15; // Adjust this value 

        // Get the client rectangle of the editor *after* it has been resized
        RECT rcFormat;
        GetClientRect(hActiveEdit, &rcFormat);

        // Inset the rectangle by the margin amount
        rcFormat.left += margin;
        rcFormat.top += margin;  
        rcFormat.right -= margin;
        rcFormat.bottom -= margin;

        // Set the formatting rectangle within the Rich Edit control
        SendMessage(hActiveEdit, EM_SETRECT, 0, (LPARAM)&rcFormat);
    }
}


void ShowCommandPalette(HWND hWnd)
{
    DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDPALETTE), hWnd, CommandPaletteProc);
}

//this function isn't really needed as we already had created Menu in .rc file, but I have added it for future use
//void ShowPopupMenu(HWND hWnd, int buttonId, int x, int y)
//{
//    //HMENU hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDC_TEXTEDITOR)); // Load base menu resource (if defined)
//    HMENU hMenu = CreateMenu(); // Simpler to always create dynamically
//    HMENU hSubMenu = NULL;
//
//    switch (buttonId) {
//    case ID_FILE_NEW: // Assuming ID_FILE_NEW is the command ID of the "File" toolbar button
//        hSubMenu = CreatePopupMenu();
//        AppendMenu(hSubMenu, MF_STRING, ID_FILE_NEW, L"&New\tCtrl+T");
//        AppendMenu(hSubMenu, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
//        AppendMenu(hSubMenu, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
//        AppendMenu(hSubMenu, MF_STRING, ID_FILE_SAVEAS, L"Save &As...");
//        AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
//        AppendMenu(hSubMenu, MF_STRING, ID_CLOSE_TAB, L"&Close Tab\tCtrl+W");
//        AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
//        AppendMenu(hSubMenu, MF_STRING, IDM_EXIT, L"E&xit");
//        break;
//
//    case IDM_THEME_MENU: // Command ID for the "Theme" button
//        hSubMenu = CreatePopupMenu();
//        // TODO: Add check marks based on current theme
//        AppendMenu(hSubMenu, MF_STRING, IDM_THEME_LIGHT, L"&Light");
//        AppendMenu(hSubMenu, MF_STRING, IDM_THEME_DARK, L"&Dark");
//        AppendMenu(hSubMenu, MF_STRING, IDM_THEME_SYSTEM, L"&System Default");
//        break;
//
//    case IDM_ABOUT: // Command ID for the "Help" button
//        hSubMenu = CreatePopupMenu();
//        AppendMenu(hSubMenu, MF_STRING, IDM_ABOUT, L"&About TextEditor...");
//        // Add other help items if needed
//        break;
//
//    case ID_EDIT_UNDO: // "Edit" button ID
//        hSubMenu = CreatePopupMenu();
//        // Check if undo/redo is possible for the current tab
//        bool canUndo = false;
//        bool canRedo = false;
//        if (currentTab >= 0 && currentTab < openTabs.size() && openTabs[currentTab].historyManager) {
//            canUndo = openTabs[currentTab].historyManager->canUndo();
//            canRedo = openTabs[currentTab].historyManager->canRedo();
//        }
//        AppendMenuW(hSubMenu, MF_STRING | (canUndo ? MF_ENABLED : MF_GRAYED), ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
//        AppendMenuW(hSubMenu, MF_STRING | (canRedo ? MF_ENABLED : MF_GRAYED), ID_EDIT_REDO, L"&Redo\tCtrl+Y");
//        AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);
//        AppendMenuW(hSubMenu, MF_STRING, ID_EDIT_COMMIT, L"Create New Version");
//        AppendMenuW(hSubMenu, MF_STRING, ID_VIEW_HISTORY, L"View &History...\tF5"); // Add History View option
//        // Add Cut, Copy, Paste later if needed (using EM_CUT, EM_COPY, EM_PASTE)
//        break;
//    }
//
//    if (hSubMenu) {
//        // Ensure menu appears correctly relative to the button
//        TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, hWnd, NULL);
//        DestroyMenu(hSubMenu); // Destroy the dynamically created popup menu
//    }
//
//    if (hMenu) DestroyMenu(hMenu); // Destroy the base menu if loaded/created
//}

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
    HWND hParentWnd = GetParent(hEdit);  //get parent window handle
    if (hParentWnd) {
        for (int i = 0; i < openTabs.size(); ++i) {
            if (openTabs[i].hEdit == hEdit) {
                tabIndex = i;
                break;
            }
        }
    } 
    else {
        OutputDebugStringW(L"LoadFileIntoEditor: Could not get parent window handle.\n");
        // Cannot update tab state without parent/tab index, but continue setting text
    }
    if (tabIndex != -1) openTabs[tabIndex].processingHistoryAction = true;

    SetWindowTextW(hEdit, outContent.c_str());

    // Reset undo buffer (ours and potentially the default one) and modification state
    SendMessage(hEdit, EM_EMPTYUNDOBUFFER, 0, 0); // Clear RichEdit internal undo
    SendMessage(hEdit, EM_SETMODIFY, FALSE, 0);    // Mark as unmodified

    // Update tab state after loading
    if (tabIndex != -1) {
        openTabs[tabIndex].textAtLastHistoryPoint = outContent;
        openTabs[tabIndex].textBeforeChange = outContent;
        openTabs[tabIndex].totalChangeSize = 0;
        openTabs[tabIndex].changesSinceLastHistoryPoint = false;
        // Post message to clear the processing flag *after* potential EN_CHANGE
        PostMessage(hParentWnd, WM_POST_APPLY_CHANGE, (WPARAM)hEdit, 0);
    }

    return true;
}

// Function to record a history point
void RecordHistoryPoint(HWND hWnd, int tabIndex, const std::wstring& description) { 
    if (tabIndex < 0 || tabIndex >= openTabs.size() || !openTabs[tabIndex].historyManager) {
        return;
    }

    auto& tab = openTabs[tabIndex];
    OutputDebugStringW((L"RecordHistoryPoint: Called for tab " + std::to_wstring(tabIndex) + L" with description: " + description + L"\n").c_str());

    // Prevent recording if editor is currently being updated by history action
    if (tab.processingHistoryAction) {
        OutputDebugStringW(L"RecordHistoryPoint: Returning early because processingHistoryAction is true.\n");
        return;
    }

    std::wstring currentState = GetRichEditText(tab.hEdit);

    OutputDebugStringW((L"RecordHistoryPoint: Comparing states:\n  Current: [" + currentState + L"]\n  LastHist:[" + tab.textAtLastHistoryPoint + L"]\n").c_str());

    // Avoid recording if text hasn't actually changed from last *recorded history point*
    if (currentState == tab.textAtLastHistoryPoint) {
        OutputDebugStringW(L"RecordHistoryPoint: Returning early because currentState == textAtLastHistoryPoint.\n");
        tab.changesSinceLastHistoryPoint = false; // Reset flag even if no change recorded
		tab.totalChangeSize = 0; // Reset total change size
        return;
    }

    // --- Synchronization Note ---
    // We calculate the diff between the *actual current editor state* and the
    // *state of the last recorded history point*.
    // The new node will be added as a child of the *manager's internal currentNode*.
    // If the user used RichEdit undo/redo, currentNode might not represent textAtLastHistoryPoint.
    // This leads to branches in the history tree, reflecting the divergence. This is acceptable.

    // Calculate the change from the *last recorded history state*
    TextChange change = CalculateTextChange(tab.textAtLastHistoryPoint, currentState, tab.hEdit);

    OutputDebugStringW((L"RecordHistoryPoint: Calculated change for recording: +" + std::to_wstring(change.insertedText.length()) + L" / -" + std::to_wstring(change.deletedText.length()) + L"\n").c_str());

    // --- Check if the change is non-empty ---
    if (change.insertedText.empty() && change.deletedText.empty()) {
        OutputDebugStringW(L"RecordHistoryPoint: Returning early because calculated change is empty.\n");
        tab.changesSinceLastHistoryPoint = false; // Reset flag
        return; // Don't record no-op changes
    }

    OutputDebugStringW(L"RecordHistoryPoint: Calling historyManager->recordChange...\n");

    // Record the change. This implicitly moves the history manager's 'currentNode' forward.
    tab.historyManager->recordChange(change, description);

    OutputDebugStringW(L"RecordHistoryPoint: Updating textAtLastHistoryPoint and resetting flags/size.\n");

    // Update the baseline for the next diff calculation to the *current* state
    tab.textAtLastHistoryPoint = currentState;
    tab.changesSinceLastHistoryPoint = false; // Reset flag, changes up to now are recorded
	tab.totalChangeSize = 0; // Reset total change size

    OutputDebugStringW(L"RecordHistoryPoint: Successfully recorded history point.\n");

    // TODO: Update status bar or log history event
    // Optional: Could prune very old history here if needed
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

		RecordHistoryPoint(GetParent(hTabCtrl), tabIndex, L"File Saved"); // Record history point
        // Mark editor control as unmodified (might be redundant but safe)

        SendMessage(openTabs[tabIndex].hEdit, EM_SETMODIFY, FALSE, 0);
        return true;
    }
    else {
        MessageBox(GetParent(hTabCtrl), L"Failed to save the file.", L"Save Error", MB_OK | MB_ICONERROR);
        return false;
    }
}


// Finds the node in history matching the current editor text and updates
// the history manager's internal pointer. Essential before showing History UI.
void SyncHistoryManagerToEditor(HWND hWnd, int tabIndex) {
    if (tabIndex < 0 || tabIndex >= openTabs.size() || !openTabs[tabIndex].historyManager) {
        return;
    }
    auto& tab = openTabs[tabIndex];
    auto& historyManager = tab.historyManager;

    std::wstring currentState = GetRichEditText(tab.hEdit);

    // Check if current node *already* matches (optimization)
    std::shared_ptr<const HistoryNode> internalCurrentNode = historyManager->getCurrentNode();
    std::wstring stateAtInternalCurrent = historyManager->reconstructStateToNode(internalCurrentNode);

    if (stateAtInternalCurrent == currentState) {
        // Already in sync, nothing to do.
        // Update baseline text just in case it drifted? Usually not needed here.
         // tab.textAtLastHistoryPoint = currentState;
        OutputDebugStringW(L"SyncHistoryManager: Already in sync.\n");
        return;
    }

    // If not in sync, perform the search
    OutputDebugStringW(L"SyncHistoryManager: Editor state differs from internal currentNode. Searching...\n");
    std::shared_ptr<HistoryNode> foundNode = historyManager->findNodeMatchingState(currentState);

    if (foundNode) {
        OutputDebugStringW(L"SyncHistoryManager: Found matching node. Updating internal pointer.\n");
        // Found the node matching the editor's current state.
        // Update the internal pointer *without* changing editor text.
        historyManager->setCurrentNode(foundNode); // Use the new method (see Step III)
        // Update the baseline text to match the newly synced state
        tab.textAtLastHistoryPoint = currentState;
    }
    else {
        // Editor state does not match ANY known state in the history tree!
        // This implies external modification or a bug.
        OutputDebugStringW(L"SyncHistoryManager: WARNING! Editor state not found in history tree.\n");
        // What to do? Options:
        // 1. Log error.
        // 2. Treat current state as a new branch point:
        //    - Calculate change from internalCurrentNode's state to currentState.
        //    - Record this as a new node off internalCurrentNode.
        //    - Then set internalCurrentNode to this new node.
        // 3. For simplicity now: Just log and potentially reset baseline.
        tab.textAtLastHistoryPoint = currentState; // Reset baseline to current unknown state
        // The history tree UI might show the old internalCurrentNode highlighted, which is technically correct
        // but doesn't reflect the editor. The user switching would fix it.
    }
}


// Recursive helper to populate the history TreeView
void PopulateHistoryTreeRecursive(HWND hTree,
    HistoryDialogParams* params,
    std::shared_ptr<const HistoryNode> node,
    HTREEITEM hParentItem,
    HTREEITEM& hCurrentItemTree // Pass by ref to store the HTREEITEM for the current node
)
{
    if (!node || !params) return;

    // 1. Create description string for the node
    std::wstring description;
    std::wstring commitMsg = node->commitMessage; // Get the commit message

    // Format timestamp (using existing fallback)
    time_t tt = std::chrono::system_clock::to_time_t(node->timestamp);
    tm local_tm;
    localtime_s(&local_tm, &tt);
    char timeBuffer[80];
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &local_tm); // Shorter time format for UI? %Y-%m-%d %H:%M:%S is long
    std::string timestampStr(timeBuffer);
    std::wstring wTimestampStr(timestampStr.begin(), timestampStr.end());

    if (node->isRoot()) {
        description = L"[" + wTimestampStr + L"] " + commitMsg;
    }
    else {
        // Format: [Time] Commit Message (+Ins/-Del)
        description = L"[" + wTimestampStr + L"]";

        // Add commit message if present
        if (!commitMsg.empty()) {
            description += L" " + commitMsg;
        }
        else {
            description += L" (Auto)"; // Indicate automatic commit if no message
        }

        // Add change details
        description += L" (+" + std::to_wstring(node->changeFromParent.insertedText.length())
            + L" / -" + std::to_wstring(node->changeFromParent.deletedText.length())
            + L")";
    }


    // 2. Prepare TreeView item struct
    TVINSERTSTRUCT tvis = { 0 };
    tvis.hParent = hParentItem;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
    // IMPORTANT: Need a persistent buffer for pszText if description is dynamically created locally.
    // Easiest way is often to store the description string itself in the map or another structure associated with the item.
    // For simplicity here, we'll risk using the local string pointer, assuming TreeView copies it immediately.
    // A safer approach involves allocating memory or using TVITEMEX's callback mechanism.
    // Let's try the simpler approach first:
    std::vector<wchar_t> textBuffer(description.begin(), description.end());
    textBuffer.push_back(L'\0'); // Null-terminate for pszText
    tvis.item.pszText = textBuffer.data(); // Pointer to the buffer

    tvis.item.lParam = (LPARAM)node.get(); // Raw pointer for map lookup
    tvis.item.state = 0;
    tvis.item.stateMask = TVIS_SELECTED | TVIS_EXPANDED;

    if (node == params->nodeAtEditorState) {
        tvis.item.state |= TVIS_SELECTED | TVIS_EXPANDED; // Select and expand current
    }
    else if (hParentItem == TVI_ROOT && node->children.empty()) {
        // Maybe don't auto-expand root if it has no children? Or always expand root?
        tvis.item.state |= TVIS_EXPANDED;
    }
    else if (node->children.empty() == false) {
        // Optionally expand nodes that have children by default
        // tvis.item.state |= TVIS_EXPANDED;
    }


    // 3. Insert item into TreeView
    HTREEITEM hNewItem = TreeView_InsertItem(hTree, &tvis);

    // 4. Store mapping from HTREEITEM to shared_ptr
    if (hNewItem) {
        // Store mapping from HTREEITEM to shared_ptr AND the description string buffer if needed for safety
        params->treeItemNodeMap[hNewItem] = node;
        if (node == params->nodeAtEditorState) {
            hCurrentItemTree = hNewItem;
        }
    }

    // 5. Recursively add children (process in reverse if you want newest on top within a level)
    // Standard order (oldest child first):
    for (const auto& child : node->children) {
        PopulateHistoryTreeRecursive(hTree, params, child, hNewItem, hCurrentItemTree);
    }
    // Ensure the current node is visible AFTER all items are inserted
    if (hParentItem == TVI_ROOT && hCurrentItemTree) {
        TreeView_EnsureVisible(hTree, hCurrentItemTree);
    }
}

//---------------------------------------------------------------------------
// HistoryDlgProc - Dialog Procedure for the History Tree window
//---------------------------------------------------------------------------
INT_PTR CALLBACK HistoryDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HistoryDialogParams* params = nullptr;
    // Retrieve params pointer stored during WM_INITDIALOG
    if (message != WM_INITDIALOG) {
        params = reinterpret_cast<HistoryDialogParams*>(GetWindowLongPtr(hDlg, DWLP_USER));
    }

    switch (message)
    {
    case WM_INITDIALOG:
    {
        // Retrieve parameters passed from DialogBoxParam
        params = reinterpret_cast<HistoryDialogParams*>(lParam);
        if (!params || !params->historyManager) {
            EndDialog(hDlg, IDCANCEL); // Cannot proceed without params
            return (INT_PTR)FALSE;
        }
        // Store params pointer for later use in other messages
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)params);

        // Get control handles
        HWND hTree = GetDlgItem(hDlg, IDC_HISTORY_TREEVIEW);
        HWND hSwitchButton = GetDlgItem(hDlg, ID_SWITCH_VERSION);
        if (!hTree || !hSwitchButton) {
            EndDialog(hDlg, IDCANCEL); // Controls missing
            return (INT_PTR)FALSE;
        }

        // --- Populate the TreeView ---
        std::shared_ptr<const HistoryNode> root = params->historyManager->getHistoryTreeRoot();
        params->nodeAtEditorState = params->historyManager->getCurrentNode(); // Get the node synced by ShowHistoryTree
        params->treeItemNodeMap.clear(); // Ensure map is empty before population

        HTREEITEM hCurrentItemTree = NULL; // To store the HTREEITEM of the selected node
        PopulateHistoryTreeRecursive(hTree, params, root, TVI_ROOT, hCurrentItemTree);

        // Ensure the initially selected item is visible and selected
        if (hCurrentItemTree) {
            TreeView_SelectItem(hTree, hCurrentItemTree); // Explicitly select
            TreeView_EnsureVisible(hTree, hCurrentItemTree);
        }

        // Initial button state: Disable switch button initially,
        // it will be enabled in WM_NOTIFY if a *different* node is selected.
        EnableWindow(hSwitchButton, FALSE);

        return (INT_PTR)TRUE; // System sets default focus
    }

    case WM_NOTIFY:
    {
        if (!params) break; // Should have params by now

        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_HISTORY_TREEVIEW && pnmh->code == TVN_SELCHANGED)
        {
            HWND hTree = pnmh->hwndFrom; // Handle to the TreeView
            HWND hSwitchButton = GetDlgItem(hDlg, ID_SWITCH_VERSION);
            HTREEITEM hSelectedItem = TreeView_GetSelection(hTree);

            bool enableSwitch = false;
            if (hSelectedItem != NULL) {
                // Find the selected node in our map
                auto it = params->treeItemNodeMap.find(hSelectedItem);
                if (it != params->treeItemNodeMap.end()) {
                    std::shared_ptr<const HistoryNode> selectedNode = it->second;
                    // Enable switch button ONLY if the selected node is NOT the one currently in the editor
                    if (selectedNode != params->nodeAtEditorState) {
                        enableSwitch = true;
                    }
                }
            }
            EnableWindow(hSwitchButton, enableSwitch);
            return (INT_PTR)TRUE;
        }
    }
    break;

    case WM_COMMAND:
    {
        if (!params) break; // Should have params by now

        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case ID_SWITCH_VERSION:
        {
            HWND hTree = GetDlgItem(hDlg, IDC_HISTORY_TREEVIEW);
            HTREEITEM hSelectedItem = TreeView_GetSelection(hTree);

            if (hSelectedItem != NULL) {
                // Find the target node shared_ptr from our map
                auto it = params->treeItemNodeMap.find(hSelectedItem);
                if (it != params->treeItemNodeMap.end()) {
                    std::shared_ptr<const HistoryNode> targetNodeSharedPtr = it->second;

                    // Retrieve the necessary components from params
                    VersionHistoryManager* historyManager = params->historyManager;
                    int tabIndex = params->tabIndex;

                    if (historyManager && tabIndex >= 0 && tabIndex < openTabs.size()) {
                        // --- Perform the switch ---
                        // 1. Get the full text state for the target node
                        std::wstring newState = historyManager->reconstructStateToNode(targetNodeSharedPtr);

                        // 2. Update the main Rich Edit control (this clears RichEdit Undo)
                        SetRichEditText(openTabs[tabIndex].hEdit, newState, targetNodeSharedPtr);

                        // 3. Update the baseline text in the tab info
                        openTabs[tabIndex].textAtLastHistoryPoint = newState;
                        openTabs[tabIndex].changesSinceLastHistoryPoint = false; // State now matches history point

                        // 4. Crucially, update the internal pointer AFTER applying the state
                        historyManager->setCurrentNode(std::const_pointer_cast<HistoryNode>(targetNodeSharedPtr)); // Cast away constness if setCurrentNode takes non-const

                        // 5. Close the dialog indicating success
                        EndDialog(hDlg, IDOK);
                    }
                    else {
                        // Handle error: manager or tab index invalid
                        MessageBoxW(hDlg, L"Error retrieving necessary data to switch version.", L"Error", MB_OK | MB_ICONERROR);
                    }
                }
                else {
                    // Handle error: selected item not found in map? Should not happen.
                    MessageBoxW(hDlg, L"Internal error: Cannot find data for selected history item.", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            return (INT_PTR)TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
    }
    break;

    case WM_DESTROY:
    {
        // Clean up map if necessary (though shared_ptrs handle memory, the map itself lives with params)
        // If params were dynamically allocated (they are not here), delete them.
        // Resetting the DWLP_USER is good practice but not strictly necessary if dialog is destroyed.
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)nullptr);
    }
    break;
    }
    return (INT_PTR)FALSE; // Default processing for unhandled messages
}

//---------------------------------------------------------------------------
// ShowHistoryTree
// Displays the version history dialog for the current tab.
//---------------------------------------------------------------------------
void ShowHistoryTree(HWND hWnd) {
    if (currentTab < 0 || currentTab >= openTabs.size() || !openTabs[currentTab].historyManager) {
        MessageBoxW(hWnd, L"No active tab or history available.", L"History", MB_OK | MB_ICONWARNING);
        return;
    }

    // Ensure any pending automatic changes (like significant change or idle timeout)
    // are committed *before* showing the history or syncing.
    auto& tab = openTabs[currentTab];
    if (tab.changesSinceLastHistoryPoint) {
        OutputDebugStringW(L"ShowHistoryTree: Found pending changes. Recording history point before showing dialog...\n");
        // Kill any pending idle timer as we are committing now.
        if (tab.idleTimerId != 0) {
            KillTimer(hWnd, reinterpret_cast<UINT_PTR>(tab.hEdit));
            tab.idleTimerId = 0;
        }
        // Record the pending change. Use a generic "Auto" message or be more specific if possible.
        // L"Auto (Pending Change)" or L"Auto (Before History View)" might be good descriptions.
        RecordHistoryPoint(hWnd, currentTab, L"Auto (Pending)");
        // Note: RecordHistoryPoint already updates textAtLastHistoryPoint etc.
    }

    // --- Synchronization Step ---
    // Ensure the history manager's internal pointer matches the editor's current state
    // BEFORE showing the dialog. This is crucial for highlighting the correct item.
    SyncHistoryManagerToEditor(hWnd, currentTab);
    // --- End Synchronization Step ---

    // Prepare parameters to pass to the dialog procedure
    HistoryDialogParams params;
    params.historyManager = openTabs[currentTab].historyManager.get(); // Pass raw pointer
    params.tabIndex = currentTab;
    params.hParent = hWnd;
    // params.treeItemNodeMap is initialized empty and populated in WM_INITDIALOG
    // params.nodeAtEditorState is set in WM_INITDIALOG

    // Create the modal dialog box
    // DialogBoxParam function returns when the dialog is closed (e.g., via EndDialog)
    INT_PTR result = DialogBoxParam(
        hInst,                          // Application instance handle
        MAKEINTRESOURCE(IDD_HISTORY_TREE), // Dialog resource ID
        hWnd,                           // Parent window handle
        HistoryDlgProc,                 // Dialog procedure
        (LPARAM)&params                 // Parameter to pass to WM_INITDIALOG
    );

    // Optional: Handle dialog result if needed (e.g., check if IDOK or IDCANCEL was returned)
    if (result == -1) {
        MessageBoxW(hWnd, L"Failed to create history dialog.", L"Error", MB_OK | MB_ICONERROR);
    }
    else if (result == IDOK) {
        // History switch was successful (handled within the dialog proc)
        // Maybe update status bar?
    }
    else {
        // Dialog was cancelled (IDCANCEL or error)
    }

    // Focus back on the editor of the current tab after dialog closes
    if (currentTab >= 0 && currentTab < openTabs.size() && openTabs[currentTab].hEdit) {
        SetFocus(openTabs[currentTab].hEdit);
    }
}


//---------------------------------------------------------------------------
// CommitMessageDlgProc - Dialog Procedure for getting the commit message
//---------------------------------------------------------------------------
INT_PTR CALLBACK CommitMessageDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    CommitMessageParams* pParams = nullptr;

    // Retrieve params pointer stored during WM_INITDIALOG
    if (message != WM_INITDIALOG) {
        pParams = reinterpret_cast<CommitMessageParams*>(GetWindowLongPtr(hDlg, DWLP_USER));
    }

    switch (message) {
    case WM_INITDIALOG:
    {
        // Retrieve parameters passed from DialogBoxParam
        pParams = reinterpret_cast<CommitMessageParams*>(lParam);
        if (!pParams || !pParams->pCommitMessage) {
            EndDialog(hDlg, IDCANCEL); // Cannot proceed without params
            return (INT_PTR)FALSE;
        }
        // Store params pointer for later use
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pParams);

        // Set initial focus to the edit control
        HWND hEdit = GetDlgItem(hDlg, IDC_COMMIT_MESSAGE_EDIT);
        if (hEdit) {
            SetFocus(hEdit);
        }
        return (INT_PTR)FALSE; // We set the focus manually
    }

    case WM_COMMAND:
    {
        if (!pParams) break; // Should have params

        int wmId = LOWORD(wParam);
        switch (wmId) {
        case IDOK:
        {
            HWND hEdit = GetDlgItem(hDlg, IDC_COMMIT_MESSAGE_EDIT);
            if (hEdit) {
                int textLen = GetWindowTextLengthW(hEdit);
                if (textLen > 0) {
                    // Ensure the target string has enough capacity
                    pParams->pCommitMessage->resize(textLen + 1); // +1 for safety, resize later
                    GetWindowTextW(hEdit, &(*pParams->pCommitMessage)[0], textLen + 1);
                    pParams->pCommitMessage->resize(textLen); // Trim null terminator
                }
                else {
                    // User clicked OK without entering text, clear the target string
                    pParams->pCommitMessage->clear();
                }
            }
            else {
                // Edit control not found? Clear the message.
                pParams->pCommitMessage->clear();
            }
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
    }
    break; // End WM_COMMAND

    case WM_CLOSE: // Handle closing via 'X' button
        EndDialog(hDlg, IDCANCEL);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        // Clean up params pointer stored in window long ptr
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)nullptr);
        break;
    }
    return (INT_PTR)FALSE; // Default processing
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    /*OutputDebugStringW((L"*** WndProc Entered - Message: " + std::to_wstring(message) + L" ***\n").c_str());*/

    switch (message)
    {
    case WM_CREATE:
    {
        /*CreateToolbar(hWnd);*/
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
                tab.processingHistoryAction = false;
                break;
            }
        }
        }
        break;

    case WM_SIZE:
        ResizeControls(hWnd);
        break;

    case WM_TIMER:
    {
        UINT_PTR timerId = (UINT_PTR)wParam;
        // Find the tab associated with this timer ID  //Maybe TODO:
        int tabIndex = -1;
        for (int i = 0; i < openTabs.size(); ++i) {
            // Compare against the stored ID OR the HWND used as ID
            if (openTabs[i].idleTimerId != 0 && reinterpret_cast<UINT_PTR>(openTabs[i].hEdit) == timerId) {
                // Check if the timer *actually* belongs to this tab,
                // SetTimer might reuse IDs if we don't manage them carefully.
                // Using the HWND as ID is generally safer if HWNDs are unique.
                if (openTabs[i].idleTimerId == GetWindowLongPtr(openTabs[i].hEdit, GWLP_USERDATA)) { /* Example check if storing ID on HWND */ }
                // Simpler: Assume the HWND cast is the ID if that's what SetTimer used.
                tabIndex = i;
                break;
            }
            // Fallback if using sequential IDs stored in idleTimerId directly
            else if (openTabs[i].idleTimerId == timerId) {
                tabIndex = i;
                break;
            }
        }

        // Process only if the timer belongs to the *currently active* tab
        // (We generally only want idle commits for the tab being actively edited)
        // We *could* allow idle commits for background tabs, but it might be less expected.
        if (tabIndex != -1 && currentTab == tabIndex) {
            auto& tab = openTabs[tabIndex];

            // Kill this specific timer instance *first* regardless of action
            KillTimer(hWnd, timerId);
            tab.idleTimerId = 0; // Mark timer as inactive

            // Check if changes actually occurred since the last recorded point
            if (tab.changesSinceLastHistoryPoint) {
                OutputDebugStringW(L"Idle timer fired. Recording history...\n");
                // Now record the history point because idle time elapsed *after* changes
                RecordHistoryPoint(hWnd, tabIndex, L"Auto (Idle)");
                // RecordHistoryPoint resets cumulativeChangeSize and changesSinceLastHistoryPoint
            }
            else {
                OutputDebugStringW(L"Idle timer fired, but no changes since last history point.\n");
                // Timer fired, but changes were already committed (e.g., by threshold)
                // or the state reverted. Do nothing.
            }
        }
        else if (tabIndex != -1) {
            // Timer fired for an *inactive* tab, or timer ID mismatch somehow.
            // Kill the timer just to be safe.
            OutputDebugStringW(L"Idle timer fired for inactive tab or mismatch. Killing timer.\n");
            KillTimer(hWnd, timerId);
            // Ensure the corresponding tab's ID is cleared if it matches the timerId
            if (reinterpret_cast<UINT_PTR>(openTabs[tabIndex].hEdit) == timerId) {
                openTabs[tabIndex].idleTimerId = 0;
            }
        }
        return 0; // Indicate message processed
    }
    break; // End WM_TIMER
    
    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            /*
            //if (pnmh->hwndFrom == hToolBar && pnmh->code == TBN_DROPDOWN) {
            //    // Handle toolbar button dropdown clicks
            //    LPNMTOOLBAR lpnmTB = (LPNMTOOLBAR)lParam;

            //    // Get button rect
            //    RECT rc;
            //    SendMessage(hToolBar, TB_GETRECT, (WPARAM)lpnmTB->iItem, (LPARAM)&rc);

            //    // Convert to screen coordinates
            //    POINT pt = { rc.left, rc.bottom };
            //    ClientToScreen(hToolBar, &pt);

            //    // Show the appropriate popup menu
            //    ShowPopupMenu(hWnd, lpnmTB->iItem, pt.x, pt.y);

            //    return TBDDRET_DEFAULT; // Prevent default handling if needed
            //}
            */
            // --- Tab Control Selection Change ---
            if (pnmh->idFrom == IDC_TABCTRL && pnmh->code == TCN_SELCHANGE) {
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
            if (currentTab >= 0 && currentTab < openTabs.size() && pnmh->hwndFrom == openTabs[currentTab].hEdit) {
                auto& tab = openTabs[currentTab]; // Use reference for cleaner code

                if (pnmh->code == EN_CHANGE) {
                    // Check processing flag to prevent recursion from history actions
                    if (!tab.processingHistoryAction) {
                        // --- Actions on ANY change (even if not recorded yet) ---
                        tab.isModified = true; // A change occurred
                        UpdateTabTitle(currentTab);
                        UpdateWindowTitle(hWnd);

                        // *** START: Significant Change & Timer Logic ***

                        // 1. Get the current state of the editor
                        std::wstring currentState = GetRichEditText(tab.hEdit);

                        // 2. Calculate the change delta compared to the state *before* this EN_CHANGE
                        //    Note: tab.textBeforeChange holds the state *before* this specific event.
                        TextChange currentDeltaChange = CalculateTextChange(tab.textBeforeChange, currentState, tab.hEdit);

                        // 3. Update the total change size since the last *recorded* history point
                        size_t changeSizeThisEvent = currentDeltaChange.insertedText.length() + currentDeltaChange.deletedText.length();
                        OutputDebugStringW((L"EN_CHANGE: Before accumulation - totalChangeSize=" + std::to_wstring(tab.totalChangeSize)
                            + L", changeSizeThisEvent=" + std::to_wstring(changeSizeThisEvent) + L"\n").c_str());

                        
                        tab.totalChangeSize += changeSizeThisEvent;

                        OutputDebugStringW((L"EN_CHANGE: After accumulation - totalChangeSize=" + std::to_wstring(tab.totalChangeSize) + L"\n").c_str());

                        // 4. Update textBeforeChange to prepare for the *next* EN_CHANGE event
                        tab.textBeforeChange = currentState;

                        // 5. Mark that changes have happened since the last *recorded* point
                        tab.changesSinceLastHistoryPoint = true;

                        // 6. Check if the significant change threshold is met
                        bool committedDueToThreshold = false;
                        if (tab.totalChangeSize >= SIGNIFICANT_CHANGE_THRESHOLD) {
                            OutputDebugStringW((L"EN_CHANGE: Significant change threshold MET (" + std::to_wstring(tab.totalChangeSize) + L" >= " + std::to_wstring(SIGNIFICANT_CHANGE_THRESHOLD) + L"). Attempting to record...\n").c_str());
                            // Kill any pending idle timer immediately
                            if (tab.idleTimerId != 0) {
                                KillTimer(hWnd, reinterpret_cast<UINT_PTR>(tab.hEdit));
                                tab.idleTimerId = 0; // Mark timer as inactive
                            }
                            // Record the history point NOW
                            RecordHistoryPoint(hWnd, currentTab, L"Auto (Significant Change)");
                            // RecordHistoryPoint resets cumulativeChangeSize and changesSinceLastHistoryPoint
                            committedDueToThreshold = true;
                        } else{
                                OutputDebugStringW((L"EN_CHANGE: Threshold NOT met (" + std::to_wstring(tab.totalChangeSize) + L" < " + std::to_wstring(SIGNIFICANT_CHANGE_THRESHOLD) + L").\n").c_str());
                        }

                        // 7. If not committed by threshold, reset/start the idle timer
                        if (!committedDueToThreshold) {
                            // Kill previous timer for this tab, if any (ensures only one timer runs)
                            if (tab.idleTimerId != 0) {
                                KillTimer(hWnd, reinterpret_cast<UINT_PTR>(tab.hEdit));
                            }
                            // Reset/Start the idle timer
                            UINT_PTR timerIdentifier = reinterpret_cast<UINT_PTR>(tab.hEdit);
                            const UINT idleTimeoutMs = IDLE_HISTORY_TIMEOUT_MS;
                            tab.idleTimerId = SetTimer(hWnd,
                                timerIdentifier,
                                idleTimeoutMs,
                                nullptr);
                            if (tab.idleTimerId == 0) {
                                OutputDebugStringW(L"Warning: SetTimer failed in EN_CHANGE!\n");
                            }
                        }
                        // *** END: Significant Change & Timer Logic ***
                    }
                    else {
                        OutputDebugStringW(L"EN_CHANGE: Skipping due to processingHistoryAction flag.\n");
                    }
                }
                else if (pnmh->code == EN_SELCHANGE) {
                    // Handle selection change if needed (e.g., update status bar)
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
        // Inside WndProc -> WM_COMMAND
        case ID_EDIT_REDO:
            if (currentTab >= 0 && currentTab < openTabs.size() && openTabs[currentTab].historyManager) {
                auto& tab = openTabs[currentTab]; // Get reference to the current tab info
                auto& historyManager = tab.historyManager;

                // --- Check if redo is possible ---
                if (historyManager->canRedo()) { //
                    std::vector<std::wstring> branches = historyManager->getRedoBranchDescriptions(); //

                    // --- Check for branching ---
                    if (branches.size() > 1) {
                        // --- More than one redo path: Show Popup Menu ---
                        HMENU hPopup = CreatePopupMenu();
                        if (!hPopup) {
                            MessageBoxW(hWnd, L"Failed to create redo menu.", L"Error", MB_OK | MB_ICONERROR);
                            break; // Exit command handling
                        }

                        // Populate the menu with branch descriptions
                        for (size_t i = 0; i < branches.size(); ++i) {
                            AppendMenuW(hPopup, MF_STRING, ID_REDO_BRANCH_BASE + i, branches[i].c_str());
                        }

                        // Get cursor position to display the menu
                        POINT pt;
                        GetCursorPos(&pt); // Get cursor position in screen coordinates

                        // Display the popup menu and wait for user selection
                        // TPM_RETURNCMD makes TrackPopupMenu return the selected command ID
                        int selectedBranchCmd = TrackPopupMenu(hPopup,
                            TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                            pt.x, pt.y, 0, hWnd, NULL);

                        DestroyMenu(hPopup); // Clean up the menu

                        if (selectedBranchCmd != 0) {
                            // User selected a branch
                            int selectedIndex = selectedBranchCmd - ID_REDO_BRANCH_BASE; // Convert command ID back to index

                            // Validate the index just in case
                            if (selectedIndex >= 0 && selectedIndex < branches.size()) {
                                // Move to the selected child node internally
                                if (historyManager->moveCurrentNodeToChild(selectedIndex)) { //
                                    // Reconstruct the state of the *new* current node
                                    std::shared_ptr<const HistoryNode> newNode = historyManager->getCurrentNode(); //
                                    std::wstring newState = historyManager->getCurrentState(); // Or reconstructStateToNode(newNode)

                                    // Update the Rich Edit control
                                    SetRichEditText(tab.hEdit, newState, newNode); //

                                    // Update tab state
                                    tab.textAtLastHistoryPoint = newState; //
                                    tab.changesSinceLastHistoryPoint = false; //
                                    // Determine modification status based on whether newState matches the saved state
                                    // tab.isModified = ... (compare newState with saved state if tracked)
                                    UpdateTabTitle(currentTab); //
                                    UpdateWindowTitle(hWnd); //
                                }
                                else {
                                    MessageBoxW(hWnd, L"Failed to move to selected redo branch.", L"Error", MB_OK | MB_ICONERROR);
                                }
                            }
                            else {
                                MessageBoxW(hWnd, L"Invalid redo branch selected.", L"Error", MB_OK | MB_ICONERROR);
                            }
                        }
                        // If selectedBranchCmd is 0, the user dismissed the menu - do nothing further.

                    }
                    else {
                        // --- Only one (or zero, though canRedo check prevents zero) redo path: Standard Redo ---
                        // Use moveCurrentNodeToChild with default behavior (moves to the first/only child)
                        if (historyManager->moveCurrentNodeToChild()) { // Use default index behavior
                            // Reconstruct the state of the *new* current node
                            std::shared_ptr<const HistoryNode> newNode = historyManager->getCurrentNode(); //
                            std::wstring newState = historyManager->getCurrentState(); // Or reconstructStateToNode(newNode)


                            // Update the Rich Edit control
                            SetRichEditText(tab.hEdit, newState, newNode); //


                            // Update tab state
                            tab.textAtLastHistoryPoint = newState; //
                            tab.changesSinceLastHistoryPoint = false; //
                            // tab.isModified = ...
                            UpdateTabTitle(currentTab); //
                            UpdateWindowTitle(hWnd); //
                        }
                        else {
                            // Should not happen if canRedo was true and branches.size() <= 1
                            OutputDebugStringW(L"Warning: moveCurrentNodeToChild failed unexpectedly in single-branch redo.\n");
                        }
                    }
                }
                // else: Cannot redo (canRedo() returned false) - do nothing or provide feedback
            }
            break; // End ID_EDIT_REDO
        case ID_VIEW_HISTORY:
            if (currentTab >= 0) {
                SyncHistoryManagerToEditor(hWnd, currentTab); // 
                ShowHistoryTree(hWnd);
            }
            break;
        
        case ID_HISTORY_PREVIOUS: // Ctrl + Alt + Left
            if (currentTab >= 0 && currentTab < openTabs.size()) {
                auto& tab = openTabs[currentTab];
                if (tab.hEdit && tab.historyManager && tab.historyManager->canUndo()) {
                    // 1. Move internal pointer
                    if (tab.historyManager->moveCurrentNodeToParent()) {
                        // 2. Get new state and node
                        std::shared_ptr<const HistoryNode> newNode = tab.historyManager->getCurrentNode();
                        std::wstring newState = tab.historyManager->getCurrentState(); // Or reconstructStateToNode(newNode)

                        // 3. Update editor and tab state (using the modified SetRichEditText)
                        // SetRichEditText now handles updating textAtLastHistoryPoint etc.
                        SetRichEditText(tab.hEdit, newState, newNode);
                    }
                    else {
                        OutputDebugStringW(L"ID_HISTORY_PREVIOUS: moveCurrentNodeToParent failed unexpectedly.\n");
                    }
                }
                else {
                    // Optionally provide feedback (e.g., status bar message or Beep)
                    MessageBeep(MB_ICONASTERISK);
                }
            }
            break; // End ID_HISTORY_PREVIOUS

        case ID_HISTORY_NEXT_CHILD: // Ctrl + Alt + Right
            if (currentTab >= 0 && currentTab < openTabs.size()) {
                auto& tab = openTabs[currentTab];
                if (tab.hEdit && tab.historyManager && tab.historyManager->canRedo()) {
                    std::vector<std::wstring> branches = tab.historyManager->getRedoBranchDescriptions();

                    if (branches.empty()) {
                        // Should not happen if canRedo() is true, but check anyway
                        MessageBeep(MB_ICONASTERISK);
                    }
                    else if (branches.size() == 1) {
                        // --- Only one child: Move directly ---
                        if (tab.historyManager->moveCurrentNodeToChild(0)) { // Move to the first (only) child
                            std::shared_ptr<const HistoryNode> newNode = tab.historyManager->getCurrentNode();
                            std::wstring newState = tab.historyManager->getCurrentState();
                            SetRichEditText(tab.hEdit, newState, newNode);
                        }
                        else {
                            OutputDebugStringW(L"ID_HISTORY_NEXT_CHILD: moveCurrentNodeToChild(0) failed unexpectedly.\n");
                        }
                    }
                    else {
                        // --- Multiple children: Show dialog ---
                        ChooseChildDialogParams params;
                        params.historyManager = tab.historyManager.get(); // Pass raw pointer
                        params.descriptions = branches; // Copy descriptions
                        params.selectedIndex = -1;

                        INT_PTR dlgResult = DialogBoxParam(
                            hInst,
                            MAKEINTRESOURCE(IDD_CHOOSE_CHILD_COMMIT),
                            hWnd, // Parent window
                            ChooseChildCommitDlgProc,
                            (LPARAM)&params
                        );

                        if (dlgResult == IDOK && params.selectedIndex >= 0 && params.selectedIndex < branches.size()) {
                            // User clicked OK and selected a valid index
                            // Move to the selected child node
                            if (tab.historyManager->moveCurrentNodeToChild(params.selectedIndex)) {
                                std::shared_ptr<const HistoryNode> newNode = tab.historyManager->getCurrentNode();
                                std::wstring newState = tab.historyManager->getCurrentState();
                                SetRichEditText(tab.hEdit, newState, newNode);
                            }
                            else {
                                MessageBoxW(hWnd, L"Failed to switch to the selected version.", L"Error", MB_OK | MB_ICONERROR);
                            }
                        }
                        // else: User cancelled or error, do nothing
                    }
                }
                else {
                    // Cannot redo
                    MessageBeep(MB_ICONASTERISK);
                }
            }
            break; // End ID_HISTORY_NEXT_CHILD


            // command for manual commit
        case ID_EDIT_COMMIT:
            if (currentTab >= 0 && currentTab < openTabs.size()) {
                auto& tab = openTabs[currentTab];
                if (tab.hEdit && tab.historyManager) {
                    // --- Get Commit Message ---
                    std::wstring userCommitMessage = L""; // Default to empty
                    CommitMessageParams params;
                    params.pCommitMessage = &userCommitMessage;

                    // Show the commit message dialog
                    INT_PTR dlgResult = DialogBoxParam(
                        hInst,
                        MAKEINTRESOURCE(IDD_COMMIT_MESSAGE),
                        hWnd, // Parent window
                        CommitMessageDlgProc,
                        (LPARAM)&params
                    );

                    if (dlgResult == IDOK) {
                        // --- User confirmed, proceed with creating version ---

                        // 1. Get current state
                        std::wstring currentState = GetRichEditText(tab.hEdit);

                        // 2. Check if state actually changed since last *recorded* point
                        if (currentState == tab.textAtLastHistoryPoint) {
                            MessageBoxW(hWnd, L"No changes detected since the last version point.\nManual version not created.", L"Create Version", MB_OK | MB_ICONINFORMATION);
                            break; // Exit case
                        }

                        // 3. Calculate the change from the last recorded state
                        TextChange change = CalculateTextChange(tab.textAtLastHistoryPoint, currentState, tab.hEdit);

                        // 4. Ensure the change is not empty (double check)
                        if (change.insertedText.empty() && change.deletedText.empty()) {
                            MessageBoxW(hWnd, L"Internal check: No changes detected.\nManual version not created.", L"Create Version", MB_OK | MB_ICONWARNING);
                            break; // Exit case
                        }

                        // 5. Record the change WITH the user's message
                        // If user entered no message, userCommitMessage will be empty, which is fine.
                        tab.historyManager->recordChange(change, userCommitMessage);

                        // 6. Update the baseline for the next diff
                        tab.textAtLastHistoryPoint = currentState;
                        tab.changesSinceLastHistoryPoint = false; // Reset flag

                        // 7. Provide feedback
                        MessageBoxW(hWnd, (L"Version created." + (userCommitMessage.empty() ? L"" : L"\nMessage: " + userCommitMessage)).c_str(), L"Commit", MB_OK | MB_ICONINFORMATION);
                    }
                    // else: User cancelled (dlgResult == IDCANCEL or error), do nothing.
                }
            }
            break; // End ID_EDIT_COMMIT

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

	////TODO: this keyboard shortcuts handling is not working right now, need changes
 //   case WM_KEYDOWN:
 //       // Handle accelerators here if not using an accelerator table or for specific needs
 //       if (GetKeyState(VK_CONTROL) & 0x8000) { // Check if Control key is pressed
 //           switch (wParam) {
 //           case 'P': // Ctrl+P
 //               ShowCommandPalette(hWnd);
 //               return 0; 
 //           case 'T': // Ctrl+T
 //               PostMessage(hWnd, WM_COMMAND, ID_FILE_NEW, 0); // Use PostMessage to avoid re-entrancy issues
 //               return 0;
 //           case 'W': // Ctrl+W
 //               if (currentTab >= 0) {
 //                   PostMessage(hWnd, WM_COMMAND, ID_CLOSE_TAB, 0);
 //               }
 //               return 0; 
 //           case 'O': // Ctrl+O
 //               PostMessage(hWnd, WM_COMMAND, ID_FILE_OPEN, 0);
 //               return 0; 
 //           case 'S': // Ctrl+S
 //               if (currentTab >= 0) {
 //                   // Check Shift key for Save As (Ctrl+Shift+S)
 //                   if (GetKeyState(VK_SHIFT) & 0x8000) {
 //                       PostMessage(hWnd, WM_COMMAND, ID_FILE_SAVEAS, 0);
 //                   }
 //                   else {
 //                       PostMessage(hWnd, WM_COMMAND, ID_FILE_SAVE, 0);
 //                   }
 //               }
 //               return 0; 
 //           }
 //       }
 //       return DefWindowProc(hWnd, message, wParam, lParam); // Default handling for other keys


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


INT_PTR CALLBACK ChooseChildCommitDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    ChooseChildDialogParams* pParams = nullptr;

    if (message == WM_INITDIALOG) {
        pParams = reinterpret_cast<ChooseChildDialogParams*>(lParam);
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pParams);

        HWND hList = GetDlgItem(hDlg, IDC_CHILD_COMMIT_LIST);
        if (hList && pParams) {
            // Populate the list box
            for (const auto& desc : pParams->descriptions) {
                SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)desc.c_str());
            }
            // Select the first item by default
            if (!pParams->descriptions.empty()) {
                SendMessage(hList, LB_SETCURSEL, 0, 0);
            }
        }
        return (INT_PTR)TRUE;
    }

    pParams = reinterpret_cast<ChooseChildDialogParams*>(GetWindowLongPtr(hDlg, DWLP_USER));
    if (!pParams) return (INT_PTR)FALSE; // Should have params by now

    switch (message) {
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmId == IDOK) {
            HWND hList = GetDlgItem(hDlg, IDC_CHILD_COMMIT_LIST);
            int idx = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                pParams->selectedIndex = idx; // Store the selected index
                EndDialog(hDlg, IDOK);       // Close dialog indicating success
            }
            else {
                // No selection, maybe show a message? Or just do nothing.
                MessageBoxW(hDlg, L"Please select a version.", L"Selection Required", MB_OK | MB_ICONINFORMATION);
            }
            return (INT_PTR)TRUE;
        }
        else if (wmId == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        else if (wmId == IDC_CHILD_COMMIT_LIST && wmEvent == LBN_DBLCLK) {
            // Treat double-click as OK
            PostMessage(hDlg, WM_COMMAND, IDOK, 0);
            return (INT_PTR)TRUE;
        }
        break; // End WM_COMMAND
    }
    case WM_CLOSE: // Handle closing via 'X' button
        EndDialog(hDlg, IDCANCEL);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)nullptr);
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
