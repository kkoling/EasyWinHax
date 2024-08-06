#include "..\..\..\src\hax.h"
#include <iostream>

// This is a example DLL for hooking and drawing within a DirectX 12 application.
// Compile the project and inject it into a process rendering with DirectX 12.
// A dll-injector built with EasyWinHax can be found here:
// https://github.com/belazr/JackieBlue

static hax::Bench bench("200 x hkPresent", 200u);

static hax::draw::dx12::Backend backend;
static hax::draw::Engine engine{ &backend };

static HANDLE hHookSemaphore;
static hax::in::TrampHook* pPresentHook;

HRESULT __stdcall hkPresent(IDXGISwapChain3* pSwapChain, UINT syncInterval, UINT flags) {
	bench.start();

	engine.beginFrame(pSwapChain);

	bench.end();
	bench.printAvg();

	if (GetAsyncKeyState(VK_END) & 1) {
		pPresentHook->disable();
		const hax::draw::dx12::tPresent pPresent = reinterpret_cast<hax::draw::dx12::tPresent>(pPresentHook->getOrigin());
		const HRESULT res = pPresent(pSwapChain, syncInterval, flags);
		ReleaseSemaphore(hHookSemaphore, 1l, nullptr);

		return res;
	}

	return reinterpret_cast<hax::draw::dx12::tPresent>(pPresentHook->getGateway())(pSwapChain, syncInterval, flags);
}


void cleanup(HANDLE hSemaphore, hax::in::TrampHook* pHook, FILE* file) {

	if (pHook) {
		delete pHook;
	}

	if (hSemaphore) {
		CloseHandle(hSemaphore);
		// just to be save
		Sleep(10ul);
	}

	if (file) {
		fclose(file);
	}

	FreeConsole();
	// just to be save
	Sleep(500ul);
}


DWORD WINAPI haxThread(HMODULE hModule) {

	if (!AllocConsole()) {
		// just to be save
		Sleep(500ul);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	FILE* file{};

	if (freopen_s(&file, "CONOUT$", "w", stdout) || !file) {
		cleanup(hHookSemaphore, pPresentHook, file);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	hHookSemaphore = CreateSemaphoreA(nullptr, 0l, 1l, nullptr);

	if (!hHookSemaphore) {
		cleanup(hHookSemaphore, pPresentHook, file);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	void* pDXGISwapChain3VTable[9]{};

	if (!hax::draw::dx12::getDXGISwapChain3VTable(pDXGISwapChain3VTable, sizeof(pDXGISwapChain3VTable))) {
		cleanup(hHookSemaphore, pPresentHook, file);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	constexpr unsigned int PRESENT_OFFSET = 8ul;
	BYTE* const pPresent = reinterpret_cast<BYTE*>(pDXGISwapChain3VTable[PRESENT_OFFSET]);

	if (!pPresent) {
		cleanup(hHookSemaphore, pPresentHook, file);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	pPresentHook = new hax::in::TrampHook(pPresent, reinterpret_cast<BYTE*>(hkPresent), 0x5);

	if (!pPresentHook) {
		cleanup(hHookSemaphore, pPresentHook, file);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	if (!pPresentHook->enable()) {
		cleanup(hHookSemaphore, pPresentHook, file);

		FreeLibraryAndExitThread(hModule, 0ul);
	}

	std::cout << "Hooked at: 0x" << std::hex << reinterpret_cast<uintptr_t>(pPresentHook->getOrigin()) << std::dec << std::endl;

	WaitForSingleObject(hHookSemaphore, INFINITE);

	cleanup(hHookSemaphore, pPresentHook, file);

	FreeLibraryAndExitThread(hModule, 0ul);
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID) {

	if (reasonForCall != DLL_PROCESS_ATTACH) {

		return TRUE;
	}

	DisableThreadLibraryCalls(hModule);
	const HANDLE hThread = CreateThread(nullptr, 0u, reinterpret_cast<LPTHREAD_START_ROUTINE>(haxThread), hModule, 0ul, nullptr);

	if (!hThread) {

		return TRUE;
	}

	CloseHandle(hThread);

	return TRUE;
}