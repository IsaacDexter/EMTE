#pragma once
#include "stdafx.h"
#include <array>

class Window;

class Application
{
public:
	Application(HINSTANCE hInstance);
	
	/*
	1. Initialize
	2. Repeat
		- Update
		- Render
	3. Destroy
	*/
	void Initialize();
	void Update();
	void Render();
	void Destroy();

private:
	static const UINT m_frameCount = 2;

	struct Vertex
	{
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
	};

#pragma region Pipeline

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
	Microsoft::WRL::ComPtr<ID3D12Device4> m_device;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Application::m_frameCount> m_renderTargets;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorSize;
	bool m_useWarpDevice = false;

#pragma endregion

#pragma region Resources

	Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

#pragma endregion

#pragma region Sync

	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

#pragma endregion

	std::shared_ptr<Window> m_window;

private:
#pragma region Initialization

	void InitializePipeline();

	Microsoft::WRL::ComPtr<ID3D12Debug> EnableDebugLayer();
	/**
	* Compatible adapter must be present before creating D3D12 device
	* @param useWarpDevice Whether or not to use software rasterizer
	* @returns pointer to the adapter
	*/
	Microsoft::WRL::ComPtr<IDXGIAdapter4> GetAdapter(bool useWarpDevice);

	void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter);
	/**
	* Create D3D12 device, used to create resources
	* @param Adapter from GetAdapter
	* @returns ComPtr to newly created D3D12 Device
	*/
	Microsoft::WRL::ComPtr<ID3D12Device4> CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter);

	void CreateDevice();
	/**
	* Desribe and create a command queue
	* @param type the type of command queue to create
	* @returns The newly created command queue
	*/
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> CreateCommandQueue(Microsoft::WRL::ComPtr<ID3D12Device> device, const D3D12_COMMAND_LIST_TYPE type);

	/**
	* Create the dxgi factory and swap chain, used to flip the front and back buffer
	* @param hWnd Handle to OS window used to display rendered image
	* @param commandQueue command queue used to issue commands to the cpu
	* @param describes the resolution width, set to 0 to obtain that of the output window
	* @param describes the resolution height, set to 0 to obtain that of the output window
	* @param the number of buffers in the swap chain
	* @returns ComPtr to the newly created swap chain
	*/
	Microsoft::WRL::ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount);

	/**
	* Create the descriptor heap, an array of resource views.Used to store render target views for the swap chain buffers
	* @param device comptr to the dx12 device
	* @param type Type that specifies the types of descriptors in the heap: CVB, SRV, UAV; Sampler; RTV; DSV
	* @param numDescriptors Number of descriptors in the heap
	* @returns ComPtr to newly created descriptor heap
	*/
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device4> device, const D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, UINT &descriptorSize);

	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Application::m_frameCount> CreateRenderTargetViews(Microsoft::WRL::ComPtr<ID3D12Device4> device, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap, Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain, UINT& rtvDescriptorSize);

	/**
	* Create a resource large enough to hold buffer data
	* Create an intermediate upload buffer to transfer data from CPU to GPU memory
	* @param commandList The command list to transfer buffer data with
	* @param pDestinationResource Created destination resource large enough to hold buffer data
	* Cannot be deleted until resource has been fully uploaded to destination
	* @param pIntermediateResource Created intermediate upload buffer to transfer data from CPU to GPU memory.
	* @param numElements CPU buffer data to be transfered to GPU resource
	* @param elementSize CPU buffer data to be transfered to GPU resource
	* @param bufferData CPU buffer data to be transfered to GPU resource
	* @param flags Additional flags to create the buffer resource
	*/
	void UpdateBufferResource(
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList,
		ID3D12Resource** pDestinationResource,
		ID3D12Resource** pIntermediateResource,
		size_t numElements,
		size_t elementSize,
		const void* bufferData,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE
	);

	/**
	* Check if screen tearing should be allowed for variable refresh displays
	* @returns true if tearing should be allowed
	*/
	bool CheckTearingSupport();

	void InitializeAssets();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature();

	/** 
	* Creates the pipeline state stream, serialized object, and object. 
	* The various descs are pulled from their various functions.
	* @param pVertexShaderBlob pointer to memory block containing vertex shader
	* @param pPixelShaderBlob pointer to memory block containing pixel shader
	* @returns ComPtr to the created PSO
	*/
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineStateObject(ID3DBlob* pVertexShaderBlob, ID3DBlob* pPixelShaderBlob);



	void WaitForPreviousFrame();

#pragma endregion

#pragma region Rendering

	void PopulateCommandList();

#pragma endregion
};
