// WindowsProject1.cpp : Complete working Windows desktop app in C++ with Command Palette

#include "framework.h"
#include "WindowsProject1.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <richedit.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100
#define IDC_TABCTRL    1001
#define IDC_TEXTBOX    1002
#define IDC_COMMANDPALETTE 1003
#define IDD_COMMANDPALETTE 2001

// Global Variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hEditBox;

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    CommandPaletteProc(HWND, UINT, WPARAM, LPARAM);

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

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_WINDOWSPROJECT1, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    MSG msg;
    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WINDOWSPROJECT1));

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
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WINDOWSPROJECT1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

void CreateMainEditor(HWND hWnd)
{
    LoadLibrary(TEXT("Msftedit.dll"));
    hEditBox = CreateWindowEx(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
        0, 0, 0, 0,
        hWnd, (HMENU)IDC_TEXTBOX, hInst, NULL);

    SetWindowText(hEditBox, L"# Welcome to Fextify\r\n\r\nThis is a *markdown* editor clone.");
}

void ResizeEditor(HWND hWnd)
{
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    MoveWindow(hEditBox, 0, 0, rcClient.right, rcClient.bottom, TRUE);
}

void ShowCommandPalette(HWND hWnd)
{
    DialogBox(hInst, MAKEINTRESOURCE(IDD_COMMANDPALETTE), hWnd, CommandPaletteProc);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateMainEditor(hWnd);
        break;

    case WM_SIZE:
        ResizeEditor(hWnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        break;

    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'P') {
            ShowCommandPalette(hWnd);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

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

INT_PTR CALLBACK CommandPaletteProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hListBox;

    switch (message)
    {
    case WM_INITDIALOG:
        hListBox = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
            10, 10, 260, 100,
            hDlg, (HMENU)IDC_COMMANDPALETTE, hInst, NULL);

        for (int i = 0; i < _countof(commands); ++i) {
            SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)commands[i]);
        }
        SetFocus(hListBox);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_COMMANDPALETTE && HIWORD(wParam) == LBN_DBLCLK) {
            int sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
            if (sel == 3) { // "Exit"
                EndDialog(hDlg, 0);
                PostQuitMessage(0);
            }
            else {
                MessageBox(hDlg, commands[sel], L"Command Executed", MB_OK);
            }
            EndDialog(hDlg, 0);
            return (INT_PTR)TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}
