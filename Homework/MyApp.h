#pragma once

#include <Egg/Common.h>

#include <vector>
#include <chrono>
#include <sstream>
#include <cstdlib>

#include "DescriptorHeap.h"

#include "RenderingSystem.h"
RenderingSystem renderer;

#include "PhysicsSystem.h"
PxSystem physics;

/*
TODO:
- multiple textures: roughness, metalness, heightfield, normal maps, etc...
- scene loading workflow
- more integrated light managament
- deferred rendering

- Lua
- game logic stuff (?)
	- gui (imgui)
	- fps
	- ball controlling (inspired by "kirby's dream course")

- shadow maps
- displacement mapping
- tesselation
- instancing

- SSAO
- lightshaft
- bloom
- tonemapping

- preconvolved cubemap loading
*/

class MyApp
{
	// main resources
	com_ptr<ID3D12Device> device;
	com_ptr<IDXGISwapChain3> swapChain;
	com_ptr<ID3D12CommandQueue> commandQueue;

	// --- ----
	// --- RENDER TARGETS
	// --- ----
	uint32_t backBufferDepth;
	D3D12_VIEWPORT viewPort;
	D3D12_RECT scissorRect;
	// rtv
	std::vector<com_ptr<ID3D12Resource>> renderTargets;
	GG::DescriptorHeap::P rtvHeap;
	// depth
	com_ptr<ID3D12Resource> depthStencilBuffer;
	GG::DescriptorHeap::P dsvHeap;

	// --- ----
	// --- COMMAND LISTS
	// --- ----
	com_ptr<ID3D12CommandAllocator> commandAllocator;
	com_ptr<ID3D12GraphicsCommandList> commandList;
	com_ptr<ID3D12GraphicsCommandList> commandList2;

	// sync objects
	com_ptr<ID3D12Fence> fence;
	HANDLE fenceEvent;
	unsigned long long fenceValue;
	unsigned int frameIndex;
	
	// time objects
	using clock_type = std::chrono::high_resolution_clock;
	std::chrono::time_point<clock_type> timestampStart;
	std::chrono::time_point<clock_type> timestampEnd;
	float elapsedTime;

	void WaitForPreviousFrame() 
	{
		const UINT64 fv = fenceValue;
		DX_API("Failed to signal from command queue")
			commandQueue->Signal(fence.Get(), fv);

		fenceValue++;

		if (fence->GetCompletedValue() < fv) {
			DX_API("Failed to sign up for event completion")
				fence->SetEventOnCompletion(fv, fenceEvent);
			WaitForSingleObject(fenceEvent, INFINITE);
		}

		frameIndex = swapChain->GetCurrentBackBufferIndex();
	}

public:
	~MyApp() = default;

	MyApp()
	{
		timestampStart = clock_type::now();
		timestampEnd = timestampStart;
	}

	void Run() {
		timestampEnd = clock_type::now();
		float deltaTime = std::chrono::duration<float>(timestampEnd - timestampStart).count();
		elapsedTime += deltaTime;
		timestampStart = timestampEnd;
		Update(deltaTime, elapsedTime);
		Render();
	}

	void Update(float dt, float T) 
	{
		physics.Update(dt);
		renderer.Update(&physics, dt);
	}

	/*
		Focusing just on 

			root signatures, 
			root descriptors, 
			root constants, 
			descriptor tables, and 
			descriptor heaps, 
			
		the flow of rendering logic for an app should be similar to the following:

		- Create one or more root signature objects – one for every different 
		binding configuration an application needs.

		- Create shaders and pipeline state with the root signature objects they 
		will be used with.

		- Create one (or, if necessary, more) descriptor heaps that will contain 
		all the SRV, UAV, and CBV descriptors for each frame of rendering.

		- Initialize the descriptor heap(s) with descriptors where possible for 
		sets of descriptors that will be reused across many frames.

		- For each frame to be rendered:

			- For each command list:

				- Set the current root signature to use (and change if 
				needed during rendering – which is rarely required).

				- Update some root signature’s constants and/or 
				root signature descriptors for the new view (such as world/view projections).

				- For each item to draw:
					- Define any new descriptors in descriptor heaps as needed for per-object rendering.
					For shader-visible descriptor heaps, the app must make sure to use descriptor heap space 
					that isn’t already being referenced by rendering that could be in flight – for example, 
					linearly allocating space through the descriptor heap during rendering.

					- Update the root signature with pointers to the required regions of the descriptor heaps. 
					For example, one descriptor table might point to some static (unchanging) descriptors initialized 
					earlier, while another descriptor table might point to some dynamic descriptors configured for the 
					current rendering.

					- Update some root signature’s constants and/or root signature descriptors for per-item rendering.

					- Set the pipeline state for the item to draw (only if change needed), compatible 
					with the currently bound root signature.

					- Draw

				- Repeat (next item)

			- Repeat (next command list)

			- Strictly when the GPU has finished with any memory that will no longer be used, 
			it can be released. Descriptors' references to it do not need to be deleted if additional 
			rendering that uses those descriptors is not submitted. So, subsequent rendering can point 
			to other areas in descriptor heaps, or stale descriptors can be overwritten with valid descriptors 
			to reuse the descriptor heap space.

		- Repeat (next frame)
	*/

	void PopulateCommandList() 
	{
		// cmdList reset, barrier
		{
			commandAllocator->Reset();
			commandList->Reset(commandAllocator.Get(), nullptr);

			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				renderTargets[frameIndex].Get(),
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);

			commandList->ResourceBarrier(1, &barrier);

		}

		// swapchain setup
		{
			commandList->RSSetViewports(1, &viewPort);
			commandList->RSSetScissorRects(1, &scissorRect);

			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle{ rtvHeap->GetCPUHandle(frameIndex) };
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle{ dsvHeap->GetCPUHandle() };

			commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

			const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
			commandList->ClearRenderTargetView(rtvHeap->GetCPUHandle(frameIndex), clearColor, 0, nullptr);
			commandList->ClearDepthStencilView(dsvHeap->GetCPUHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}

		renderer.Draw(commandList.Get(), &physics);

		// close commandlist
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				renderTargets[frameIndex].Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PRESENT
			);

			commandList->ResourceBarrier(1, &barrier);

			DX_API("Failed to close command list")
				commandList->Close();
		}
	}

	void Render()  
	{
		PopulateCommandList();

		// Execute
		ID3D12CommandList* cLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(_countof(cLists), cLists);

		DX_API("Failed to present swap chain")
			swapChain->Present(0, 0);

		// Sync
		WaitForPreviousFrame();
	}

	// sync objects, command allocator, command list
	// renderer, physics startup
	void CreateResources() 
	{
		// create sync objects
		{
			DX_API("Failed to create fence")
				device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
			fenceValue = 1;

			fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			if (fenceEvent == NULL) {
				DX_API("Failed to create windows event") HRESULT_FROM_WIN32(GetLastError());
			}
		}

		// create work submission resources - command list & command allocator
		{
			DX_API("Failed to create command allocator")
				device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));

			DX_API("Failed to greate graphics command list")
				device->CreateCommandList(
					0, 
					D3D12_COMMAND_LIST_TYPE_DIRECT, 
					commandAllocator.Get(), 
					nullptr, 
					IID_PPV_ARGS(commandList.GetAddressOf())
				);
			
			commandList->Close();
			//WaitForPreviousFrame();

			DX_API("Failed to greate graphics command list")
				device->CreateCommandList(
					0,
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					commandAllocator.Get(),
					nullptr,
					IID_PPV_ARGS(commandList2.GetAddressOf())
				);

			commandList2->Close();
			WaitForPreviousFrame();
		}
		
		renderer.StartUp(device.Get());
		physics.StartUp(device.Get());
	}

	void ReleaseResources()  {
		commandList.Reset();
		fence.Reset();
		commandAllocator.Reset();
		commandQueue.Reset();
		swapChain.Reset();
		device.Reset();
	}

	// viewport, scissor, rtvs & its descriptor heap, depth stencil
	void CreateSwapChainResources()  
	{
		// viewport, scissor
		{
			DXGI_SWAP_CHAIN_DESC scDesc;
			swapChain->GetDesc(&scDesc);

			backBufferDepth = scDesc.BufferCount;

			viewPort.TopLeftX = 0;
			viewPort.TopLeftY = 0;
			viewPort.Width = (float)scDesc.BufferDesc.Width;
			viewPort.Height = (float)scDesc.BufferDesc.Height;
			viewPort.MinDepth = 0.0f;
			viewPort.MaxDepth = 1.0f;

			scissorRect.left = 0;
			scissorRect.top = 0;
			scissorRect.right = scDesc.BufferDesc.Width;
			scissorRect.bottom = scDesc.BufferDesc.Height;
		}

		// Create Render Target View Descriptor Heap, like a RenderTargetView** on the GPU. A set of pointers.
		rtvHeap = GG::DescriptorHeap::Create(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, backBufferDepth);

		// Create Render Target Views
		{
			renderTargets.resize(backBufferDepth);

			for (unsigned int i = 0; i < backBufferDepth; ++i) {
				DX_API("Failed to get swap chain buffer")
					swapChain->GetBuffer(i, IID_PPV_ARGS(renderTargets[i].GetAddressOf()));

				device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, rtvHeap->GetCPUHandle(i));
			}

			frameIndex = swapChain->GetCurrentBackBufferIndex();
		}

		// Depth stencil
		{
			dsvHeap = GG::DescriptorHeap::Create(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

			D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
			depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
			depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
			depthOptimizedClearValue.DepthStencil.Stencil = 0;

			CD3DX12_HEAP_PROPERTIES heapProp{ D3D12_HEAP_TYPE_DEFAULT };
			CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, scissorRect.right, scissorRect.bottom, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

			DX_API("Failed to create Depth Stencil buffer")
				device->CreateCommittedResource(
					&heapProp,
					D3D12_HEAP_FLAG_NONE,
					&resourceDesc,
					D3D12_RESOURCE_STATE_DEPTH_WRITE,
					&depthOptimizedClearValue,
					IID_PPV_ARGS(depthStencilBuffer.GetAddressOf())
				);

			depthStencilBuffer->SetName(L"Depth Stencil Buffer");

			D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
			depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
			depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

			device->CreateDepthStencilView(depthStencilBuffer.Get(), &depthStencilDesc, dsvHeap->GetCPUHandle());
		}
	}

	void ReleaseSwapChainResources() 
	{
		depthStencilBuffer.Reset();

		for (com_ptr<ID3D12Resource>& i : renderTargets) {
			i.Reset();
		}
		renderTargets.clear();
	}

	// shaders, texture, geometry, gpso, cbv
	void LoadAssets()  
	{
		// spere object
		{
			const std::string id{ "sphere" };
			renderer.AddShadedMesh(device.Get(), id, "sphere.fbx", "checkered.png");
			physics.AddRigidBody(id, PxTransform{ 0,15,0 }, PxSphereGeometry(2.5f));
		}

		// another spere object
		{
			const std::string id{ "sphere2" };
			renderer.AddShadedMesh(device.Get(), id, "sphere.fbx", "checkered.png");
			physics.AddRigidBody(id, PxTransform{ 5,25,0 }, PxSphereGeometry(2.5f));
		}

		// plane object
		{
			const std::string id{ "plane" };
			renderer.AddShadedMesh(device.Get(), id, "plane.obj", "floor.png");
			physics.AddRigidBody(id, PxTransform{ 0,0,0 }, PxBoxGeometry(PxVec3{ 20, 1, 20 }), true);
		}

		// an array of cubes
		for (int i = -18; i < 18; i += 5) {
			for (int j = -18; j < 18; j += 5) {
				for (int k = 5; k < 15; k += 5) {
					const std::string id{ "cube_" + std::to_string(i) + '-' + std::to_string(j) + '-' + std::to_string(k) };
					renderer.AddShadedMesh(device.Get(), id, "box.obj", "giraffe.jpg");
					physics.AddRigidBody(id, PxTransform{ (float)i, (float)k, (float)j }, PxBoxGeometry(PxVec3{ 1, 1, 1 }));
				}
			}
		}

		// lights
		{
			const std::string id1{ "light1" };
			renderer.AddLight(id1, Float3{ 20,20,20 });
			physics.AddRigidBody(id1, PxTransform{ 10,10,10 }, PxSphereGeometry(1.f), false);

			const std::string id2{ "light2" };
			renderer.AddLight(id2, Float3{ 20,20,0 });
			physics.AddRigidBody(id2, PxTransform{ 10,10,-10 }, PxSphereGeometry(1.f), true);

			const std::string id3{ "light3" };
			renderer.AddLight(id3, Float3{ 20,0,0 });
			physics.AddRigidBody(id3, PxTransform{ -10,10,10 }, PxSphereGeometry(1.f), true);

			const std::string id4{ "light4" };
			renderer.AddLight(id4, Float3{ 0,0,20 });
			physics.AddRigidBody(id4, PxTransform{ -10,10,-10 }, PxSphereGeometry(1.f), true);
		}

		// upload textures
		{
			DX_API("Failed to reset command allocator (UploadResources)")
				commandAllocator->Reset();
			DX_API("Failed to reset command list (UploadResources)")
				commandList->Reset(commandAllocator.Get(), nullptr);

			renderer.UploadTextures(commandList.Get());

			DX_API("Failed to close command list (UploadResources)")
				commandList->Close();

			ID3D12CommandList* commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

			WaitForPreviousFrame();
		}

	}

	void ReleaseAssets() { }

	void Resize(int width, int height)  {
		WaitForPreviousFrame();
		ReleaseSwapChainResources();
		DX_API("Failed to resize swap chain")
			swapChain->ResizeBuffers(backBufferDepth, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
		CreateSwapChainResources();
	}

	void ProcessMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) 
	{ 
		renderer.ProcessMessage(hWnd, uMsg, wParam, lParam); 
		if (uMsg == WM_KEYDOWN && wParam == VK_SPACE)
		{
			int seed = std::chrono::system_clock::now().time_since_epoch().count();
			const std::string id{ "light" + std::to_string(seed) };

			renderer.AddLight(id, Float3{ 10,10,0 });
			Float3 pos = renderer.camera->GetEyePosition();
			physics.AddRigidBody(id, PxTransform{ ~pos }, PxSphereGeometry(1.f));
			Float3 ahead = renderer.camera->GetAhead() * 1'000.f;
			physics.AddForce(id, ahead);
		}
	}

	void Destroy()  {
		WaitForPreviousFrame();
		ReleaseSwapChainResources();
		ReleaseResources();
		ReleaseAssets();
	}

	void SetCommandQueue(com_ptr<ID3D12CommandQueue> cQueue) { commandQueue = cQueue; }

	void SetDevice(com_ptr<ID3D12Device> dev) { device = dev; }

	void SetSwapChain(com_ptr<IDXGISwapChain3> sChain) { swapChain = sChain; }

};