#include <Egg/Utility.h>
#include <chrono>
#include "MyApp.h"

std::unique_ptr<MyApp> app{ nullptr };

UINT_PTR timerHandle = 0;

void TimerProcess(HWND windowHandle, UINT a1, UINT_PTR a2, DWORD a3) {
	PostMessage(windowHandle, WM_PAINT, 0, 0);
}

LRESULT CALLBACK WindowProcess(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
	switch(message) {

	case WM_DESTROY:
		app->Destroy();
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		if(app != nullptr) {
			int height = HIWORD(lParam);
			int width = LOWORD(lParam);
			app->Resize(width, height);
		}
		break;
		/*
		when resizing or moving the window we always have to "left click" somewhere, NC: non client area => borders of the window
		when we hold down left click we'll set up a timer to continue sending WM paint messages to be able to repaint the window not too often, 16ms in this example,
		so the window will stay responsive while the game model is being updated
		*/
	case WM_NCLBUTTONDOWN:
		timerHandle = SetTimer(windowHandle, 0, 16, TimerProcess);
		break;
	case WM_NCLBUTTONUP:
		KillTimer(windowHandle, timerHandle);
		break;
	case WM_PAINT:
		app->Run();
		break;
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_MOUSEMOVE:
	case WM_MOUSEWHEEL:
	case WM_KILLFOCUS:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
		app->ProcessMessage(windowHandle, message, wParam, lParam);
		break;
	}

	return DefWindowProcW(windowHandle, message, wParam, lParam);
}

HWND InitWindow(HINSTANCE hInstance) {

	const wchar_t * windowClassName = L"ClassName";

	WNDCLASSW windowClass;
	ZeroMemory(&windowClass, sizeof(WNDCLASSW));

	windowClass.lpfnWndProc = WindowProcess;
	windowClass.lpszClassName = windowClassName;
	windowClass.hInstance = hInstance;

	RegisterClassW(&windowClass);

	HWND wnd = CreateWindowExW(0,
							   windowClassName,
							   L"Fire",
							   WS_OVERLAPPEDWINDOW,
							   CW_USEDEFAULT,
							   CW_USEDEFAULT,
							   CW_USEDEFAULT,
							   CW_USEDEFAULT,
							   NULL,
							   NULL,
							   hInstance,
							   NULL);

	ASSERT(wnd != NULL, "Failed to create window");

	return wnd;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR command, _In_ INT nShowCmd) 
{

	HWND windowHandle = InitWindow(hInstance);
	// DirectX stuff
	com_ptr<ID3D12Debug> debugController{ nullptr };
	com_ptr<IDXGIFactory5> dxgiFactory{ nullptr };
	com_ptr<IDXGISwapChain3> swapChain{ nullptr };
	com_ptr<ID3D12Device> device{ nullptr };
	com_ptr<ID3D12CommandQueue> commandQueue{ nullptr };

	// debug controller 
	DX_API("Failed to create debug layer")
		D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()));

	//debugController->EnableDebugLayer();

	// needed to load WIC files (Windows Imaging Component) such as Jpg-s.
	DX_API("Failed to initialize COM library (ImportTexture)")
		CoInitialize(NULL);

	DX_API("Failed to create DXGI factory")
		CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

	std::vector<com_ptr<IDXGIAdapter1>> adapters;
	Egg::Utility::GetAdapters(dxgiFactory.Get(), adapters);

	// select your adapter here, NULL = system default
	IUnknown * selectedAdapter = (adapters.size() > 0) ? adapters[0].Get() : NULL;

	DX_API("Failed to create D3D Device")
		D3D12CreateDevice(selectedAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.GetAddressOf()));

	// Create Command Queue

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc;
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	commandQueueDesc.NodeMask = 0;

	DX_API("Failed to create command queue")
		device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));

	// swap chain creation
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
	// if you specify width/height as 0, the CreateSwapChainForHwnd will query it from the output window
	swapChainDesc.Width = 0;
	swapChainDesc.Height = 0;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2; // back buffer depth
	swapChainDesc.Scaling = DXGI_SCALING_NONE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	swapChainDesc.Flags = 0;

	DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDesc = { 0 };
	swapChainFullscreenDesc.RefreshRate = DXGI_RATIONAL{ 60, 1 };
	swapChainFullscreenDesc.Windowed = true;
	swapChainFullscreenDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
	swapChainFullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST;

	com_ptr<IDXGISwapChain1> tempSwapChain;

	DX_API("Failed to create swap chain for HWND")
		dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), windowHandle, &swapChainDesc, &swapChainFullscreenDesc, NULL, tempSwapChain.GetAddressOf());

	DX_API("Failed to cast swap chain")
		tempSwapChain.As(&swapChain);

	DX_API("Failed to make window association") // disable ALT+Enter shortcut to full screen mode
		dxgiFactory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);

	app = std::make_unique<MyApp>();
	app->SetDevice(device);
	app->SetCommandQueue(commandQueue);
	app->SetSwapChain(swapChain);
	
	app->CreateResources();
	app->CreateSwapChainResources();
	app->LoadAssets();

	ShowWindow(windowHandle, nShowCmd);
	MSG winMessage = { 0 };

	while(winMessage.message != WM_QUIT) {
		if(PeekMessage(&winMessage, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&winMessage);
			DispatchMessage(&winMessage);
		} else {
			app->Run();
		}
	}

	CoUninitialize();

	return 0;
}
