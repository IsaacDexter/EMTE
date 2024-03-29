#include "Renderer.h"
#include "Window.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "misc/cpp/imgui_stdlib.h"
#include <chrono>
#include "Camera.h"
#include "RtvHeap.h"
#include "Portal.h"
#include "RenderTexture.h"

#include "SceneObject.h"
#include "Resource.h"
#include "ShaderResourceView.h"
#include "ConstantBufferView.h"
#include "Primitive.h"
#include "Engine.h"

using namespace Microsoft::WRL;
using namespace DirectX;

#define _GUI

Renderer::Renderer(std::shared_ptr<Engine>& scene)
	: m_framebuffers{}
	, m_frameIndex()
	, g_scene(scene)
{

}

void Renderer::Initialize(HWND hWnd, const UINT width, const UINT height)
{
	m_viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	// Initialize Pipeline
	// Initialize Assets
	InitializePipeline(hWnd, width, height);
	InitializeAssets(width, height);
	InitializeGUI(hWnd);

}

void Renderer::Update()
{



}

void Renderer::Render()
{/*
	- Populate command list
		- Reset command list allocator
			- Re-use memory associated with command allocator
		- Reset command list
		- Set graphics root signature
		   - To use with current command list
		- Set viewport and scissor rect
		- Set *resource barrier*s
			- Indicate back buffer to be used as render target
		- Record commands into command list
		- Indicate back buffer will be used to present after command list execution
			- Set resource barrier
		- Close command list
	- Execute command list
	- Present frame
	- Wait for GPU to finish
		- Wait on fence
	*/

	UpdateGUI(g_scene->m_sceneObjects, g_scene->m_selectedObject);

	// Put the command list into an array (of one) for execution on the queue
	// TODO : Change this to take advantage of CommandQueue
	if (m_cbvSrvUavHeap->Load(m_commandQueue->GetD3D12CommandQueue()))
		m_commandQueue->Flush();


	// Record portal commands
	for (auto portal : g_scene->m_portals)
	{
		{
			auto commandList = m_commandQueue->GetCommandList(m_pipelineState.Get());
			commandList->SetName(L"Portal Command List");
			PrepareCommandList(commandList.Get());
			portal->DrawTexture(commandList.Get());
			m_commandQueue->ExecuteCommandList(commandList.Get());
		}
		{
			// proceed to the next frame
			// insert a signal into the queue, to stall the cpu with
			auto frameFenceValue = m_commandQueue->Signal();
			// stall the CPU until any writable resources (i.e the back buffer's RTV) are finished being used
			m_commandQueue->WaitForFenceValue(frameFenceValue);
		}
	}
	

	// Draw the remainder of the scene once the portal textures have been updated
	{

		auto commandList = m_commandQueue->GetCommandList(m_pipelineState.Get());
		commandList->SetName(L"Backbuffer Command List");
		auto backBuffer = m_framebuffers[m_frameIndex].first;
		auto backBufferCpuDescriptorHandle = m_framebuffers[m_frameIndex].second;
		PrepareCommandList(commandList.Get());
		{
			// Indicate that the back buffer will be used as a render target.

			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			commandList->ResourceBarrier(1, &barrier);

			// Set CPU handles for Render Target Views (RTVs) and Depth Stencil Views (DSVs) heaps
			CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
			commandList->OMSetRenderTargets(1, &backBufferCpuDescriptorHandle, FALSE, &dsvHandle);

			// Record commands.
			// Clear the RTVs and DSVs
			const float clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
			commandList->ClearRenderTargetView(backBufferCpuDescriptorHandle, clearColor, 0, nullptr);
			commandList->ClearDepthStencilView(
				dsvHandle,  // Aforementioned handle to DSV heap
				D3D12_CLEAR_FLAG_DEPTH, // Clear just the depth, not the stencil
				1.0f,   // Value to clear the depth to  
				0,  // Value to clear the stencil view
				0, nullptr  // Clear the whole view. Set these to only clear specific rects.
			);
			// Update Model View Projection (MVP) Matrix according to camera position

			// Draw objects, including the portals scene objects.
			for (auto object : g_scene->m_sceneObjects)
			{
				object->UpdateConstantBuffer(g_scene->m_camera->GetView(), g_scene->m_camera->GetProj());
				object->Draw(commandList.Get());
			}

			RenderGUI(commandList.Get());

			// Indicate that the back buffer will now be used to present.
			barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			commandList->ResourceBarrier(1, &barrier);
		}


		m_commandQueue->ExecuteCommandList(commandList.Get());
	}

	// Present the frame
	ThrowIfFailed(m_swapChain->Present(1, 0), "Failed to present frame.\n");


	// update the index of the back buffer, which may not be sequential
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// proceed to the next frame
	// insert a signal into the queue, to stall the cpu with
	auto frameFenceValue = m_commandQueue->Signal();
	// stall the CPU until any writable resources (i.e the back buffer's RTV) are finished being used
	m_commandQueue->WaitForFenceValue(frameFenceValue);
}

void Renderer::Destroy()
{
	// Wait for the GPU to be done with all resources.
	m_commandQueue->Flush();

	DestroyGUI();
}

void Renderer::Resize(UINT width, UINT height)
{

	// flush the GPU queue to ensure the buffers aren't currently in use
	// I think this is the problem
	m_commandQueue->Flush();

	for (int i = 0; i < m_frameCount; ++i)
	{
		// relesase references to renderTargets before resizing
		m_framebuffers[i].first.Reset();
	}
	//m_renderTextureRtv->Reset();



	// query the swap chain description so the same colour format and flags can be used to recreate it
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	ThrowIfFailed(m_swapChain->GetDesc(&swapChainDesc));
	// recreate the swap chain with the new size from the same description
	ThrowIfFailed(m_swapChain->ResizeBuffers(
		m_frameCount,
		width,
		height,
		swapChainDesc.BufferDesc.Format,
		swapChainDesc.Flags
	));
	// update the back buffer index known by the application, as it may not be the same as the resized version
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// as the swap chain buffers have been resized, update their descriptors too
	UpdateFramebuffers();

	UpdateDepthStencilView(m_dsv, m_dsvHeap, width, height);
	// Update the size of the scissor rect
	m_scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	// Update the viewport also    
	m_viewport.Width = float(width);
	m_viewport.Height = float(height);
}

std::shared_ptr<Resource> Renderer::CreateTexture(const wchar_t* path, std::string name)
{
	return m_cbvSrvUavHeap->CreateShaderResourceView(m_device.Get(), m_pipelineState.Get(), path, name);
}

std::shared_ptr<Resource> Renderer::CreateTexture(std::string name)
{
	return m_cbvSrvUavHeap->ReserveShaderResourceView(name);
}

std::shared_ptr<RenderTexture> Renderer::CreateRenderTexture(std::string name)
{
	auto texture = CreateTexture(name);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	m_rtvHeap->GetFreeHandle(rtvHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
	// TODO : Acquire a new DSV handle
	dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
	auto renderTexture = std::make_shared<RenderTexture>(texture->cpuDescriptorHandle, texture->gpuDescriptorHandle, texture->rootParameterIndex, rtvHandle, dsvHandle);
	renderTexture->Initialize(m_device.Get());
	return renderTexture;
}

std::shared_ptr<Primitive> Renderer::CreateModel(const wchar_t* path, std::string name)
{
	return m_cbvSrvUavHeap->CreateModel(m_device.Get(), m_pipelineState.Get(), m_rootSignature.Get(), path, name);
}

std::shared_ptr<ConstantBufferView> Renderer::CreateConstantBuffer()
{
	return m_cbvSrvUavHeap->CreateConstantBufferView(m_device.Get());
}

void Renderer::UnloadResource(D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvUavCpuDescriptorHandle, D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavGpuDescriptorHandle)
{
	m_cbvSrvUavHeap->Free(cbvSrvUavCpuDescriptorHandle, cbvSrvUavGpuDescriptorHandle);
}

void Renderer::UnloadResource(D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvUavCpuDescriptorHandle, D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavGpuDescriptorHandle, D3D12_CPU_DESCRIPTOR_HANDLE rtvCpuDescriptorHandle)
{
	m_cbvSrvUavHeap->Free(cbvSrvUavCpuDescriptorHandle, cbvSrvUavGpuDescriptorHandle);
	m_rtvHeap->Free(rtvCpuDescriptorHandle);
	// TODO : Free DSV handle
}


/*
- Enable debug layer
- Create device
- Create command queue
- Create swap chain
- Create RTV descriptor heap
- Create frame resources
- Create command allocator
*/
void Renderer::InitializePipeline(HWND hWnd, const UINT width, const UINT height)
{
	#if defined (_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();


		}
	}
	#endif
	// create the device
	auto hardwareAdapter = GetAdapter(m_useWarpDevice);
	m_device = CreateDevice(hardwareAdapter);
	//CreateDevice();

	// create the direct command queue
	m_commandQueue = std::make_unique<CommandQueue>(m_device, D3D12_COMMAND_LIST_TYPE_DIRECT);

	m_swapChain = CreateSwapChain(hWnd, m_commandQueue->GetD3D12CommandQueue(), width, height, m_frameCount);

	// Create descriptor heaps
	{
		// Describe and create the Depth Stencil View (DSV) descriptor heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = 1; // 1 depth stencil view,
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;  // in a depth stencil view heap,
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;    // invisible to the shaders.
			ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
			m_dsvHeap->SetName(L"m_dsvHeap");
		}

		// Describe and create sampler descriptor heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
			samplerHeapDesc.NumDescriptors = 1;
			samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;  // Sampler type
			samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // Let the samplers be accessed by shaders
			ThrowIfFailed(m_device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap)));
			m_samplerHeap->SetName(L"m_samplerHeap");
		}


	}

	UpdateDepthStencilView(m_dsv, m_dsvHeap, width, height);


	// Create the swapChain
	{

		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = 8;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;  //RTV type
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;    // This heap needs no binding to pipeline
		m_rtvHeap = std::make_unique<DescriptorHeap>(m_device.Get(), rtvHeapDesc);
	}

	// Create the swapChain
	CreateFramebuffers();
}


Microsoft::WRL::ComPtr<ID3D12Debug> Renderer::EnableDebugLayer()
{
	// Enable the D3D12 debug layer.
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
	}
	return debugController;
}

Microsoft::WRL::ComPtr<IDXGIAdapter4> Renderer::GetAdapter(bool useWarpDevice)
{

	// a DXGI factory must be created before querying available adaptors
	ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;
#if defined(_DEBUG)
	//enables errors to be caught during device creation and while querying adapters
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif //DEBUG

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)), "Couldn't create factory.\n");

	ComPtr<IDXGIAdapter1> dxgiAdapter1;
	ComPtr<IDXGIAdapter4> dxgiAdapter4;

	if (useWarpDevice)
	{
		// if a warp device is to be used, EnumWarpAdapter will directly create the warp adapter
		ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)), "Couldn't get adapter.\n");
		// cannot static_cast COM, so .As casts com to correct type
		ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4), "Couldn't get adapter.\n");
	}
	else
	{
		// when not using warp, DXGI factory querys hardware adaptors
		SIZE_T maxDedicatedVideoMemory = 0;
		// enummarate available gpu adapters in the system and iterate through
		for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			// check to see if the adapter can create a D3D12 device without actually  creating it. The adapter with the largest dedicated video memory is favored
			// DXGI_ADAPTER_FLAG_SOFTWARE avoids software rasterizer as we're not using WARP
			if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
				SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), // create a null ddevice if its a compatible DX12 adapter, if it returns S_OK, it is a compatible DX12 header
					D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
				dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
			{
				maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4), "Couldn't get adapter.\n");  // cast the adapter and return it
			}
		}
	}

	return dxgiAdapter4;
}

void Renderer::GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter)
{
	*ppAdapter = nullptr;
	for (UINT adapterIndex = 0; ; ++adapterIndex)
	{
		IDXGIAdapter1* pAdapter = nullptr;
		if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &pAdapter))
		{
			// No more adapters to enumerate.
			break;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			*ppAdapter = pAdapter;
			return;
		}
		pAdapter->Release();
	}

}

Microsoft::WRL::ComPtr<ID3D12Device4> Renderer::CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter)
{
	ComPtr<ID3D12Device4> d3d12Device3;
	ThrowIfFailed(D3D12CreateDevice(
		adapter.Get(),  //pointer to video adapter to use when creating device
		D3D_FEATURE_LEVEL_11_0, //minimum feature level for successful device creation
		IID_PPV_ARGS(&d3d12Device3)
	), "Couldn't create Device.\n");  //store device in this argument

#if defined(_DEBUG)

	//used to enable break points based on severity of message and filter messages' creation
	ComPtr<ID3D12InfoQueue> pInfoQueue;
	if (SUCCEEDED(d3d12Device3.As(&pInfoQueue)))    //query infoqueue inteface from comptr.as
	{
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);    //sets a message severity level to break on with debugger
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

		// suppress whole categories of messages
		//D3D12_MESSAGE_CATEGORY Categories[] = {};

		// suppress messages based on their severity level
		D3D12_MESSAGE_SEVERITY Severities[] =
		{
			D3D12_MESSAGE_SEVERITY_INFO
		};

		// suppress individual messages by their ID
		D3D12_MESSAGE_ID DenyIds[] = {
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // warning when render target is cleared using a clear color
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
			D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED
		};

		//info queue filter is defined and filter is pushed onto info queue
		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories = _countof(Categories);
		//NewFilter.DenyList.pCategoryList = Categories;
		NewFilter.DenyList.NumSeverities = _countof(Severities);
		NewFilter.DenyList.pSeverityList = Severities;
		NewFilter.DenyList.NumIDs = _countof(DenyIds);
		NewFilter.DenyList.pIDList = DenyIds;

		ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
	}

#endif // DEBUG

	return d3d12Device3;
}

void Renderer::CreateDevice()
{
	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> Renderer::CreateCommandQueue(Microsoft::WRL::ComPtr<ID3D12Device> device, const D3D12_COMMAND_LIST_TYPE type)
{
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = type;

	ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)), "Couldn't ceate command queue.");

	return commandQueue;
}

Microsoft::WRL::ComPtr<IDXGISwapChain4> Renderer::CreateSwapChain(HWND hWnd, Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount)
{
	ComPtr<IDXGISwapChain4> dxgiSwapChain4;
	ComPtr<IDXGIFactory4> dxgiFactory4;
	UINT createFactoryFlags = 0;

#if defined (_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	// Create the dxgi factory
	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)), "Couldn't create swap chain.\n");

	// Create descriptor to describe creation of swap chain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = width;    // resolution width, 0 to obtain width from output window
	swapChainDesc.Height = height;  // resolution height, 0 to obtain width from output window
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // swap chain format, A four-component, 32-bit typeless format that supports 8 bits per channel including alpha.
	swapChainDesc.Stereo = FALSE;   // if swap chain back buffer is stereo, if specified, so must be a flip-model swap chain
	swapChainDesc.SampleDesc = { 1, 0 };    // sample desc that describes multisampling parameters, must be 1, 0 for flip model
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;    // describes surface usage and cpu access to back buffer. Shader input or render target output
	swapChainDesc.BufferCount = bufferCount;    // number of buffers in swap chain, must be 2 for flip presentation 
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;   // idenifies resize behaviour of back buffer is not equal to output: stretch, none, aspect ratio stretch
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;   // the presentation model and options for handeling buffer after present: sequential, discard, flip sequential and flip discard. Use flip for better performance.
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;  // transparency behaviour: unspecified, premultiplied, straight or ignored.
	swapChainDesc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0; // Allow tearing if tearing is supported (for variable sync)

	// Create the swap chain from the descriptor
	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		commandQueue.Get(),
		hWnd,
		&swapChainDesc,
		nullptr,    // fullscreen descriptor, set to nullptr for a windowed swap chain
		nullptr,    // pointer to idxgi output to restrict content to, set to nullptr
		&swapChain1
	), "Couldn't create swap chain.\n");

	// Disable alt+enter fullscreen, so borderless window fullscreen can be used
	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER), "Couldn't create swap chain.\n");

	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4), "Couldn't create swap chain.\n");

	return dxgiSwapChain4;
}


Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Renderer::CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device4> device, const D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, UINT& descriptorSize)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	// Create the descriptor heap descriptor used to describe its creation
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;   // number of descriptors in the heap
	desc.Type = type;   //types of descriptors in the heap: CRV, SRV, UAV; SAMPLER; RTV; DSV
	// additional flags for shader visible, indicating it can be bound on command list for reference by shaders, only for CBV, SRV, UAV; and samplers
	// additional nodemask for multi-adapter nodes

	// Create the descriptor heap from the descriptor
	ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

	descriptorSize = m_device->GetDescriptorHandleIncrementSize(type);

	return descriptorHeap;
}

void Renderer::CreateFramebuffers()
{
	// For each framebuffer
	for (UINT n = 0; n < m_framebuffers.size(); n++)
	{
		// Get a free handle pair on the RTV heap, then create an RTV at it and store the RTV and its handle.
		m_rtvHeap->GetFreeHandle(m_framebuffers[n].second);

		ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_framebuffers[n].first)));
		m_device->CreateRenderTargetView(m_framebuffers[n].first.Get(), nullptr, m_framebuffers[n].second);
	}
}

void Renderer::UpdateFramebuffers()
{
	// For each framebuffer
	for (UINT n = 0; n < m_framebuffers.size(); n++)
	{
		ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_framebuffers[n].first)));
		m_device->CreateRenderTargetView(m_framebuffers[n].first.Get(), nullptr, m_framebuffers[n].second);
	}
}

bool Renderer::CheckTearingSupport()
{

	bool allowTearing = false;

	// Create a DXGI 1.4 factory interface and query for the 1.5 interface
	// as DXGI 1.5 may not support graphics debugging tools
	ComPtr<IDXGIFactory4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
	{
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory4.As(&factory5)))
		{
			// Query if tearing is supported
			if (FAILED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing, sizeof(allowTearing))))
			{
				allowTearing = FALSE;
			}
		}
	}

	return allowTearing;
}

void Renderer::InitializeAssets(const UINT width, const UINT height)
{
	/*
	- Create empty *root signature*
	- Compile shaders
	- Create vertex input layout
	- Create *pipeline state object*
		- Create description
		- Create object
	- Create command list
	- Close command list
	- Create and load vertex buffers
	- Create vertex buffer views
	- Create *fence*
	- Create event handle
	- Wait for GPU to finish
	- Wait on fence
	*/

	// Create empty root signature
	m_rootSignature = CreateRootSignature();

	// Load compiled shaders
	// Load vertex shader from precompiled shader files
	ComPtr<ID3DBlob> vertexShaderBlob;
	ThrowIfFailed(D3DReadFileToBlob(L"VertexShader.cso", &vertexShaderBlob), "Failed to load vertex shader.\n");
	// Load pixel shader from precompiled shader files
	ComPtr<ID3DBlob> pixelShaderBlob;
	ThrowIfFailed(D3DReadFileToBlob(L"PixelShader.cso", &pixelShaderBlob), "Failed to load pixel shader.\n");

	// Create pipeline state object
	m_pipelineState = CreatePipelineStateObject(vertexShaderBlob.Get(), pixelShaderBlob.Get());

	// Create CBV SRV UAV joint heap
	{
		// Describe and create a Shader Resource View (SRV) heap for the texture.
		// This heap also contains the Constant Buffer Views. These are in the same heap because
		// CBVs, SRVs, and UAVs can be combined into a single descriptor table.
		// This means the descriptor heap needn't be changed in the command list, which is slow and rarely used.
		// https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-flow-of-control
		D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
		cbvSrvUavHeapDesc.NumDescriptors = 1024;
		cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;  // SRV type
		cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // Allow this heap to be bound to the pipeline

		m_cbvSrvUavHeap = std::make_unique<CbvSrvUavHeap>(m_device.Get(), cbvSrvUavHeapDesc, m_pipelineState.Get());
	}

	CreateSampler();

	CreateSyncObjects();
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> Renderer::CreateRootSignature()
{
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

	// Check the highest version of root signature that can be used
	D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

	// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
	featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
	{
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
	}

	// Describe descriptor tables to root signature
	// Describe range of descriptor heap encompassed by descriptor table
	CD3DX12_DESCRIPTOR_RANGE1 ranges[3] = {};
	// CBV range
	ranges[DescriptorHeap::RootParameterIndices::CBV].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_CBV,    // type of resources within the range
		1,  // number of descriptors in the range
		0,  // base shader register in the range
		0,  // register space, typically 0
		D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC // Descriptors and data are static and will not change (as they're loaded textures)
	);
	// SRV range
	ranges[DescriptorHeap::RootParameterIndices::SRV].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,    // type of resources within the range
		1,  // number of descriptors in the range
		0,  // base shader register in the range
		0  // register space, typically 0
		//D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC // Descriptors and data are static and will not change (as they're loaded textures)
	);
	// Sampler range
	ranges[DescriptorHeap::RootParameterIndices::Sampler].Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,    // Samplers lie within this range
		1,  // Just one of them
		0   // Bound to the base register
	);


	// Describe layout of descriptor tables to the root signature based on ranges
	CD3DX12_ROOT_PARAMETER1 rootParameters[3] = {};
	// CBV root parameters
	rootParameters[DescriptorHeap::RootParameterIndices::CBV].InitAsDescriptorTable(
		1,  // number of ranges in this table
		&ranges[DescriptorHeap::RootParameterIndices::CBV], // Descriptor range specified already 
		D3D12_SHADER_VISIBILITY_VERTEX   // Specify that the vertex shader can access these textures
	);
	// SRV root parameters
	rootParameters[DescriptorHeap::RootParameterIndices::SRV].InitAsDescriptorTable(
		1,  // number of ranges in this table
		&ranges[DescriptorHeap::RootParameterIndices::SRV], // Descriptor range specified already 
		D3D12_SHADER_VISIBILITY_PIXEL   // Specify that the pixel shader can access these textures
	);
	// Describe sampler descriptor table
	rootParameters[DescriptorHeap::RootParameterIndices::Sampler].InitAsDescriptorTable(
		1,  // Number of ranges in this table
		&ranges[DescriptorHeap::RootParameterIndices::Sampler], // Said descriptor ranges
		D3D12_SHADER_VISIBILITY_PIXEL   // Only pixel shader need access sampler
	);


	// Create root signature descriptor 
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	// Use version 1 of root signature layout
	rootSignatureDesc.Init_1_1(
		_countof(rootParameters), rootParameters, // pass layout of descriptor tables
		0, nullptr, // We are using a sampler heap rather than static samplers
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT    // Use input assembler, i.e. input layout and vertex buffer
	);


	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	// Serialize root signature so it can be created based on description
	ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
		&rootSignatureDesc, // Description of root signature
		featureData.HighestVersion, // Use the highest version of root signature we can
		&signature, // Memory block to acess the serialized root signature (i.e. to create it)
		&error  // Memory block to access serializer error messages
	), "Couldn't serialize root signature.\n");

	// Create root signature from serialized one using the device
	ThrowIfFailed(m_device->CreateRootSignature(
		0,  // Set to 0 when using single GPU, for Multi-adapter systems
		signature->GetBufferPointer(),  // Pointer to the data of the blob holding the serialized root signature
		signature->GetBufferSize(), // The length of the serialized root signature
		IID_PPV_ARGS(&rootSignature) // GUID of the root signature interface
	), "Failed to create root signature.\n");

	// Set the debug name for this object
	rootSignature->SetName(L"m_rootSignature");


	return rootSignature;
}

void Renderer::CreateSyncObjects()
{
	//// Create synchronization objects

	//ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	//m_fenceValues[m_frameIndex]++;

	//// Create an event handle to use for frame synchronization.
	//m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	//if (m_fenceEvent == nullptr)
	//{
	//	ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	//}

	//// Wait for the command list to execute; we are reusing the same command 
	//// list in our main loop but for now, we just want to wait for setup to 
	//// complete before continuing.
	//WaitForGpu();

}

void Renderer::UpdateDepthStencilView(Microsoft::WRL::ComPtr<ID3D12Resource>& dsv, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& dsvHeap, UINT width, UINT height)
{
	// Describe the depth stencil view
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; // Depth Stencil View format
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;  // Access DSV as a single texture 2d resource
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;    // Indicate the DSV isn't read-only

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	// Format the clear value as a DS value type
	depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	// Specify a depth stencil value to clear
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	D3D12_RESOURCE_DESC dsDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_D32_FLOAT,  // Use established DS format
		width,  // Depth Stencil to encompass the whole screen (ensure to resize it alongside the screen.)
		height,
		1,  // Array size of 1
		0,  // no MIP levels
		1, 0,   // Sample count and quality (no Anti-Aliasing)
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL // Allow a DSV to be created for the resource and allow it to handle write/read transitions
	);

	// Create the DSV in an implicit heap that encompasses it
	{
		// Upload with a default heap
		auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(m_device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&dsDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&dsv)
		));
	}

	// Give it a debug name
	dsv->SetName(L"m_depthStencilView");

	m_device->CreateDepthStencilView(dsv.Get(), &dsvDesc, dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Renderer::CreateSampler()
{
	// Describe and create a sampler.
	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	m_device->CreateSampler(&samplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> Renderer::CreatePipelineStateObject(ID3DBlob* pVertexShaderBlob, ID3DBlob* pPixelShaderBlob)
{
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;



	// Define vertex input layout, which describes a single element for the Input Assembler
	// Define vertex input layout, which describes a single element for the Input Assembler
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{
			"POSITION", // HLSL semnantic name associated with this element
			0,  // sematic index for the element. Only needed when there are multiple elements with the same semantic, i.e. matrices' components
			DXGI_FORMAT_R32G32B32_FLOAT,    // format of the element data
			0,  // identifier for the input slot for using multiple vertex buffers (0-15)
			D3D12_APPEND_ALIGNED_ELEMENT,   // offset in bytes between each elements, defines current element directly after previous one
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, // input data class for input slot: per-vertex/per-instance data
			0   //number of instances to draw
		},
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		//{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// Create the input layout desc, which defines the organization of input elements to the pipeline state desc
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc;
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);



	// Describe depth stencil state for pipeline state object, to be default and disabled
	CD3DX12_DEPTH_STENCIL_DESC1 dsDesc(D3D12_DEFAULT);
	dsDesc.DepthEnable = true;
	dsDesc.StencilEnable = false;

	// Describe rasterizer state for pipeline with default values
	CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);

	// Describe blend state with default values
	CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);

	// define render target count and render target formats

	CD3DX12_RT_FORMAT_ARRAY rtvFormats;
	rtvFormats.NumRenderTargets = 3;    // define render target count
	std::fill(std::begin(rtvFormats.RTFormats), std::end(rtvFormats.RTFormats), DXGI_FORMAT_UNKNOWN);   // set to unknown format for unused render targets
	rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;   // define render target format for the first (real) render target
	rtvFormats.RTFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;   // define render target format for the second (portal) render target
	rtvFormats.RTFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;   // define render target format for the third (portal) render target

	// Default sample mode without anti aliasing
	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = 1;
	sampleDesc.Quality = 0;

	// Create Graphics Pipeline State Object, which maintains shaders
	// Describe PSO with Pipeline State Stream, to create the desc with more flexibility
	CD3DX12_PIPELINE_STATE_STREAM pss;
	pss.InputLayout = inputLayoutDesc;  // Input elements' layout
	pss.pRootSignature = m_rootSignature.Get(); // Pointer to root signature
	pss.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderBlob);   // Create shader bytecode describing the vertex shader as it appears in memory
	pss.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderBlob);    // Create shader bytecode describing the pixel shader as it appears in memory
	pss.RasterizerState = rasterizerDesc;   // describe rasterizer
	pss.BlendState = blendDesc; // Describe blend state
	pss.DepthStencilState = dsDesc;   // Describe depth-stencil state
	pss.SampleMask = UINT_MAX;  // Define sample mask for blend state, max signifies point sampling 
	pss.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // Set the primitive topology to use triangles to draw
	pss.RTVFormats = rtvFormats;    // Render target count & formats
	pss.SampleDesc = sampleDesc;
	pss.DSVFormat = DXGI_FORMAT_D32_FLOAT;  // Depth Stencil View format

	// Wrap Pipeline State Stream into a desc
	D3D12_PIPELINE_STATE_STREAM_DESC pssDesc = {};
	pssDesc.SizeInBytes = sizeof(pss);
	pssDesc.pPipelineStateSubobjectStream = &pss;

	// Pass descriptor into pipeline state to create PSO object
	ThrowIfFailed(m_device->CreatePipelineState(&pssDesc, IID_PPV_ARGS(&pipelineState)), "Failed to create pipeline state object.\n");
	// Set name for debugging
	pipelineState->SetName(L"m_pipelineState");


	return pipelineState;
}

void Renderer::InitializeGUI(HWND hWnd)
{
#if defined (_GUI)

	// Set up Dear ImGui

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hWnd);
	auto srv = m_cbvSrvUavHeap->ReserveShaderResourceView("GUI");
	ImGui_ImplDX12_Init(m_device.Get(), m_frameCount, DXGI_FORMAT_R8G8B8A8_UNORM,
		m_cbvSrvUavHeap->GetDescriptorHeap(),
		// You'll need to designate a descriptor from your descriptor heap for Dear ImGui to use internally for its font texture's SRV
		srv->cpuDescriptorHandle,
		srv->gpuDescriptorHandle
	);
#endif
}

void Renderer::UpdateGUI(std::set<std::shared_ptr<SceneObject>>& objects, std::shared_ptr<SceneObject>& selectedObject)
{
#if defined (_GUI)
	// Start new Dear ImGui frame

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Scene Graph
	ShowSceneGraph(objects, selectedObject);

	// Create properties editor
	ShowProperties(selectedObject);


#endif
}

void Renderer::ShowSceneGraph(std::set<std::shared_ptr<SceneObject>>& objects, std::shared_ptr<SceneObject>& selectedObject)
{
	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(150, 500), ImGuiCond_::ImGuiCond_Once);
	if (!ImGui::Begin("Scene Graph", &open))
	{
		ImGui::End();
		return;
	}
	// Create the list of items in the world
	if (ImGui::BeginListBox("##Objects list", ImVec2(-FLT_MIN, -FLT_MIN)))
	{
		for (auto object : objects)
		{
			const bool is_selected = (selectedObject == object);
			if (ImGui::Selectable(object->GetName().c_str(), is_selected))
				selectedObject = object;

			// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		if (ImGui::Button("New Scene Object"))
		{
			std::shared_ptr<SceneObject> object;
			auto cbv = CreateConstantBuffer();
			g_scene->m_constantBuffers.push_back(cbv);

			// TODO : Reimplement duplicate a scene object
			//if (selectedObject)
			//{
			//	
			//	object = std::make_shared<SceneObject>(selectedObject->GetModel(), selectedObject->GetTexture(), cbv, "new " + selectedObject->GetName());
			//}
			//else
			{
				object = std::make_shared<SceneObject>(nullptr, nullptr, cbv, "new ");
			}
			objects.emplace(object);
			selectedObject = object;
		}
		if (ImGui::Button("New Portal"))
		{
			// TODO : Fix coupling, add duplicating portals


			std::shared_ptr<Portal> portal;
			auto cbv = CreateConstantBuffer();
			g_scene->m_constantBuffers.push_back(cbv);

			auto renderTexture = CreateRenderTexture("Portal");
			g_scene->m_renderTextures.push_back(renderTexture);

			portal = std::make_shared<Portal>(nullptr, renderTexture, cbv, "new Portal", g_scene->m_sceneObjects, g_scene->m_camera);
			portal->SetScale(XMFLOAT3(1.0f, 1.0f, 0.0f));
			g_scene->m_portals.insert(portal);
			g_scene->m_sceneObjects.insert(portal);
		}

		ImGui::EndListBox();
	}

	ImGui::End();
}

void Renderer::ShowProperties(std::shared_ptr<SceneObject>& selectedObject)
{
	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(250, 500), ImGuiCond_::ImGuiCond_Once);
	if (!ImGui::Begin("Properties Editor", &open))
	{
		ImGui::End();
		return;
	}
	if (selectedObject)
	{
		// Portal properties, if applicable
		Portal* selectedPortal = dynamic_cast<Portal*>(selectedObject.get());

		// Create the list of items in the world
		/*if (ImGui::TreeNode(selectedObject->GetName().c_str()))
		{*/
		// Name:
		{
			ImGui::Text("Name:");
			std::string name = selectedObject->GetName();
			if (ImGui::InputText("##Name", &name) && !name.empty())
			{
				selectedObject->SetName(name);
			}

		}

		// Position
		{
			ImGui::Text("Position:");

			float position[3] = { selectedObject->GetPosition().x, selectedObject->GetPosition().y, selectedObject->GetPosition().z };
			if (ImGui::DragFloat3("##Position", position, 0.1f))
			{
				selectedObject->SetPosition(XMFLOAT3(position[0], position[1], position[2]));
			}
		}
		// Rotation
		{
			ImGui::Text("Rotation:");

			float rotation[3] = { selectedObject->GetRotation().x, selectedObject->GetRotation().y, selectedObject->GetRotation().z };
			if (ImGui::DragFloat3("##Rotation", rotation, XM_PIDIV4 / 2, -XM_PI, XM_PI))
			{
				selectedObject->SetRotation(XMFLOAT3(rotation[0], rotation[1], rotation[2]));
			}
		}
		// Scale
		if (selectedPortal)
		{
			ImGui::Text("Scale:");

			float scale[2] = { selectedObject->GetScale().x, selectedObject->GetScale().y };
			if (ImGui::DragFloat2("##Scale", scale, 0.1f, 0.1f, 100.0f))
			{
				// TODO : Fix scaling properly
				if (scale[0] > 0.0f && scale[1] > 0.0f)
				{
					selectedObject->SetScale(XMFLOAT3(scale[0], scale[1], 0.0f));
				}

			}
		}
		else
		{
			ImGui::Text("Scale:");

			float scale[3] = { selectedObject->GetScale().x, selectedObject->GetScale().y, selectedObject->GetScale().z };
			if (ImGui::DragFloat3("##Scale", scale, 0.1f, 0.1f, 100.0f))
			{
				// TODO : Fix scaling properly

				selectedObject->SetScale(XMFLOAT3(scale[0], scale[1], scale[2]));

			}
		}
		// Forward (READ ONLY)
		{
			ImGui::BeginDisabled();
			ImGui::Text("Forward:");

			float forward[3] = { selectedObject->GetForward().x, selectedObject->GetForward().y, selectedObject->GetForward().z };

			ImGui::DragFloat3("##Forward", forward, 0.05f);
			ImGui::EndDisabled();
		}
		// Model
		{
			ImGui::Text("Model:");

			std::string name = "None";
			if (selectedObject->GetModel())
			{
				name = selectedObject->GetModel()->GetName().c_str();
			}
			if (ImGui::BeginCombo("##Model", name.c_str()))
			{
				for (auto model : g_scene->m_models)
				{
					const bool is_selected = (selectedObject->GetModel() == model);
					if (ImGui::Selectable(model->GetName().c_str(), is_selected))
						selectedObject->SetModel(model);

					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				// Add additional "None" option
				{
					const bool is_selected = (selectedObject->GetModel() == nullptr);
					if (ImGui::Selectable("None", is_selected))
						selectedObject->SetModel(nullptr);

					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				// Add additional "Load" option
				{
					// TODO : Add .obj loading
					ImGui::BeginDisabled();
					if (ImGui::Button("Load"))
					{
						ImGui::OpenPopup("##LoadModel");
					}
					ImGui::EndDisabled();

					if (ImGui::BeginPopup("##LoadModel"))
					{
						static std::string path;
						static std::string name;
						// use to tell the user they've entered an invalid path
						const char validPathHint[] = "Path...";
						const char invalidPathHint[] = "Invalid Path!";
						static std::string pathHint = validPathHint;

						// Ensure the user can only hit load if they've changed the path and have a valid name
						static bool pathChanged = false;
						bool nameValid = false;

						pathChanged |= ImGui::InputTextWithHint("##ModelPath", pathHint.c_str(), &path);
						pathChanged &= !path.empty();

						ImGui::InputTextWithHint("##ModelName", "Name...", &name);
						// Ensure the name isn't naught and that it doesn't already exist
						nameValid = !name.empty();
						//nameValid &= !m_cbvSrvUavHeap->m_models.contains(name);

						ImGui::BeginDisabled(!(pathChanged && nameValid));
						if (ImGui::Button("Load"))
						{
							// Convert the path to a literal to use in the model loader
							std::wstring wpath(path.begin(), path.end());
							auto model = CreateModel(wpath.c_str(), name);
							// If the model was loaded correctly, set it
							if (model)
							{
								selectedObject->SetModel(model);
								pathHint = validPathHint;

							}
							else
							{
								path = "";
								pathHint = invalidPathHint;
								pathChanged = false;
							}
						}
						ImGui::EndDisabled();

						ImGui::EndPopup();
					}
				}
				ImGui::EndCombo();
			}
		}
		// See if this object is a portal
		if(selectedPortal)
		{
			// As the current item is a portal, show the portal properties
			ImGui::Text("Other Portal:");

			std::string name = "None";
			auto otherPortal = selectedPortal->GetOtherPortal();
			if (otherPortal)
			{
				name = otherPortal->GetName().c_str();
			}
			if (ImGui::BeginCombo("##OtherPortal", name.c_str()))
			{
				for (auto portal : g_scene->m_portals)
				{
					const bool is_selected = (otherPortal == portal);
					if (ImGui::Selectable(portal->GetName().c_str(), is_selected))
						selectedPortal->SetOtherPortal(portal);

					// Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		else
		{
			// Texture
			{
				ImGui::Text("Texture:");

				std::string name = "None";
				if (selectedObject->GetTexture())
				{
					name = selectedObject->GetTexture()->name.c_str();
				}
				if (ImGui::BeginCombo("##Texture", name.c_str()))
				{
					for (auto texture : g_scene->m_textures)
					{
						const bool is_selected = (selectedObject->GetTexture() == texture);
						if (ImGui::Selectable(texture->name.c_str(), is_selected))
							selectedObject->SetTexture(texture);

						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					// Add additional "None" option
					{
						const bool is_selected = (selectedObject->GetTexture() == nullptr);
						if (ImGui::Selectable("None", is_selected))
							selectedObject->SetTexture(nullptr);

						if (is_selected)
							ImGui::SetItemDefaultFocus();
					}
					// Add additional "Load" option
					{
						if (ImGui::Button("Load"))
						{
							ImGui::OpenPopup("##LoadTexture");
						}
						if (ImGui::BeginPopup("##LoadTexture"))
						{
							static std::string path;
							static std::string name;
							// use to tell the user they've entered an invalid path
							const char validPathHint[] = "Path...";
							const char invalidPathHint[] = "Invalid Path!";
							static std::string pathHint = validPathHint;

							// Ensure the user can only hit load if they've changed the path and have a valid name
							static bool pathChanged = false;
							bool nameValid = false;

							pathChanged |= ImGui::InputTextWithHint("##TexturePath", pathHint.c_str(), &path);
							pathChanged &= !path.empty();
							ImGui::InputTextWithHint("##TextureName", "Name...", &name);
							nameValid = !name.empty();
							//nameValid &= !m_cbvSrvUavHeap->m_textures.contains(name);

							ImGui::BeginDisabled(!(pathChanged && nameValid));
							if (ImGui::Button("Load"))
							{
								// Convert the path to a literal to use in the model loader
								std::wstring wpath(path.begin(), path.end());
								auto texture = CreateTexture(wpath.c_str(), name);
								// If the model was loaded correctly, set it
								if (texture)
								{
									selectedObject->SetTexture(texture);
									pathHint = validPathHint;

								}
								else
								{
									path = "";
									pathHint = invalidPathHint;
									pathChanged = false;
								}
							}
							ImGui::EndDisabled();

							ImGui::EndPopup();
						}
					}
					ImGui::EndCombo();
				}
			}
		}

		//    ImGui::TreePop();
		//}
	}

	ImGui::End();

}

void Renderer::DestroyGUI()
{
#if defined (_GUI)
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
}

void Renderer::RenderGUI(ID3D12GraphicsCommandList* commandList)
{
#if defined (_GUI)
	// render Dear ImGui

	// Rendering
	// (Your code clears your framebuffer, renders your other stuff etc.)
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	// (Your code calls ExecuteCommandLists, swapchain's Present(), etc.)

#endif
}


void Renderer::PrepareCommandList(ID3D12GraphicsCommandList* commandList)
{
	// Set necessary state.
	commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	// Set command list shader resource view and constant buffer view
	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap->GetDescriptorHeap(), m_samplerHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	// Describe how samplers are laid out to GPU
	commandList->SetGraphicsRootDescriptorTable(DescriptorHeap::RootParameterIndices::Sampler, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());

	commandList->RSSetViewports(1, &m_viewport);
	commandList->RSSetScissorRects(1, &m_scissorRect);
}

