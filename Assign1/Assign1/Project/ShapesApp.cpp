//***************************************************************************************
// LitColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
    ShapesApp(HINSTANCE hInstance);
    ShapesApp(const ShapesApp& rhs) = delete;
    ShapesApp& operator=(const ShapesApp& rhs) = delete;
    ~ShapesApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	void BuildSkullGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
 
private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
    float mRadius = 60.0f;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        ShapesApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
	BuildSkullGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void ShapesApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::AnimateMaterials(const GameTimer& gt)
{
	
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.2f, 0.2f, 0.2f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Create root CBV.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(40.0f, 40.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(1.0f, 0.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 0.75f, 0.9f, 1, 5, 3);
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(0.5f, 1.f, 40, 40);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1, 1, 0.5f, 0.0f, 1, 3);
	GeometryGenerator::MeshData prism = geoGen.CreatePrism(1, 1.f, 1.f, 3);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1, 1.f, 1.f, 3);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT diamondVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT torusVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT pyramidVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
	UINT prismVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT wedgeVertexOffset = prismVertexOffset + (UINT)prism.Vertices.size();


	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT diamondIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT torusIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT pyramidIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();
	UINT prismIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT wedgeIndexOffset = prismIndexOffset + (UINT)prism.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry prismSubmesh;
	prismSubmesh.IndexCount = (UINT)prism.Indices32.size();
	prismSubmesh.StartIndexLocation = prismIndexOffset;
	prismSubmesh.BaseVertexLocation = prismVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() + 
		diamond.Vertices.size() + 
		torus.Vertices.size() + 
		pyramid.Vertices.size() +
		prism.Vertices.size() + 
		wedge.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
	}

	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
	}

	for (size_t i = 0; i < prism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = prism.Vertices[i].Position;
		vertices[k].Normal = prism.Vertices[i].Normal;
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(prism.GetIndices16()), std::end(prism.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));


    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["prism"] = prismSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;


	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");

	if(!fin)
	{
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vcount);
	for(UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for(UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}

void ShapesApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void ShapesApp::BuildMaterials()
{
	int cbIndex = 0;
	int srvHeapIndex = 0;

	auto gold = std::make_unique<Material>();
	gold->Name = "gold";
	gold->MatCBIndex = cbIndex++;
	gold->DiffuseSrvHeapIndex = srvHeapIndex++;
	gold->DiffuseAlbedo = XMFLOAT4(Colors::Gold);
	gold->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	gold->Roughness = 0.01f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = cbIndex++;
	stone0->DiffuseSrvHeapIndex = srvHeapIndex++;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::LightSteelBlue);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.8f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = cbIndex++;
	tile0->DiffuseSrvHeapIndex = srvHeapIndex++;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::ForestGreen);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.8f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = cbIndex++; 
	skullMat->DiffuseSrvHeapIndex = srvHeapIndex++;
	skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.5f, 0.5f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	skullMat->Roughness = 0.3f;

	auto diamond1Mat = std::make_unique<Material>();
	diamond1Mat->Name = "diamond1Mat";
	diamond1Mat->MatCBIndex = cbIndex++;
	diamond1Mat->DiffuseSrvHeapIndex = srvHeapIndex++;
	diamond1Mat->DiffuseAlbedo = XMFLOAT4(0.45f, 0.15f, 0.2f, 0.8f);
	diamond1Mat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	diamond1Mat->Roughness = 0.3f;

	auto diamond2Mat = std::make_unique<Material>();
	diamond2Mat->Name = "diamond2Mat";
	diamond2Mat->MatCBIndex = cbIndex++;
	diamond2Mat->DiffuseSrvHeapIndex = srvHeapIndex++;
	diamond2Mat->DiffuseAlbedo = XMFLOAT4(Colors::DimGray);
	diamond2Mat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	diamond2Mat->Roughness = 0.8f;

	auto torusMat = std::make_unique<Material>();
	torusMat->Name = "torusMat";
	torusMat->MatCBIndex = cbIndex++;
	torusMat->DiffuseSrvHeapIndex = srvHeapIndex++;
	torusMat->DiffuseAlbedo = XMFLOAT4(Colors::Green);
	torusMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	torusMat->Roughness = 0.4f;

	auto pyramidMat = std::make_unique<Material>();
	pyramidMat->Name = "pyramidMat";
	pyramidMat->MatCBIndex = cbIndex++;
	pyramidMat->DiffuseSrvHeapIndex = srvHeapIndex++;
	pyramidMat->DiffuseAlbedo = XMFLOAT4(Colors::SandyBrown);
	pyramidMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	pyramidMat->Roughness = 0.8f;

	auto prismMat = std::make_unique<Material>();
	prismMat->Name = "prismMat";
	prismMat->MatCBIndex = cbIndex++;
	prismMat->DiffuseSrvHeapIndex = srvHeapIndex++;
	prismMat->DiffuseAlbedo = XMFLOAT4(Colors::CornflowerBlue);
	prismMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	prismMat->Roughness = 0.7f;

	auto wedgeMat = std::make_unique<Material>();
	wedgeMat->Name = "wedgeMat";
	wedgeMat->MatCBIndex = cbIndex++;
	wedgeMat->DiffuseSrvHeapIndex = srvHeapIndex++;
	wedgeMat->DiffuseAlbedo = XMFLOAT4(Colors::Sienna);
	wedgeMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	wedgeMat->Roughness = 0.55f;

	mMaterials["gold"] = std::move(gold);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["diamond1Mat"] = std::move(diamond1Mat);
	mMaterials["diamond2Mat"] = std::move(diamond2Mat);
	mMaterials["torusMat"] = std::move(torusMat);
	mMaterials["pyramidMat"] = std::move(pyramidMat);
	mMaterials["prismMat"] = std::move(prismMat);
	mMaterials["wedgeMat"] = std::move(wedgeMat);


}

void ShapesApp::BuildRenderItems()
{
	UINT objCBIndex = 0;

	//Keep
	auto keepBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&keepBox->World, XMMatrixScaling(10.0f, 14.0f, 6.0f)*XMMatrixTranslation(0.0f, 7.0f, 0.0f));
	XMStoreFloat4x4(&keepBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	keepBox->ObjCBIndex = objCBIndex++;
	keepBox->Mat = mMaterials["stone0"].get();
	keepBox->Geo = mGeometries["shapeGeo"].get();
	keepBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	keepBox->IndexCount = keepBox->Geo->DrawArgs["box"].IndexCount;
	keepBox->StartIndexLocation = keepBox->Geo->DrawArgs["box"].StartIndexLocation;
	keepBox->BaseVertexLocation = keepBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(keepBox));

	//Keep Roof
	auto keepRoofPyramid = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&keepRoofPyramid->World, XMMatrixScaling(12.0f, 4.0f, 8.0f)*XMMatrixTranslation(0.0f, 16.0f, 0.0f));
	XMStoreFloat4x4(&keepRoofPyramid->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	keepRoofPyramid->ObjCBIndex = objCBIndex++;
	keepRoofPyramid->Mat = mMaterials["wedgeMat"].get();
	keepRoofPyramid->Geo = mGeometries["shapeGeo"].get();
	keepRoofPyramid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	keepRoofPyramid->IndexCount = keepRoofPyramid->Geo->DrawArgs["pyramid"].IndexCount;
	keepRoofPyramid->StartIndexLocation = keepRoofPyramid->Geo->DrawArgs["pyramid"].StartIndexLocation;
	keepRoofPyramid->BaseVertexLocation = keepRoofPyramid->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(keepRoofPyramid));

	//Keep Stairs
	auto keepStairsWedge = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&keepStairsWedge->World, XMMatrixScaling(5.0f, 2.0f, 3.0f)*XMMatrixRotationRollPitchYaw(0.0f, XM_PI, 0.0f)*XMMatrixTranslation(0.0f, 1.0f, -4.5f));
	XMStoreFloat4x4(&keepStairsWedge->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	keepStairsWedge->ObjCBIndex = objCBIndex++;
	keepStairsWedge->Mat = mMaterials["wedgeMat"].get();
	keepStairsWedge->Geo = mGeometries["shapeGeo"].get();
	keepStairsWedge->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	keepStairsWedge->IndexCount = keepStairsWedge->Geo->DrawArgs["wedge"].IndexCount;
	keepStairsWedge->StartIndexLocation = keepStairsWedge->Geo->DrawArgs["wedge"].StartIndexLocation;
	keepStairsWedge->BaseVertexLocation = keepStairsWedge->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(keepStairsWedge));

	//Back Wall
	auto backWallBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backWallBox->World, XMMatrixScaling(28.0f, 6.0f, 1.0f)*XMMatrixTranslation(0.0f, 3.0f, 12.0f));
	XMStoreFloat4x4(&backWallBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backWallBox->ObjCBIndex = objCBIndex++;
	backWallBox->Mat = mMaterials["stone0"].get();
	backWallBox->Geo = mGeometries["shapeGeo"].get();
	backWallBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backWallBox->IndexCount = backWallBox->Geo->DrawArgs["box"].IndexCount;
	backWallBox->StartIndexLocation = backWallBox->Geo->DrawArgs["box"].StartIndexLocation;
	backWallBox->BaseVertexLocation = backWallBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backWallBox));

	//Front Right Wall
	auto RfrontWallBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RfrontWallBox->World, XMMatrixScaling(9.0f, 6.0f, 1.0f)*XMMatrixTranslation(7.0f, 3.0f, -18.0f));
	XMStoreFloat4x4(&RfrontWallBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RfrontWallBox->ObjCBIndex = objCBIndex++;
	RfrontWallBox->Mat = mMaterials["stone0"].get();
	RfrontWallBox->Geo = mGeometries["shapeGeo"].get();
	RfrontWallBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RfrontWallBox->IndexCount = RfrontWallBox->Geo->DrawArgs["box"].IndexCount;
	RfrontWallBox->StartIndexLocation = RfrontWallBox->Geo->DrawArgs["box"].StartIndexLocation;
	RfrontWallBox->BaseVertexLocation = RfrontWallBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(RfrontWallBox));

	//Front Left Wall
	auto LfrontWallBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&LfrontWallBox->World, XMMatrixScaling(9.0f, 6.0f, 1.0f)*XMMatrixTranslation(-7.0f, 3.0f, -18.0f));
	XMStoreFloat4x4(&LfrontWallBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	LfrontWallBox->ObjCBIndex = objCBIndex++;
	LfrontWallBox->Mat = mMaterials["stone0"].get();
	LfrontWallBox->Geo = mGeometries["shapeGeo"].get();
	LfrontWallBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	LfrontWallBox->IndexCount = LfrontWallBox->Geo->DrawArgs["box"].IndexCount;
	LfrontWallBox->StartIndexLocation = LfrontWallBox->Geo->DrawArgs["box"].StartIndexLocation;
	LfrontWallBox->BaseVertexLocation = LfrontWallBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(LfrontWallBox));

	//Left Wall
	auto leftWallBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftWallBox->World, XMMatrixScaling(1.0f, 6.0f, 28.0f)*XMMatrixTranslation(-14.0f, 3.0f, -3.0f));
	XMStoreFloat4x4(&leftWallBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftWallBox->ObjCBIndex = objCBIndex++;
	leftWallBox->Mat = mMaterials["stone0"].get();
	leftWallBox->Geo = mGeometries["shapeGeo"].get();
	leftWallBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftWallBox->IndexCount = leftWallBox->Geo->DrawArgs["box"].IndexCount;
	leftWallBox->StartIndexLocation = leftWallBox->Geo->DrawArgs["box"].StartIndexLocation;
	leftWallBox->BaseVertexLocation = leftWallBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftWallBox));

	//Right Wall
	auto rightWallBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightWallBox->World, XMMatrixScaling(1.0f, 6.0f, 28.0f)*XMMatrixTranslation(14.0f, 3.0f, -3.0f));
	XMStoreFloat4x4(&rightWallBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightWallBox->ObjCBIndex = objCBIndex++;
	rightWallBox->Mat = mMaterials["stone0"].get();
	rightWallBox->Geo = mGeometries["shapeGeo"].get();
	rightWallBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightWallBox->IndexCount = rightWallBox->Geo->DrawArgs["box"].IndexCount;
	rightWallBox->StartIndexLocation = rightWallBox->Geo->DrawArgs["box"].StartIndexLocation;
	rightWallBox->BaseVertexLocation = rightWallBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightWallBox));

	//Rear Left Tower
	auto RLTowerBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RLTowerBox->World, XMMatrixScaling(4.0f, 8.0f, 4.0f)*XMMatrixTranslation(-13.0f, 4.0f, 11.0f));
	XMStoreFloat4x4(&RLTowerBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RLTowerBox->ObjCBIndex = objCBIndex++;
	RLTowerBox->Mat = mMaterials["diamond2Mat"].get();
	RLTowerBox->Geo = mGeometries["shapeGeo"].get();
	RLTowerBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RLTowerBox->IndexCount = RLTowerBox->Geo->DrawArgs["box"].IndexCount;
	RLTowerBox->StartIndexLocation = RLTowerBox->Geo->DrawArgs["box"].StartIndexLocation;
	RLTowerBox->BaseVertexLocation = RLTowerBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(RLTowerBox));

	//Rear Right Tower
	auto RRTowerBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RRTowerBox->World, XMMatrixScaling(4.0f, 8.0f, 4.0f)*XMMatrixTranslation(13.0f, 4.0f, 11.0f));
	XMStoreFloat4x4(&RRTowerBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RRTowerBox->ObjCBIndex = objCBIndex++;
	RRTowerBox->Mat = mMaterials["diamond2Mat"].get();
	RRTowerBox->Geo = mGeometries["shapeGeo"].get();
	RRTowerBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RRTowerBox->IndexCount = RRTowerBox->Geo->DrawArgs["box"].IndexCount;
	RRTowerBox->StartIndexLocation = RRTowerBox->Geo->DrawArgs["box"].StartIndexLocation;
	RRTowerBox->BaseVertexLocation = RRTowerBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(RRTowerBox));

	//Front Left Tower
	auto FLTowerBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FLTowerBox->World, XMMatrixScaling(4.0f, 8.0f, 4.0f)*XMMatrixTranslation(-13.0f, 4.0f, -17.0f));
	XMStoreFloat4x4(&FLTowerBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	FLTowerBox->ObjCBIndex = objCBIndex++;
	FLTowerBox->Mat = mMaterials["diamond2Mat"].get();
	FLTowerBox->Geo = mGeometries["shapeGeo"].get();
	FLTowerBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FLTowerBox->IndexCount = FLTowerBox->Geo->DrawArgs["box"].IndexCount;
	FLTowerBox->StartIndexLocation = FLTowerBox->Geo->DrawArgs["box"].StartIndexLocation;
	FLTowerBox->BaseVertexLocation = FLTowerBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FLTowerBox));

	//Front Right Tower
	auto FRTowerBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FRTowerBox->World, XMMatrixScaling(4.0f, 8.0f, 4.0f)*XMMatrixTranslation(13.0f, 4.0f, -17.0f));
	XMStoreFloat4x4(&FRTowerBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	FRTowerBox->ObjCBIndex = objCBIndex++;
	FRTowerBox->Mat = mMaterials["diamond2Mat"].get();
	FRTowerBox->Geo = mGeometries["shapeGeo"].get();
	FRTowerBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FRTowerBox->IndexCount = FRTowerBox->Geo->DrawArgs["box"].IndexCount;
	FRTowerBox->StartIndexLocation = FRTowerBox->Geo->DrawArgs["box"].StartIndexLocation;
	FRTowerBox->BaseVertexLocation = FRTowerBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FRTowerBox));



	//Rear Left Tower Cap
	auto RLTowerCap = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RLTowerCap->World, XMMatrixScaling(3.0f, 4.0f, 3.0f)*XMMatrixTranslation(-13.0f, 10.0f, 11.0f));
	XMStoreFloat4x4(&RLTowerCap->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RLTowerCap->ObjCBIndex = objCBIndex++;
	RLTowerCap->Mat = mMaterials["prismMat"].get();
	RLTowerCap->Geo = mGeometries["shapeGeo"].get();
	RLTowerCap->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RLTowerCap->IndexCount = RLTowerCap->Geo->DrawArgs["cylinder"].IndexCount;
	RLTowerCap->StartIndexLocation = RLTowerCap->Geo->DrawArgs["cylinder"].StartIndexLocation;
	RLTowerCap->BaseVertexLocation = RLTowerCap->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(RLTowerCap));

	//Rear Right Tower Cap
	auto RRTowerCap = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&RRTowerCap->World, XMMatrixScaling(3.0f, 4.0f, 3.0f)*XMMatrixTranslation(13.0f, 10.0f, 11.0f));
	XMStoreFloat4x4(&RRTowerCap->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	RRTowerCap->ObjCBIndex = objCBIndex++;
	RRTowerCap->Mat = mMaterials["prismMat"].get();
	RRTowerCap->Geo = mGeometries["shapeGeo"].get();
	RRTowerCap->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	RRTowerCap->IndexCount = RRTowerCap->Geo->DrawArgs["cylinder"].IndexCount;
	RRTowerCap->StartIndexLocation = RRTowerCap->Geo->DrawArgs["cylinder"].StartIndexLocation;
	RRTowerCap->BaseVertexLocation = RRTowerCap->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(RRTowerCap));

	//Front Left Tower Cap
	auto FLTowerCap = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FLTowerCap->World, XMMatrixScaling(3.0f, 4.0f, 3.0f)*XMMatrixTranslation(-13.0f, 10.0f, -17.0f));
	XMStoreFloat4x4(&FLTowerCap->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	FLTowerCap->ObjCBIndex = objCBIndex++;
	FLTowerCap->Mat = mMaterials["prismMat"].get();
	FLTowerCap->Geo = mGeometries["shapeGeo"].get();
	FLTowerCap->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FLTowerCap->IndexCount = FLTowerCap->Geo->DrawArgs["cylinder"].IndexCount;
	FLTowerCap->StartIndexLocation = FLTowerCap->Geo->DrawArgs["cylinder"].StartIndexLocation;
	FLTowerCap->BaseVertexLocation = FLTowerCap->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FLTowerCap));

	//Front Right Tower Cap
	auto FRTowerCap = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&FRTowerCap->World, XMMatrixScaling(3.0f, 4.0f, 3.0f)*XMMatrixTranslation(13.0f, 10.0f, -17.0f));
	XMStoreFloat4x4(&FRTowerCap->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	FRTowerCap->ObjCBIndex = objCBIndex++;
	FRTowerCap->Mat = mMaterials["prismMat"].get();
	FRTowerCap->Geo = mGeometries["shapeGeo"].get();
	FRTowerCap->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	FRTowerCap->IndexCount = FRTowerCap->Geo->DrawArgs["cylinder"].IndexCount;
	FRTowerCap->StartIndexLocation = FRTowerCap->Geo->DrawArgs["cylinder"].StartIndexLocation;
	FRTowerCap->BaseVertexLocation = FRTowerCap->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(FRTowerCap));






	//Left Gate
	auto leftGateBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftGateBox->World, XMMatrixScaling(4.0f, 8.0f, 3.0f)*XMMatrixTranslation(-4.0f, 4.0f, -18.0f));
	XMStoreFloat4x4(&leftGateBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftGateBox->ObjCBIndex = objCBIndex++;
	leftGateBox->Mat = mMaterials["diamond2Mat"].get();
	leftGateBox->Geo = mGeometries["shapeGeo"].get();
	leftGateBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftGateBox->IndexCount = leftGateBox->Geo->DrawArgs["box"].IndexCount;
	leftGateBox->StartIndexLocation = leftGateBox->Geo->DrawArgs["box"].StartIndexLocation;
	leftGateBox->BaseVertexLocation = leftGateBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftGateBox));

	//Right Gate
	auto rightGateBox = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightGateBox->World, XMMatrixScaling(4.0f, 8.0f, 3.0f)*XMMatrixTranslation(4.0f, 4.0f, -18.0f));
	XMStoreFloat4x4(&rightGateBox->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightGateBox->ObjCBIndex = objCBIndex++;
	rightGateBox->Mat = mMaterials["diamond2Mat"].get();
	rightGateBox->Geo = mGeometries["shapeGeo"].get();
	rightGateBox->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightGateBox->IndexCount = rightGateBox->Geo->DrawArgs["box"].IndexCount;
	rightGateBox->StartIndexLocation = rightGateBox->Geo->DrawArgs["box"].StartIndexLocation;
	rightGateBox->BaseVertexLocation = rightGateBox->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightGateBox));

	//Left Gate Roof
	auto leftGateWedge = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftGateWedge->World, XMMatrixScaling(3.0f, 3.0f, 6.0f)*XMMatrixRotationRollPitchYaw(0.0f, -XM_PI/2, 0.0f)*XMMatrixTranslation(-3.0f, 9.5f, -18.0f));
	XMStoreFloat4x4(&leftGateWedge->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftGateWedge->ObjCBIndex = objCBIndex++;
	leftGateWedge->Mat = mMaterials["wedgeMat"].get();
	leftGateWedge->Geo = mGeometries["shapeGeo"].get();
	leftGateWedge->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftGateWedge->IndexCount = leftGateWedge->Geo->DrawArgs["wedge"].IndexCount;
	leftGateWedge->StartIndexLocation = leftGateWedge->Geo->DrawArgs["wedge"].StartIndexLocation;
	leftGateWedge->BaseVertexLocation = leftGateWedge->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftGateWedge));

	//Right Gate Roof
	auto rightGateWedge = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightGateWedge->World, XMMatrixScaling(3.0f, 3.0f, 6.0f)*XMMatrixRotationRollPitchYaw(0.0f, XM_PI / 2, 0.0f)*XMMatrixTranslation(3.0f, 9.5f, -18.0f));
	XMStoreFloat4x4(&rightGateWedge->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightGateWedge->ObjCBIndex = objCBIndex++;
	rightGateWedge->Mat = mMaterials["wedgeMat"].get();
	rightGateWedge->Geo = mGeometries["shapeGeo"].get();
	rightGateWedge->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightGateWedge->IndexCount = rightGateWedge->Geo->DrawArgs["wedge"].IndexCount;
	rightGateWedge->StartIndexLocation = rightGateWedge->Geo->DrawArgs["wedge"].StartIndexLocation;
	rightGateWedge->BaseVertexLocation = rightGateWedge->Geo->DrawArgs["wedge"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightGateWedge));

	//Diamond Pedestal
	auto diamond1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamond1->World, XMMatrixScaling(1.0f, 3.0f, 1.0f)*XMMatrixTranslation(-5.0f, 0.0f, -8.0f));
	XMStoreFloat4x4(&diamond1->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamond1->ObjCBIndex = objCBIndex++;
	diamond1->Mat = mMaterials["skullMat"].get();
	diamond1->Geo = mGeometries["shapeGeo"].get();
	diamond1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamond1->IndexCount = diamond1->Geo->DrawArgs["diamond"].IndexCount;
	diamond1->StartIndexLocation = diamond1->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamond1->BaseVertexLocation = diamond1->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamond1));

	//Diamond Pedestal 2
	auto diamond2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamond2->World, XMMatrixScaling(1.0f, 3.0f, 1.0f)*XMMatrixTranslation(5.0f, 0.0f, -8.0f));
	XMStoreFloat4x4(&diamond2->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamond2->ObjCBIndex = objCBIndex++;
	diamond2->Mat = mMaterials["skullMat"].get();
	diamond2->Geo = mGeometries["shapeGeo"].get();
	diamond2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamond2->IndexCount = diamond2->Geo->DrawArgs["diamond"].IndexCount;
	diamond2->StartIndexLocation = diamond2->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamond2->BaseVertexLocation = diamond2->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamond2));

	//Torus
	auto torus = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&torus->World, XMMatrixScaling(0.75f, 0.75f, 0.75f)*XMMatrixRotationRollPitchYaw(XM_PI/2, 0.0f, 0.0f)*XMMatrixTranslation(5.0f, 4.1f, -8.0f));
	XMStoreFloat4x4(&torus->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	torus->ObjCBIndex = objCBIndex++;
	torus->Mat = mMaterials["gold"].get();
	torus->Geo = mGeometries["shapeGeo"].get();
	torus->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	torus->IndexCount = torus->Geo->DrawArgs["torus"].IndexCount;
	torus->StartIndexLocation = torus->Geo->DrawArgs["torus"].StartIndexLocation;
	torus->BaseVertexLocation = torus->Geo->DrawArgs["torus"].BaseVertexLocation;
	mAllRitems.push_back(std::move(torus));

	//Skull
	auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.2f, 0.2f, 0.2f)*XMMatrixTranslation(-5.0f, 3.0f, -8.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = objCBIndex++;
	skullRitem->Mat = mMaterials["diamond1Mat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	mAllRitems.push_back(std::move(skullRitem));


	
    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(40.0f, 40.0f, 1.0f));
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));
	
	
	
	/*

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	for(int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();
	
		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f);
	
		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);
	
		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["gold"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	
		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["gold"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	
		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["stone0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	
		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["stone0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	
		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}
	
	auto diamond1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamond1->World, XMMatrixScaling(0.4f, 0.6f, 0.4f)*XMMatrixTranslation(-0.65f, 2.6f, -0.65f));
	diamond1->TexTransform = MathHelper::Identity4x4();
	diamond1->ObjCBIndex = objCBIndex++;
	diamond1->Mat = mMaterials["diamond1Mat"].get();
	diamond1->Geo = mGeometries["shapeGeo"].get();
	diamond1->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamond1->IndexCount = diamond1->Geo->DrawArgs["diamond"].IndexCount;
	diamond1->StartIndexLocation = diamond1->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamond1->BaseVertexLocation = diamond1->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamond1));
	
	auto diamond2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamond2->World, XMMatrixScaling(0.4f, 0.6f, 0.4f)*XMMatrixTranslation(0.65f, 2.6f, -0.65f));
	diamond2->TexTransform = MathHelper::Identity4x4();
	diamond2->ObjCBIndex = objCBIndex++;
	diamond2->Mat = mMaterials["diamond2Mat"].get();
	diamond2->Geo = mGeometries["shapeGeo"].get();
	diamond2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamond2->IndexCount = diamond2->Geo->DrawArgs["diamond"].IndexCount;
	diamond2->StartIndexLocation = diamond2->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamond2->BaseVertexLocation = diamond2->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamond2));

	*/

	// All the render items are opaque.
	for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(1, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}
