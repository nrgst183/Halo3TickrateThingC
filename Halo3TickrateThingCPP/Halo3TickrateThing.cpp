#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <tlhelp32.h>

#define ID_CHECKBOX1 1
#define ID_STATUS_LABEL 2

HWND hCheckbox1;
HWND hStatusLabel;

bool oldStatus = false;
HANDLE mccProcess;


bool findMccProcess() {
    const WCHAR* processName = L"MCC-Win64-Shipping.exe";
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;

    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    mccProcess = NULL;
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to take snapshot of the process list." << std::endl;
        return false;
    }

    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hProcessSnap, &pe32)) {
        std::cerr << "Failed to gather information on system processes." << std::endl;
        CloseHandle(hProcessSnap);
        return false;
    }

    do {
        if (wcscmp(processName, pe32.szExeFile) == 0) {
            mccProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID);
            CloseHandle(hProcessSnap);
            return true;
        }
    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    return false;
}

bool getIsHalo3Hooked() {

    if (mccProcess == NULL) 
    {
        if (findMccProcess())
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    return false;
}

DWORD readMemoryValue(HANDLE process, LPCVOID address) {
    DWORD value;
    ReadProcessMemory(process, address, &value, sizeof(value), nullptr);
    return value;
}

void writeMemoryValue(HANDLE process, LPVOID address, DWORD value) {
    WriteProcessMemory(process, address, &value, sizeof(value), nullptr);
}

void updateStatus() {
    if (getIsHalo3Hooked()) {
        SetWindowText(hStatusLabel, _T("Status: Connected to Game."));
    }
    else {
        SetWindowText(hStatusLabel, _T("Status: Not Connected to Game."));
    }
}

void setTickRate(bool is30) {
    if (getIsHalo3Hooked()) {
        // Dummy logic for setting the tick rate goes here.
        // This should involve writing the desired values to the memory of the game process.
    }
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        hCheckbox1 = CreateWindow(TEXT("BUTTON"), TEXT("30 Tickrate"), WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            10, 10, 120, 30, hwnd, (HMENU)ID_CHECKBOX1, nullptr, nullptr);
        hStatusLabel = CreateWindow(TEXT("STATIC"), TEXT("Status: Not Connected to Game."), WS_VISIBLE | WS_CHILD,
            10, 50, 250, 25, hwnd, (HMENU)ID_STATUS_LABEL, nullptr, nullptr);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_CHECKBOX1:
            setTickRate(IsDlgButtonChecked(hwnd, ID_CHECKBOX1) == BST_CHECKED);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = { 0 };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = hInstance;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    wc.lpszClassName = TEXT("TickrateChangerClass");

    RegisterClass(&wc);

    HWND hwnd = CreateWindow(TEXT("TickrateChangerClass"), TEXT("Tickrate Changer"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);

        auto result = getIsHalo3Hooked();

        if (result != oldStatus) {
            updateStatus();
        }

        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}
