#include "Primitive.h"

Primitive::Primitive(std::string name) :
m_indexBufferView(),
m_vertexBufferView(),
m_name(name)
{
}

bool Primitive::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState, ID3D12RootSignature* rootSignature, const wchar_t* path)
{
    if (LoadModel(path))
    {
        CreateVertexBuffer(device, commandList);
        CreateIndexBuffer(device, commandList);
        CreateBundle(device, pipelineState, rootSignature);
        return true;
    }
    return false;
}

void Primitive::Draw(ID3D12GraphicsCommandList* commandList)
{
    commandList->ExecuteBundle(m_bundle.Get());
}

bool Primitive::LoadModel(const wchar_t* path)
{
    static std::string cubePath = "Cube";
    static std::wstring wcubePath = std::wstring(cubePath.begin(), cubePath.end());
    static std::string pyramidPath = "Pyramid";
    static std::wstring wpyramidPath = std::wstring(pyramidPath.begin(), pyramidPath.end());

    if (path[0] == wcubePath.c_str()[0])
    {
        // Define the geometry for a cube.
        m_vertices =
        {
            // front face
            {{ -0.5f,  0.5f, -0.5f}, {0.0f, 0.0f }},
            {{  0.5f, -0.5f, -0.5f}, {1.0f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f}, {0.0f, 1.0f }},
            {{  0.5f,  0.5f, -0.5f}, {1.0f, 0.0f }},

            // right side face
            {{  0.5f, -0.5f, -0.5f}, {0.0f, 1.0f }},
            {{  0.5f,  0.5f,  0.5f}, {1.0f, 0.0f }},
            {{  0.5f, -0.5f,  0.5f}, {1.0f, 1.0f }},
            {{  0.5f,  0.5f, -0.5f}, {0.0f, 0.0f }},

            // left side face
            {{ -0.5f, 0.5f, 0.5f}, { 0.0f, 0.0f }},
            { { -0.5f, -0.5f, -0.5f}, {1.0f, 1.0f } },
            { { -0.5f, -0.5f,  0.5f}, {0.0f, 1.0f } },
            { { -0.5f,  0.5f, -0.5f}, {1.0f, 0.0f } },

            // back face
            { {  0.5f,  0.5f,  0.5f}, {0.0f, 0.0f } },
            { { -0.5f, -0.5f,  0.5f}, {1.0f, 1.0f } },
            { {  0.5f, -0.5f,  0.5f}, {0.0f, 1.0f } },
            { { -0.5f,  0.5f,  0.5f}, {1.0f, 0.0f } },

            // top face
            { { -0.5f,  0.5f, -0.5f}, {0.0f, 1.0f } },
            { {  0.5f,  0.5f,  0.5f}, {1.0f, 0.0f } },
            { {  0.5f,  0.5f, -0.5f}, {1.0f, 1.0f } },
            { { -0.5f,  0.5f,  0.5f}, {0.0f, 0.0f } },

            // bottom face
            { {  0.5f, -0.5f,  0.5f}, {0.0f, 0.0f } },
            { { -0.5f, -0.5f, -0.5f}, {1.0f, 1.0f } },
            { {  0.5f, -0.5f, -0.5f}, {0.0f, 1.0f } },
            { { -0.5f, -0.5f,  0.5f}, {1.0f, 0.0f } }
        };

        m_indices =
        {
            // front face
            0, 1, 2, // first triangle
            0, 3, 1, // second triangle

            // left face
            4, 5, 6, // first triangle
            4, 7, 5, // second triangle

            // right face
            8, 9, 10, // first triangle
            8, 11, 9, // second triangle

            // back face
            12, 13, 14, // first triangle
            12, 15, 13, // second triangle

            // top face
            16, 17, 18, // first triangle
            16, 19, 17, // second triangle

            // bottom face
            20, 21, 22, // first triangle
            20, 23, 21, // second triangle
        };
    }
    else if (path[0] == wpyramidPath.c_str()[0])
    {
        m_vertices = 
        {
            { {0.5f, -0.5f, 0.5f}, {1.0f, 0.0f} },
            { {0.5f, -0.5f, -0.5f}, { 0.0f, 0.0f} },
            { {-0.5f, -0.5f, -0.5f}, { 1.0f, 0.0f} },
            { {-0.5f, -0.5f, 0.5f}, { 0.0f, 0.0f} },
            { {0.0f, 0.5f, 0.0f}, { 0.5f, 1.0f} },
        };

        m_indices =
        {
            1,4,0,
            2,4,1,
            3,4,2,
            0,4,3,
            1,0,3,
            3,2,1
        };
    }
    // Could not 'Load' shape (all behaviour to do with mesh loading is dummy
    else
    {
        return false;
    }


    m_verticesSize = m_vertices.size() * UINT(sizeof(Vertex));
    m_indicesCount = m_indices.size();
    m_indicesSize = m_indicesCount * UINT(sizeof(Index));

    return true;
}

void Primitive::CreateVertexBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
    


    // Create the vertex buffer in an implicit copy heap,
    // which we will copy to from an upload heap to upload to the GPU
    {
        // Create a default heap
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        // Create a buffer large enough to encompass the vertex data
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_verticesSize);


        // Create the vertex buffer as a committed resource and an implicit heap big enough to contain it, and commit the resource to the heap.
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, // Default heap
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, // Create this as a copy destination for the upload heap, it is the vertex buffers final resting place.
            nullptr,    // Only need optimized clear value for RTV/DSV
            IID_PPV_ARGS(&m_vertexBuffer)   // GUID of vertex buffer interface
        ), "Failed to create vertex buffer.\n");
    }
    m_vertexBuffer->SetName(L"m_vertexBuffer");



    // create upload heap to transfer vertex buffer data to the vertex buffer
    {
        // Create an upload heap to upload the GPU Vertex Buffer heap
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        // Create a buffer large enough to encompass the vertex data
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_verticesSize);


        // Create the vertex buffer as a committed resource and an implicit heap big enough to contain it, and commit the resource to the heap.
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, // Upload heap
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,  // Required heap state for an upload heap, GPU will read from this heap
            nullptr,    // Only need optimized clear value for RTV/DSV
            IID_PPV_ARGS(&m_vbUploadHeap)
        ), "Failed to create vertex buffer upload heap.\n");
    }
    m_vbUploadHeap->SetName(L"m_vbUploadHeap");

    // Store vertex buffer in upload heap
    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = reinterpret_cast<void**>(m_vertices.data()); // Pointer to the vertex array
    vertexData.RowPitch = m_verticesSize;     // Physical size of vertex data
    vertexData.SlicePitch = m_verticesSize;


    // Queue the copying of the Vertex Buffer (VB) intermediary upload heap to the VB resource.
    UpdateSubresources(
        commandList,    // pointer for the command list interface to upload with
        m_vertexBuffer.Get(),   // Destination resource
        m_vbUploadHeap.Get(),
        0,  // Offset in bytes to intermediate resource
        0,  // Index of first subresource in the resource, always 0 for buffer resources
        1,  // Number of subresources  in the resource to be updated, always 1 for buffer resources
        &vertexData // Vertices to copy to GPU
    );

    // Once the vertex data has been copied to the VB, transition it to the VB state from the copy destination state, as it needn't be copied to any longer
    {
        // Create a transition barrier to transition the VB from copy destination to VB state
        CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_vertexBuffer.Get(),   // Resource to transtition
            D3D12_RESOURCE_STATE_COPY_DEST, // Resource's starting state, as copy dest for the upload heap
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER // VB state to transition to
        );
        // Queue the aforementioned transition in the command list
        commandList->ResourceBarrier(1, &transitionBarrier);
    }

    // create vertex buffer view, used to tell input assembler where vertices are stored in GPU memory
    {
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress(); // specify D3D12_GPU_VIRTUAL_ADDRESS that identifies buffer address
        m_vertexBufferView.SizeInBytes = m_verticesSize;    // specify size of the buffer in bytes
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);  // specify size in bytes of each vertex entry in buffer 
    }
}

void Primitive::CreateIndexBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
// Create index buffer
{
    // Create the index buffer in an implicit copy heap,
    // which we will copy to from an upload heap to upload to the GPU
    {
        // Create a default heap
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        // Create a buffer large enough to encompass the index data
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_indicesSize);


        // Create the index buffer as a committed resource and an implicit heap big enough to contain it, and commit the resource to the heap.
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, // Default heap
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, // Create this as a copy destination for the upload heap, it is the index buffers final resting place.
            nullptr,    // Only need optimized clear value for RTV/DSV
            IID_PPV_ARGS(&m_indexBuffer)   // GUID of index buffer interface
        ), "Failed to create index buffer.\n");
    }
    m_indexBuffer->SetName(L"m_indexBuffer");



    // create upload heap to transfer Index Buffer (IB) data to the IB
    {
        // Create an upload heap to upload the GPU IB heap
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        // Create a buffer large enough to encompass the index data
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_indicesSize);


        // Create the IB as a committed resource and an implicit heap big enough to contain it, and commit the resource to the heap.
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps, // Upload heap
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,  // Required heap state for an upload heap, GPU will read from this heap
            nullptr,    // Only need optimized clear value for RTV/DSV
            IID_PPV_ARGS(&m_ibUploadHeap)
        ), "Failed to create index buffer upload heap.\n");
    }
    m_ibUploadHeap->SetName(L"m_ibUploadHeap");

    // Store index buffer in upload heap
    D3D12_SUBRESOURCE_DATA indexData = {};
    indexData.pData = reinterpret_cast<void**>(m_indices.data()); // Pointer to the index array
    indexData.RowPitch = m_indicesSize;     // Physical size of index data
    indexData.SlicePitch = m_indicesSize;


    // Queue the copying of the IB intermediary upload heap to the IB resource.
    UpdateSubresources(
        commandList,    // pointer for the command list interface to upload with
        m_indexBuffer.Get(),   // Destination resource
        m_ibUploadHeap.Get(),
        0,  // Offset in bytes to intermediate resource
        0,  // Index of first subresource in the resource, always 0 for buffer resources
        1,  // Number of subresources  in the resource to be updated, always 1 for buffer resources
        &indexData // Indices to copy to GPU
    );

    // Once the index data has been copied to the IB, transition it to the IB state from the copy destination state, as it needn't be copied to any longer
    {
        // Create a transition barrier to transition the IB from copy destination to IB state
        CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_indexBuffer.Get(),   // Resource to transtition
            D3D12_RESOURCE_STATE_COPY_DEST, // Resource's starting state, as copy dest for the upload heap
            D3D12_RESOURCE_STATE_INDEX_BUFFER // IB state to transition to
        );
        // Queue the aforementioned transition in the command list
        commandList->ResourceBarrier(1, &transitionBarrier);
    }

    // create index buffer view, used to tell input assembler where indices are stored in GPU memory
    {
        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress(); // specify D3D12_GPU_VIRTUAL_ADDRESS that identifies buffer address
        m_indexBufferView.SizeInBytes = m_indicesSize;    // specify size of the buffer in bytes
        m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;    // Specify DWORD as format
    }
}

void Primitive::CreateBundle(ID3D12Device* device, ID3D12PipelineState* pipelineState, ID3D12RootSignature* rootSignature)
{
    // Create bundle allocator
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&m_bundleAllocator)), "Couldn't create command bundle.\n");

    // Create the bundle for drawing this model
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, m_bundleAllocator.Get(), pipelineState, IID_PPV_ARGS(&m_bundle)));

    // Populate the bundle with what is necessary to draw this model
    m_bundle->SetGraphicsRootSignature(rootSignature);
    m_bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_bundle->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_bundle->IASetIndexBuffer(&m_indexBufferView);
    m_bundle->DrawIndexedInstanced(m_indicesCount, 1, 0, 0, 0);

    // Cease recording of this bundle
    ThrowIfFailed(m_bundle->Close());

}
