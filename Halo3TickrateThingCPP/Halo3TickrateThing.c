#include <assert.h>
#include <stdbool.h>

#include <windows.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <stdint.h>

#pragma comment(lib, "legacy_stdio_definitions.lib")

static volatile HANDLE mcc_thread_process = INVALID_HANDLE_VALUE;
static volatile uintptr_t mcc_thread_tickrate_address = 0;
static volatile uintptr_t mcc_thread_time_per_tick_address = 0;

static HANDLE mcc_scanner_thread = NULL;
static HWND mcc_scanner_window = NULL;
static BOOL should_close_mcc_scanner_thread = false;

static HWND mcc_scanner_window_checkbox = NULL;
static HWND mcc_scanner_window_status_label = NULL;

#define ID_CHECKBOX1 1
#define ID_STATUS_LABEL 2

const TCHAR* k_mcc_halo3_module_name = _T("halo3.dll");
const TCHAR* k_mcc_process_name = _T("MCC-Win64-Shipping.exe");
const TCHAR* k_window_class = _T("TickrateChangerClass");

uintptr_t resolve_pointer_chain(uintptr_t base_address, unsigned int offsets[], size_t num_offsets)
{
	uintptr_t current_address = base_address;
	for (size_t i = 0; i < num_offsets; i++)
	{
		// Add offset before dereferencing
		current_address += offsets[i];

		// Don't dereference the pointer on the last offset
		if (i < num_offsets - 1)
		{
			// Read memory
			uintptr_t temp_address = 0;
			if (ReadProcessMemory(InterlockedCompareExchangePointer(&mcc_thread_process, NULL, NULL),
				(LPVOID)current_address,
				&temp_address,
				sizeof(temp_address),
				NULL))
			{
				current_address = temp_address;
			}
			else
			{
				return 0;  // failed to read memory
			}
		}
	}
	return current_address;
}

bool acquired_mcc_process()
{
	HANDLE mcc_process = InterlockedCompareExchangePointer(&mcc_thread_process, NULL, NULL);
	return (mcc_process != INVALID_HANDLE_VALUE);
};

uintptr_t find_mcc_halo3_address()
{
	HANDLE mcc_process = InterlockedCompareExchangePointer(&mcc_thread_process, NULL, NULL);
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(mcc_process));
	HMODULE module = NULL;

	MODULEENTRY32 module_entry = { 0 };
	module_entry.dwSize = sizeof(module_entry);

	if (Module32First(snapshot, &module_entry))
	{
		do
		{
			if (!_tcscmp(k_mcc_halo3_module_name, module_entry.szModule))
			{
				module = module_entry.hModule;
				break;
			}
		} while (Module32Next(snapshot, &module_entry));
	}
	CloseHandle(snapshot);

	return (uintptr_t)module;
}

bool scan_for_mcc()
{
	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	PROCESSENTRY32 pe32 = { .dwSize = sizeof(PROCESSENTRY32) };
	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);
		return false;
	}

	do
	{
		// Look for the MCC process
		if (_tcscmp(k_mcc_process_name, pe32.szExeFile) == 0)
		{
			HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID);
			if (!hProcess)
			{
				continue;
			}

			InterlockedExchangePointer(&mcc_thread_process, hProcess);

			uintptr_t mcc_halo3_address = find_mcc_halo3_address();
			if (mcc_halo3_address == 0)
			{
				continue;
			}

			// Resolve the addresses
			unsigned int tickRateOffsets[] = { 0x02012630, 0x498, 0xC8, 0x4 };
			unsigned int timePerTickOffsets[] = { 0x02012630, 0x498, 0xC8, 0x8 };
			uintptr_t tickTimeAddr = resolve_pointer_chain(mcc_halo3_address, tickRateOffsets, sizeof(tickRateOffsets) / sizeof(unsigned int));
			uintptr_t timePerTickAddr = resolve_pointer_chain(mcc_halo3_address, timePerTickOffsets, sizeof(timePerTickOffsets) / sizeof(unsigned int));

			// Update the global addresses
			InterlockedExchange64(&mcc_thread_tickrate_address, tickTimeAddr);
			InterlockedExchange64(&mcc_thread_time_per_tick_address, timePerTickAddr);
		}
	} while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);

	return acquired_mcc_process();
}

DWORD WINAPI mcc_scanner_thread_proc(LPVOID lpThreadParameter)
{
	while (!InterlockedCompareExchange(&should_close_mcc_scanner_thread, FALSE, FALSE))
	{
		scan_for_mcc();
		Sleep(100);
	}

	return 0;
}

void create_mcc_scanner_thread()
{
	mcc_scanner_thread = CreateThread(
		NULL,
		0,
		mcc_scanner_thread_proc,
		NULL,
		0,
		NULL);

	assert(mcc_scanner_thread != NULL);
}

void wait_for_mcc_scanner_thread()
{
	WaitForSingleObject(mcc_scanner_thread, INFINITE);
}

void set_mcc_tickrate(unsigned int tickrate)
{
	HANDLE mcc_process = InterlockedCompareExchangePointer(&mcc_thread_process, NULL, NULL);
	uintptr_t mcc_tickrate_address = InterlockedCompareExchange64(&mcc_thread_tickrate_address, 0, 0);
	uintptr_t mcc_time_per_tick_address = InterlockedCompareExchange64(&mcc_thread_time_per_tick_address, 0, 0);

	float desired_time_per_tick = 1.f / tickrate;

	if (mcc_process != INVALID_HANDLE_VALUE && mcc_tickrate_address != 0)
	{
		// Read current values
		unsigned int current_tickrate;
		float current_time_per_tick;
		ReadProcessMemory(mcc_process, (LPVOID)mcc_tickrate_address, &current_tickrate, sizeof(current_tickrate), NULL);
		ReadProcessMemory(mcc_process, (LPVOID)mcc_time_per_tick_address, &current_time_per_tick, sizeof(current_time_per_tick), NULL);

		// Check if the values are different and need updating
		if (current_tickrate != tickrate)
		{
			bool success = WriteProcessMemory(mcc_process, (LPVOID)mcc_tickrate_address, &tickrate, sizeof(tickrate), NULL);
			assert(success == true);
		}

		if (current_time_per_tick != desired_time_per_tick)
		{
			bool success = WriteProcessMemory(mcc_process, (LPVOID)mcc_time_per_tick_address, &desired_time_per_tick, sizeof(desired_time_per_tick), NULL);
			assert(success == true);
		}
	}
}

void update_mcc_scanner_window_status()
{
	if (acquired_mcc_process())
	{
		SetWindowText(mcc_scanner_window_status_label, _T("Status: Connected to Game."));
		EnableWindow(mcc_scanner_window_checkbox, TRUE);
	}
	else
	{
		SetWindowText(mcc_scanner_window_status_label, _T("Status: Not Connected to Game."));
		EnableWindow(mcc_scanner_window_checkbox, FALSE);
	}
}

LRESULT CALLBACK mcc_scanner_window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
	{
		mcc_scanner_window_checkbox = CreateWindow(
			_T("BUTTON"),
			_T("30 Tickrate"),
			WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			10,
			10,
			120,
			30,
			hwnd,
			(HMENU)ID_CHECKBOX1,
			NULL,
			NULL);
		mcc_scanner_window_status_label = CreateWindow(
			_T("STATIC"),
			_T("Status: Not Connected to Game."),
			WS_VISIBLE | WS_CHILD,
			10,
			50,
			250,
			25,
			hwnd,
			(HMENU)ID_STATUS_LABEL,
			NULL,
			NULL);
	}
	break;

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case ID_CHECKBOX1:
			set_mcc_tickrate(
				(IsDlgButtonChecked(hwnd, ID_CHECKBOX1) == BST_CHECKED) ? 30 : 60);
			break;
		}
	}
	break;

	case WM_DESTROY:
	{
		PostQuitMessage(0);
		InterlockedExchange(&should_close_mcc_scanner_thread, TRUE);
	}
	break;

	case WM_TIMER:
		update_mcc_scanner_window_status();
		break;

	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}

void create_mcc_scanner_window(HINSTANCE hInstance, int nCmdShow)
{
	WNDCLASS win_class =
	{
		.lpfnWndProc = mcc_scanner_window_proc,
		.hInstance = hInstance,
		.lpszClassName = k_window_class,
		.hbrBackground = GetSysColorBrush(COLOR_3DFACE)
	};

	RegisterClass(&win_class);

	mcc_scanner_window = CreateWindow(
		k_window_class,
		_T("Tickrate Changer"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		300,
		150,
		NULL,
		NULL,
		hInstance,
		NULL);
	assert(mcc_scanner_window != NULL);

	ShowWindow(mcc_scanner_window, nCmdShow);
	UpdateWindow(mcc_scanner_window);
	update_mcc_scanner_window_status();

	// Set a timer to periodically update the status
	SetTimer(mcc_scanner_window, 1, 500, NULL); // 1 second intervals
}

void pump_mcc_scanner_window_events()
{
	MSG msg = { 0 };
	BOOL success = FALSE;

	while ((success = GetMessage(&msg, mcc_scanner_window, 0, 0)) != 0)
	{
		if (success == -1)
		{
			return;
		}
		else
		{
			static bool old_status = false;
			bool current_status = acquired_mcc_process();

			if (current_status != old_status)
			{
				update_mcc_scanner_window_status();
				old_status = current_status;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	create_mcc_scanner_thread();
	create_mcc_scanner_window(hInstance, nCmdShow);
	pump_mcc_scanner_window_events();
	wait_for_mcc_scanner_thread();

	return EXIT_SUCCESS;
}

