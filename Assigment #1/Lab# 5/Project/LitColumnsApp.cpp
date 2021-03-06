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

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};

class LitColumnsApp : public D3DApp
{
public:
    LitColumnsApp(HINSTANCE hInstance);
    LitColumnsApp(const LitColumnsApp& rhs) = delete;
    LitColumnsApp& operator=(const LitColumnsApp& rhs) = delete;
    ~LitColumnsApp();

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

	void LoadTextures();
	void BuildDescriptorHeaps();

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildTreeSpritesGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
 
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;

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
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;
	//new sol
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f*XM_PI;
    float mPhi = 0.2f*XM_PI;
    float mRadius = 15.0f;

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
        LitColumnsApp theApp(hInstance);
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

LitColumnsApp::LitColumnsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

LitColumnsApp::~LitColumnsApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool LitColumnsApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
    
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
	BuildSkullGeometry();
	BuildTreeSpritesGeometry();
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
 
void LitColumnsApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void LitColumnsApp::Update(const GameTimer& gt)
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

	//AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void LitColumnsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

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

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);


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

void LitColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void LitColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void LitColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		//TODO
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
 
void LitColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
}
 
void LitColumnsApp::UpdateCamera(const GameTimer& gt)
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

void LitColumnsApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;

	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void LitColumnsApp::UpdateObjectCBs(const GameTimer& gt)
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

void LitColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
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

void LitColumnsApp::UpdateMainPassCB(const GameTimer& gt)
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
	mMainPassCB.AmbientLight = { 0.45f, 0.45f, 0.05f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void LitColumnsApp::LoadTextures()
{
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = "woodCrateTex";
	woodCrateTex->Filename = L"Textures/bricks2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodCrateTex->Filename.c_str(),
		woodCrateTex->Resource, woodCrateTex->UploadHeap));


	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));

	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);

}

void LitColumnsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Create root CBV.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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

void LitColumnsApp::BuildShadersAndInputLayout()
{
	
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	//TODO
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void LitColumnsApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.45f, 5.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(10.0f, 1.0f,40, 40);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(30.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	//GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT cylinderVertexOffset = (UINT)box.Vertices.size();
	UINT diamondVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT coneVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT wedgeVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT pyramidVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT torusVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT gridVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	//UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT cylinderIndexOffset = (UINT)box.Indices32.size();
	UINT diamondIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT coneIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT wedgeIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT pyramidIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT torusIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT gridIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	//UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		cylinder.Vertices.size() +
		diamond.Vertices.size() +
		cone.Vertices.size() +
		wedge.Vertices.size() +
		pyramid.Vertices.size() +
		torus.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size();
		

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;
	}

	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
		vertices[k].TexC = wedge.Vertices[i].TexC;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
		vertices[k].TexC = torus.Vertices[i].TexC;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}
	
	

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	

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
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	

	mGeometries[geo->Name] = std::move(geo);
}

void LitColumnsApp::BuildSkullGeometry()
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

void LitColumnsApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 16;
	std::array<TreeSpriteVertex, 16> vertices;
	for (UINT i = 0; i < treeCount; ++i)
	{
		int left = MathHelper::Rand(0, 1);
		int top = MathHelper::Rand(0, 1);
		float x;
		float z;

		if (left == 0)
		{
			x = MathHelper::RandF(15.0f, 45.0f);
			
		}
		else
		{
			x = MathHelper::RandF(-45.0f, -15.0f);
		}
		if (top == 0)
		{
			z = MathHelper::RandF(15.0f, 45.0f);
		}
		else
		{
			z = MathHelper::RandF(-45.0f, -15.0f);
		}
		
		float y = -5.5f;
			//GetHillsHeight(x, z);

		// Move tree slightly above land height.
		y += 8.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(5.0f, 5.0f);
	}

	std::array<std::uint16_t, 16> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void LitColumnsApp::BuildPSOs()
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
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
    //ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));

	//
	// PSO for transparent objects
	//
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));
	
	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void LitColumnsApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void LitColumnsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 5;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto woodCrateTex = mTextures["woodCrateTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto grassTex = mTextures["grassTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	//desc
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	srvDesc.Format = stoneTex->GetDesc().Format;
	//upload
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);
	
	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	//desc
	srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
	srvDesc.Format = waterTex->GetDesc().Format;
	//upload
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	//desc
	srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
	srvDesc.Format = grassTex->GetDesc().Format;
	//upload
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	//desc
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	//upload
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
	
	
}

void LitColumnsApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	bricks0->Roughness = 0.1f;


	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.1f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.2f;

	/*auto diamondMat = std::make_unique<Material>();
	diamondMat->Name = "diamondMat";
	diamondMat->MatCBIndex = 3;
	diamondMat->DiffuseSrvHeapIndex = 3;
	diamondMat->DiffuseAlbedo = XMFLOAT4(Colors::Blue);
	diamondMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	diamondMat->Roughness = 0.3f;

	auto diamondMatRed = std::make_unique<Material>();
	diamondMatRed->Name = "diamondMatRed";
	diamondMatRed->MatCBIndex = 4;
	diamondMatRed->DiffuseSrvHeapIndex = 4;
	diamondMatRed->DiffuseAlbedo = XMFLOAT4(Colors::Red);
	diamondMatRed->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	diamondMatRed->Roughness = 0.3f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 5;
	skullMat->DiffuseSrvHeapIndex = 5;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	skullMat->Roughness = 0.3f;*/

	auto grassMat = std::make_unique<Material>();
	grassMat->Name = "grassMat";
	grassMat->MatCBIndex = 3;
	grassMat->DiffuseSrvHeapIndex = 3;
	grassMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grassMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	grassMat->Roughness = 0.3f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 4;
	treeSprites->DiffuseSrvHeapIndex = 4;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;
	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	//mMaterials["diamondMat"] = std::move(diamondMat);
	//mMaterials["diamondMatRed"] = std::move(diamondMatRed);
	//mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["grassMat"] = std::move(grassMat);
	mMaterials["treeSprites"] = std::move(treeSprites);
}

void LitColumnsApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(10.0f, 4.0f, 10.0f)*XMMatrixTranslation(0.0f, 2.0f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = mMaterials["bricks0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	auto moatRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&moatRitem->World, XMMatrixScaling(1.f, 1.f, .1f)*XMMatrixRotationX(XM_PI / 2)*XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&moatRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	moatRitem->ObjCBIndex = 1;
	moatRitem->Mat = mMaterials["tile0"].get();
	moatRitem->Geo = mGeometries["shapeGeo"].get();
	moatRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	moatRitem->IndexCount = moatRitem->Geo->DrawArgs["torus"].IndexCount;
	moatRitem->StartIndexLocation = moatRitem->Geo->DrawArgs["torus"].StartIndexLocation;
	moatRitem->BaseVertexLocation = moatRitem->Geo->DrawArgs["torus"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(moatRitem.get());
	mAllRitems.push_back(std::move(moatRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	//gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(4.0f, 1.0f, 4.0f)*XMMatrixRotationX(0.0f)*XMMatrixTranslation(0.0f, 0.0f, 0.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(4.0f, 1.0f, 4.0f));
	gridRitem->ObjCBIndex = 2;
	gridRitem->Mat = mMaterials["grassMat"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	auto centerCylinderRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&centerCylinderRitem->World, XMMatrixScaling(50.0f, 1.0f, 2.0f)*XMMatrixRotationX(0.0f)*XMMatrixTranslation(0.0f, 50.0f, 0.0f));
	XMStoreFloat4x4(&centerCylinderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	centerCylinderRitem->ObjCBIndex = 3;
	centerCylinderRitem->Mat = mMaterials["stone0"].get();
	centerCylinderRitem->Geo = mGeometries["shapeGeo"].get();
	centerCylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	centerCylinderRitem->IndexCount = centerCylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	centerCylinderRitem->StartIndexLocation = centerCylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	centerCylinderRitem->BaseVertexLocation = centerCylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(centerCylinderRitem.get());
	mAllRitems.push_back(std::move(centerCylinderRitem));

	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(0.2f, 0.2f, 0.2f)*XMMatrixRotationX(80.5)*XMMatrixTranslation(-0.7f, 2.5f, -0.7f));
	XMStoreFloat4x4(&diamondRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamondRitem->ObjCBIndex = 4;
	diamondRitem->Mat = mMaterials["stone0"].get();
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(diamondRitem.get());
	mAllRitems.push_back(std::move(diamondRitem));

	auto diamondLeftRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondLeftRitem->World, XMMatrixScaling(0.2f, 0.2f, 0.2f)*XMMatrixRotationX(80.5)*XMMatrixTranslation(0.7f, 2.5f, -0.7f));
	XMStoreFloat4x4(&diamondLeftRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamondLeftRitem->ObjCBIndex = 5;
	diamondLeftRitem->Mat = mMaterials["stone0"].get();
	diamondLeftRitem->Geo = mGeometries["shapeGeo"].get();
	diamondLeftRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondLeftRitem->IndexCount = diamondLeftRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondLeftRitem->StartIndexLocation = diamondLeftRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondLeftRitem->BaseVertexLocation = diamondLeftRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(diamondLeftRitem.get());
	mAllRitems.push_back(std::move(diamondLeftRitem));

	auto coneRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)*XMMatrixRotationX(0.0f)*XMMatrixTranslation(0.0f, 6.0f, 0.0f));
	XMStoreFloat4x4(&coneRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	coneRitem->ObjCBIndex = 6;
	coneRitem->Mat = mMaterials["stone0"].get();
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(coneRitem.get());
	mAllRitems.push_back(std::move(coneRitem));

	auto flagCylinderRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&flagCylinderRitem->World, XMMatrixScaling(.1f, 1.0f, .1f)*XMMatrixRotationX(0.0f)*XMMatrixTranslation(0.0f, 5.0f, 0.0f));
	XMStoreFloat4x4(&flagCylinderRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	flagCylinderRitem->ObjCBIndex = 7;
	flagCylinderRitem->Mat = mMaterials["stone0"].get();
	flagCylinderRitem->Geo = mGeometries["shapeGeo"].get();
	flagCylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	flagCylinderRitem->IndexCount = flagCylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	flagCylinderRitem->StartIndexLocation = flagCylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	flagCylinderRitem->BaseVertexLocation = flagCylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(flagCylinderRitem.get());
	mAllRitems.push_back(std::move(flagCylinderRitem));

	auto flagBoxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&flagBoxRitem->World, XMMatrixScaling(1.0f, .4f, .1f)*XMMatrixRotationX(0.0f)*XMMatrixTranslation(-.5f, 7.25f, 0.0f));
	XMStoreFloat4x4(&flagBoxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	flagBoxRitem->ObjCBIndex = 8;
	flagBoxRitem->Mat = mMaterials["stone0"].get();
	flagBoxRitem->Geo = mGeometries["shapeGeo"].get();
	flagBoxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	flagBoxRitem->IndexCount = flagBoxRitem->Geo->DrawArgs["box"].IndexCount;
	flagBoxRitem->StartIndexLocation = flagBoxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	flagBoxRitem->BaseVertexLocation = flagBoxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(flagBoxRitem.get());
	mAllRitems.push_back(std::move(flagBoxRitem));

	auto doorRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&doorRitem->World, XMMatrixScaling(1.f, .01, 4.f)*XMMatrixRotationX(XM_PI/2)*XMMatrixTranslation(0.0f, 0.0f, -5.0f));
	XMStoreFloat4x4(&doorRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	doorRitem->ObjCBIndex = 9;
	doorRitem->Mat = mMaterials["tile0"].get();
	doorRitem->Geo = mGeometries["shapeGeo"].get();
	doorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	doorRitem->IndexCount = doorRitem->Geo->DrawArgs["cylinder"].IndexCount;
	doorRitem->StartIndexLocation = doorRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	doorRitem->BaseVertexLocation = doorRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(doorRitem.get());
	mAllRitems.push_back(std::move(doorRitem));

	auto bridgeRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&bridgeRitem->World, XMMatrixScaling(1.0f, 0.04f, 10.0f)*XMMatrixRotationX(0.0f)*XMMatrixTranslation(0.0f, 0.1f, -6.0f));
	XMStoreFloat4x4(&bridgeRitem->TexTransform, XMMatrixScaling(1.0f, .04f, 10.0f));
	bridgeRitem->ObjCBIndex = 10;
	bridgeRitem->Mat = mMaterials["bricks0"].get();
	bridgeRitem->Geo = mGeometries["shapeGeo"].get();
	bridgeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	bridgeRitem->IndexCount = bridgeRitem->Geo->DrawArgs["box"].IndexCount;
	bridgeRitem->StartIndexLocation = bridgeRitem->Geo->DrawArgs["box"].StartIndexLocation;
	bridgeRitem->BaseVertexLocation = bridgeRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(bridgeRitem.get());
	mAllRitems.push_back(std::move(bridgeRitem));

	auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)*XMMatrixTranslation(0.0f, .5f, 0.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 11;
	skullRitem->Mat = mMaterials["stone0"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
	mAllRitems.push_back(std::move(skullRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objCBIndex = 12;
	for (int i = 0; i < 8; ++i)
	{
		auto leftPyramidRitem = std::make_unique<RenderItem>();
		auto rightPyramidRitem = std::make_unique<RenderItem>();
		auto backPyramidRitem = std::make_unique<RenderItem>();
		auto frontPyramidRitem = std::make_unique<RenderItem>();

		XMMATRIX leftPyramidWorld =  XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(0.0)*XMMatrixTranslation(-4.5f, 4.5f, 3.5f - (i* 1.0f));
		XMMATRIX rightPyramidWorld = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(0.0)*XMMatrixTranslation(4.5f , 4.5f, -3.5f + (i* 1.0f));
		XMMATRIX backPyramidWorld =  XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(0.0)*XMMatrixTranslation( -3.5f + (i* 1.0f), 4.5f, 4.5f );
		XMMATRIX frontPyramidWorld = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(0.0)*XMMatrixTranslation( 3.5f - (i* 1.0f), 4.5f, -4.5f);
		
		XMStoreFloat4x4(&leftPyramidRitem->World, leftPyramidWorld);
		XMStoreFloat4x4(&leftPyramidRitem->TexTransform, brickTexTransform);
		leftPyramidRitem->ObjCBIndex = objCBIndex++;
		leftPyramidRitem->Mat = mMaterials["stone0"].get();
		leftPyramidRitem->Geo = mGeometries["shapeGeo"].get();
		leftPyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftPyramidRitem->IndexCount = leftPyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
		leftPyramidRitem->StartIndexLocation = leftPyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		leftPyramidRitem->BaseVertexLocation = leftPyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftPyramidRitem.get());

		XMStoreFloat4x4(&rightPyramidRitem->World, rightPyramidWorld);
		XMStoreFloat4x4(&rightPyramidRitem->TexTransform, brickTexTransform);
		rightPyramidRitem->ObjCBIndex = objCBIndex++;
		rightPyramidRitem->Mat = mMaterials["stone0"].get();
		rightPyramidRitem->Geo = mGeometries["shapeGeo"].get();
		rightPyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightPyramidRitem->IndexCount = rightPyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
		rightPyramidRitem->StartIndexLocation = rightPyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		rightPyramidRitem->BaseVertexLocation = rightPyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightPyramidRitem.get());

		XMStoreFloat4x4(&backPyramidRitem->World, backPyramidWorld);
		XMStoreFloat4x4(&backPyramidRitem->TexTransform, brickTexTransform);
		backPyramidRitem->ObjCBIndex = objCBIndex++;
		backPyramidRitem->Mat = mMaterials["stone0"].get();
		backPyramidRitem->Geo = mGeometries["shapeGeo"].get();
		backPyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		backPyramidRitem->IndexCount = backPyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
		backPyramidRitem->StartIndexLocation = backPyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		backPyramidRitem->BaseVertexLocation = backPyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(backPyramidRitem.get());

		XMStoreFloat4x4(&frontPyramidRitem->World, frontPyramidWorld);
		XMStoreFloat4x4(&frontPyramidRitem->TexTransform, brickTexTransform);
		frontPyramidRitem->ObjCBIndex = objCBIndex++;
		frontPyramidRitem->Mat = mMaterials["stone0"].get();
		frontPyramidRitem->Geo = mGeometries["shapeGeo"].get();
		frontPyramidRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		frontPyramidRitem->IndexCount = frontPyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
		frontPyramidRitem->StartIndexLocation = frontPyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
		frontPyramidRitem->BaseVertexLocation = frontPyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(frontPyramidRitem.get());

		mAllRitems.push_back(std::move(leftPyramidRitem));
		mAllRitems.push_back(std::move(rightPyramidRitem));
		mAllRitems.push_back(std::move(backPyramidRitem));
		mAllRitems.push_back(std::move(frontPyramidRitem));
	}


//	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	objCBIndex = 44;
	for (int i = 0; i < 2; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();

		auto rightWedge1Ritem = std::make_unique<RenderItem>();
		auto rightWedge2Ritem = std::make_unique<RenderItem>();
		auto rightWedge3Ritem = std::make_unique<RenderItem>();
		auto rightWedge4Ritem = std::make_unique<RenderItem>();

		auto leftWedge1Ritem = std::make_unique<RenderItem>();
		auto leftWedge2Ritem = std::make_unique<RenderItem>();
		auto leftWedge3Ritem = std::make_unique<RenderItem>();
		auto leftWedge4Ritem = std::make_unique<RenderItem>();

		auto leftSpire1Ritem = std::make_unique<RenderItem>();
		auto leftSpire2Ritem = std::make_unique<RenderItem>();
		auto leftSpire3Ritem = std::make_unique<RenderItem>();
		auto leftSpire4Ritem = std::make_unique<RenderItem>();
		auto leftSpire5Ritem = std::make_unique<RenderItem>();
		auto leftSpire6Ritem = std::make_unique<RenderItem>();
		auto leftSpire7Ritem = std::make_unique<RenderItem>();
		auto leftSpire8Ritem = std::make_unique<RenderItem>();

		auto rightSpire1Ritem = std::make_unique<RenderItem>();
		auto rightSpire2Ritem = std::make_unique<RenderItem>();
		auto rightSpire3Ritem = std::make_unique<RenderItem>();
		auto rightSpire4Ritem = std::make_unique<RenderItem>();
		auto rightSpire5Ritem = std::make_unique<RenderItem>();
		auto rightSpire6Ritem = std::make_unique<RenderItem>();
		auto rightSpire7Ritem = std::make_unique<RenderItem>();
		auto rightSpire8Ritem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 2.0f, -5.0f + i*10.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 2.0f, -5.0f + i*10.0f);

		XMMATRIX rightWedge1World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(0.0f)*				XMMatrixTranslation(-5.5f, 4.0f, -5.0f + i*10.0f);
		XMMATRIX rightWedge2World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(XM_PI / 2)*		XMMatrixTranslation(-5.0f, 4.0f, -4.5f + i*10.0f);
		XMMATRIX rightWedge3World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(XM_PI)*			XMMatrixTranslation(-4.5f, 4.0f, -5.0f + i*10.0f);
		XMMATRIX rightWedge4World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(XM_PI + XM_PI / 2)*XMMatrixTranslation(-5.0f, 4.0f, -5.5f + i*10.0f);

		XMMATRIX leftWedge1World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(4.5f, 4.0f, -5.0f + i*10.0f);
		XMMATRIX leftWedge2World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(XM_PI / 2)*XMMatrixTranslation(5.0f, 4.0f, -4.5f + i*10.0f);
		XMMATRIX leftWedge3World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(XM_PI)*XMMatrixTranslation(5.5f, 4.0f, -5.0f + i*10.0f);
		XMMATRIX leftWedge4World = XMMatrixScaling(1.f, 1.f, 1.f)*XMMatrixRotationY(XM_PI + XM_PI / 2)*XMMatrixTranslation(5.0f, 4.0f, -5.5f + i*10.0f);

		XMMATRIX rightSpire1World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-4.25f, 4.75f, -5.25f + i*10.0f);
		XMMATRIX rightSpire2World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-4.25f, 4.75f, -4.75f + i*10.0f);
		XMMATRIX rightSpire3World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-5.25f, 4.75f, -5.75f + i*10.0f);
		XMMATRIX rightSpire4World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-4.75f, 4.75f, -5.75f + i*10.0f);
		XMMATRIX rightSpire5World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-5.75f, 4.75f, -5.25f + i*10.0f);
		XMMATRIX rightSpire6World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-5.75f, 4.75f, -4.75f + i*10.0f);
		XMMATRIX rightSpire7World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-5.25f, 4.75f, -4.25f + i*10.0f);
		XMMATRIX rightSpire8World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(-4.75f, 4.75f, -4.25f + i*10.0f);

		XMMATRIX leftSpire1World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(4.25f, 4.75f, -5.25f + i*10.0f);
		XMMATRIX leftSpire2World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(4.25f, 4.75f, -4.75f + i*10.0f);
		XMMATRIX leftSpire3World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(5.25f, 4.75f, -5.75f + i*10.0f);
		XMMATRIX leftSpire4World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(4.75f, 4.75f, -5.75f + i*10.0f);
		XMMATRIX leftSpire5World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(5.75f, 4.75f, -5.25f + i*10.0f);
		XMMATRIX leftSpire6World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(5.75f, 4.75f, -4.75f + i*10.0f);
		XMMATRIX leftSpire7World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(5.25f, 4.75f, -4.25f + i*10.0f);
		XMMATRIX leftSpire8World = XMMatrixScaling(.25f, .5f, .25f)*XMMatrixRotationY(0.0f)*XMMatrixTranslation(4.75f, 4.75f, -4.25f + i*10.0f);


		XMStoreFloat4x4(&rightWedge1Ritem->World, rightWedge1World);
		XMStoreFloat4x4(&rightWedge1Ritem->TexTransform, brickTexTransform);
		rightWedge1Ritem->ObjCBIndex = objCBIndex++;
		rightWedge1Ritem->Mat = mMaterials["stone0"].get();
		rightWedge1Ritem->Geo = mGeometries["shapeGeo"].get();
		rightWedge1Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightWedge1Ritem->IndexCount = rightWedge1Ritem->Geo->DrawArgs["wedge"].IndexCount;
		rightWedge1Ritem->StartIndexLocation = rightWedge1Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightWedge1Ritem->BaseVertexLocation = rightWedge1Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightWedge1Ritem.get());

		XMStoreFloat4x4(&rightWedge2Ritem->World, rightWedge2World);
		XMStoreFloat4x4(&rightWedge2Ritem->TexTransform, brickTexTransform);
		rightWedge2Ritem->ObjCBIndex = objCBIndex++;
		rightWedge2Ritem->Mat = mMaterials["stone0"].get();
		rightWedge2Ritem->Geo = mGeometries["shapeGeo"].get();
		rightWedge2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightWedge2Ritem->IndexCount = rightWedge2Ritem->Geo->DrawArgs["wedge"].IndexCount;
		rightWedge2Ritem->StartIndexLocation = rightWedge2Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightWedge2Ritem->BaseVertexLocation = rightWedge2Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightWedge2Ritem.get());

		XMStoreFloat4x4(&rightWedge3Ritem->World, rightWedge3World);
		XMStoreFloat4x4(&rightWedge3Ritem->TexTransform, brickTexTransform);
		rightWedge3Ritem->ObjCBIndex = objCBIndex++;
		rightWedge3Ritem->Mat = mMaterials["stone0"].get();
		rightWedge3Ritem->Geo = mGeometries["shapeGeo"].get();
		rightWedge3Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightWedge3Ritem->IndexCount = rightWedge3Ritem->Geo->DrawArgs["wedge"].IndexCount;
		rightWedge3Ritem->StartIndexLocation = rightWedge3Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightWedge3Ritem->BaseVertexLocation = rightWedge3Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightWedge3Ritem.get());

		XMStoreFloat4x4(&rightWedge4Ritem->World, rightWedge4World);
		XMStoreFloat4x4(&rightWedge4Ritem->TexTransform, brickTexTransform);
		rightWedge4Ritem->ObjCBIndex = objCBIndex++;
		rightWedge4Ritem->Mat = mMaterials["stone0"].get();
		rightWedge4Ritem->Geo = mGeometries["shapeGeo"].get();
		rightWedge4Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightWedge4Ritem->IndexCount = rightWedge4Ritem->Geo->DrawArgs["wedge"].IndexCount;
		rightWedge4Ritem->StartIndexLocation = rightWedge4Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		rightWedge4Ritem->BaseVertexLocation = rightWedge4Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightWedge4Ritem.get());

		XMStoreFloat4x4(&leftWedge1Ritem->World, leftWedge1World);
		XMStoreFloat4x4(&leftWedge1Ritem->TexTransform, brickTexTransform);
		leftWedge1Ritem->ObjCBIndex = objCBIndex++;
		leftWedge1Ritem->Mat = mMaterials["stone0"].get();
		leftWedge1Ritem->Geo = mGeometries["shapeGeo"].get();
		leftWedge1Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftWedge1Ritem->IndexCount = leftWedge1Ritem->Geo->DrawArgs["wedge"].IndexCount;
		leftWedge1Ritem->StartIndexLocation = leftWedge1Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftWedge1Ritem->BaseVertexLocation = leftWedge1Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftWedge1Ritem.get());

		XMStoreFloat4x4(&leftWedge2Ritem->World, leftWedge2World);
		XMStoreFloat4x4(&leftWedge2Ritem->TexTransform, brickTexTransform);
		leftWedge2Ritem->ObjCBIndex = objCBIndex++;
		leftWedge2Ritem->Mat = mMaterials["stone0"].get();
		leftWedge2Ritem->Geo = mGeometries["shapeGeo"].get();
		leftWedge2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftWedge2Ritem->IndexCount = leftWedge2Ritem->Geo->DrawArgs["wedge"].IndexCount;
		leftWedge2Ritem->StartIndexLocation = leftWedge2Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftWedge2Ritem->BaseVertexLocation = leftWedge2Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftWedge2Ritem.get());

		XMStoreFloat4x4(&leftWedge3Ritem->World, leftWedge3World);
		XMStoreFloat4x4(&leftWedge3Ritem->TexTransform, brickTexTransform);
		leftWedge3Ritem->ObjCBIndex = objCBIndex++;
		leftWedge3Ritem->Mat = mMaterials["stone0"].get();
		leftWedge3Ritem->Geo = mGeometries["shapeGeo"].get();
		leftWedge3Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftWedge3Ritem->IndexCount = leftWedge3Ritem->Geo->DrawArgs["wedge"].IndexCount;
		leftWedge3Ritem->StartIndexLocation = leftWedge3Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftWedge3Ritem->BaseVertexLocation = leftWedge3Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftWedge3Ritem.get());

		XMStoreFloat4x4(&leftWedge4Ritem->World, leftWedge4World);
		XMStoreFloat4x4(&leftWedge4Ritem->TexTransform, brickTexTransform);
		leftWedge4Ritem->ObjCBIndex = objCBIndex++;
		leftWedge4Ritem->Mat = mMaterials["stone0"].get();
		leftWedge4Ritem->Geo = mGeometries["shapeGeo"].get();
		leftWedge4Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftWedge4Ritem->IndexCount = leftWedge4Ritem->Geo->DrawArgs["wedge"].IndexCount;
		leftWedge4Ritem->StartIndexLocation = leftWedge4Ritem->Geo->DrawArgs["wedge"].StartIndexLocation;
		leftWedge4Ritem->BaseVertexLocation = leftWedge4Ritem->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftWedge4Ritem.get());

		XMStoreFloat4x4(&leftSpire1Ritem->World, leftSpire1World);
		XMStoreFloat4x4(&leftSpire1Ritem->TexTransform, brickTexTransform);
		leftSpire1Ritem->ObjCBIndex = objCBIndex++;
		leftSpire1Ritem->Mat = mMaterials["stone0"].get();
		leftSpire1Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire1Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire1Ritem->IndexCount = leftSpire1Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire1Ritem->StartIndexLocation = leftSpire1Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire1Ritem->BaseVertexLocation = leftSpire1Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire1Ritem.get());

		XMStoreFloat4x4(&leftSpire2Ritem->World, leftSpire2World);
		XMStoreFloat4x4(&leftSpire2Ritem->TexTransform, brickTexTransform);
		leftSpire2Ritem->ObjCBIndex = objCBIndex++;
		leftSpire2Ritem->Mat = mMaterials["stone0"].get();
		leftSpire2Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire2Ritem->IndexCount = leftSpire2Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire2Ritem->StartIndexLocation = leftSpire2Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire2Ritem->BaseVertexLocation = leftSpire2Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire2Ritem.get());

		XMStoreFloat4x4(&leftSpire3Ritem->World, leftSpire3World);
		XMStoreFloat4x4(&leftSpire3Ritem->TexTransform, brickTexTransform);
		leftSpire3Ritem->ObjCBIndex = objCBIndex++;
		leftSpire3Ritem->Mat = mMaterials["stone0"].get();
		leftSpire3Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire3Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire3Ritem->IndexCount = leftSpire3Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire3Ritem->StartIndexLocation = leftSpire3Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire3Ritem->BaseVertexLocation = leftSpire3Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire3Ritem.get());

		XMStoreFloat4x4(&leftSpire4Ritem->World, leftSpire4World);
		XMStoreFloat4x4(&leftSpire4Ritem->TexTransform, brickTexTransform);
		leftSpire4Ritem->ObjCBIndex = objCBIndex++;
		leftSpire4Ritem->Mat = mMaterials["stone0"].get();
		leftSpire4Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire4Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire4Ritem->IndexCount = leftSpire4Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire4Ritem->StartIndexLocation = leftSpire4Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire4Ritem->BaseVertexLocation = leftSpire4Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire4Ritem.get());

		XMStoreFloat4x4(&leftSpire5Ritem->World, leftSpire5World);
		XMStoreFloat4x4(&leftSpire5Ritem->TexTransform, brickTexTransform);
		leftSpire5Ritem->ObjCBIndex = objCBIndex++;
		leftSpire5Ritem->Mat = mMaterials["stone0"].get();
		leftSpire5Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire5Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire5Ritem->IndexCount = leftSpire5Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire5Ritem->StartIndexLocation = leftSpire5Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire5Ritem->BaseVertexLocation = leftSpire5Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire5Ritem.get());

		XMStoreFloat4x4(&leftSpire6Ritem->World, leftSpire6World);
		XMStoreFloat4x4(&leftSpire6Ritem->TexTransform, brickTexTransform);
		leftSpire6Ritem->ObjCBIndex = objCBIndex++;
		leftSpire6Ritem->Mat = mMaterials["stone0"].get();
		leftSpire6Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire6Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire6Ritem->IndexCount = leftSpire6Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire6Ritem->StartIndexLocation = leftSpire6Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire6Ritem->BaseVertexLocation = leftSpire6Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire6Ritem.get());

		XMStoreFloat4x4(&leftSpire7Ritem->World, leftSpire7World);
		XMStoreFloat4x4(&leftSpire7Ritem->TexTransform, brickTexTransform);
		leftSpire7Ritem->ObjCBIndex = objCBIndex++;
		leftSpire7Ritem->Mat = mMaterials["stone0"].get();
		leftSpire7Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire7Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire7Ritem->IndexCount = leftSpire7Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire7Ritem->StartIndexLocation = leftSpire7Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire7Ritem->BaseVertexLocation = leftSpire7Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire7Ritem.get());

		XMStoreFloat4x4(&leftSpire8Ritem->World, leftSpire8World);
		XMStoreFloat4x4(&leftSpire8Ritem->TexTransform, brickTexTransform);
		leftSpire8Ritem->ObjCBIndex = objCBIndex++;
		leftSpire8Ritem->Mat = mMaterials["stone0"].get();
		leftSpire8Ritem->Geo = mGeometries["shapeGeo"].get();
		leftSpire8Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSpire8Ritem->IndexCount = leftSpire8Ritem->Geo->DrawArgs["box"].IndexCount;
		leftSpire8Ritem->StartIndexLocation = leftSpire8Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		leftSpire8Ritem->BaseVertexLocation = leftSpire8Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSpire8Ritem.get());

		XMStoreFloat4x4(&rightSpire1Ritem->World, rightSpire1World);
		XMStoreFloat4x4(&rightSpire1Ritem->TexTransform, brickTexTransform);
		rightSpire1Ritem->ObjCBIndex = objCBIndex++;
		rightSpire1Ritem->Mat = mMaterials["stone0"].get();
		rightSpire1Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire1Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire1Ritem->IndexCount = rightSpire1Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire1Ritem->StartIndexLocation = rightSpire1Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire1Ritem->BaseVertexLocation = rightSpire1Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire1Ritem.get());

		XMStoreFloat4x4(&rightSpire2Ritem->World, rightSpire2World);
		XMStoreFloat4x4(&rightSpire2Ritem->TexTransform, brickTexTransform);
		rightSpire2Ritem->ObjCBIndex = objCBIndex++;
		rightSpire2Ritem->Mat = mMaterials["stone0"].get();
		rightSpire2Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire2Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire2Ritem->IndexCount = rightSpire2Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire2Ritem->StartIndexLocation = rightSpire2Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire2Ritem->BaseVertexLocation = rightSpire2Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire2Ritem.get());

		XMStoreFloat4x4(&rightSpire3Ritem->World, rightSpire3World);
		XMStoreFloat4x4(&rightSpire3Ritem->TexTransform, brickTexTransform);
		rightSpire3Ritem->ObjCBIndex = objCBIndex++;
		rightSpire3Ritem->Mat = mMaterials["stone0"].get();
		rightSpire3Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire3Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire3Ritem->IndexCount = rightSpire3Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire3Ritem->StartIndexLocation = rightSpire3Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire3Ritem->BaseVertexLocation = rightSpire3Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire3Ritem.get());

		XMStoreFloat4x4(&rightSpire4Ritem->World, rightSpire4World);
		XMStoreFloat4x4(&rightSpire4Ritem->TexTransform, brickTexTransform);
		rightSpire4Ritem->ObjCBIndex = objCBIndex++;
		rightSpire4Ritem->Mat = mMaterials["stone0"].get();
		rightSpire4Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire4Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire4Ritem->IndexCount = rightSpire4Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire4Ritem->StartIndexLocation = rightSpire4Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire4Ritem->BaseVertexLocation = rightSpire4Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire4Ritem.get());

		XMStoreFloat4x4(&rightSpire5Ritem->World, rightSpire5World);
		XMStoreFloat4x4(&rightSpire5Ritem->TexTransform, brickTexTransform);
		rightSpire5Ritem->ObjCBIndex = objCBIndex++;
		rightSpire5Ritem->Mat = mMaterials["stone0"].get();
		rightSpire5Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire5Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire5Ritem->IndexCount = rightSpire5Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire5Ritem->StartIndexLocation = rightSpire5Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire5Ritem->BaseVertexLocation = rightSpire5Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire5Ritem.get());

		XMStoreFloat4x4(&rightSpire6Ritem->World, rightSpire6World);
		XMStoreFloat4x4(&rightSpire6Ritem->TexTransform, brickTexTransform);
		rightSpire6Ritem->ObjCBIndex = objCBIndex++;
		rightSpire6Ritem->Mat = mMaterials["stone0"].get();
		rightSpire6Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire6Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire6Ritem->IndexCount = rightSpire6Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire6Ritem->StartIndexLocation = rightSpire6Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire6Ritem->BaseVertexLocation = rightSpire6Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire6Ritem.get());

		XMStoreFloat4x4(&rightSpire7Ritem->World, rightSpire7World);
		XMStoreFloat4x4(&rightSpire7Ritem->TexTransform, brickTexTransform);
		rightSpire7Ritem->ObjCBIndex = objCBIndex++;
		rightSpire7Ritem->Mat = mMaterials["stone0"].get();
		rightSpire7Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire7Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire7Ritem->IndexCount = rightSpire7Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire7Ritem->StartIndexLocation = rightSpire7Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire7Ritem->BaseVertexLocation = rightSpire7Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire7Ritem.get());

		XMStoreFloat4x4(&rightSpire8Ritem->World, rightSpire8World);
		XMStoreFloat4x4(&rightSpire8Ritem->TexTransform, brickTexTransform);
		rightSpire8Ritem->ObjCBIndex = objCBIndex++;
		rightSpire8Ritem->Mat = mMaterials["stone0"].get();
		rightSpire8Ritem->Geo = mGeometries["shapeGeo"].get();
		rightSpire8Ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSpire8Ritem->IndexCount = rightSpire8Ritem->Geo->DrawArgs["box"].IndexCount;
		rightSpire8Ritem->StartIndexLocation = rightSpire8Ritem->Geo->DrawArgs["box"].StartIndexLocation;
		rightSpire8Ritem->BaseVertexLocation = rightSpire8Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSpire8Ritem.get());

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["stone0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["stone0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());

		mAllRitems.push_back(std::move(rightWedge1Ritem));
		mAllRitems.push_back(std::move(rightWedge2Ritem));
		mAllRitems.push_back(std::move(rightWedge3Ritem));
		mAllRitems.push_back(std::move(rightWedge4Ritem));
		mAllRitems.push_back(std::move(leftWedge1Ritem));
		mAllRitems.push_back(std::move(leftWedge2Ritem));
		mAllRitems.push_back(std::move(leftWedge3Ritem));
		mAllRitems.push_back(std::move(leftWedge4Ritem));
		mAllRitems.push_back(std::move(leftSpire1Ritem));
		mAllRitems.push_back(std::move(leftSpire2Ritem));
		mAllRitems.push_back(std::move(leftSpire3Ritem));
		mAllRitems.push_back(std::move(leftSpire4Ritem));
		mAllRitems.push_back(std::move(leftSpire5Ritem));
		mAllRitems.push_back(std::move(leftSpire6Ritem));
		mAllRitems.push_back(std::move(leftSpire7Ritem));
		mAllRitems.push_back(std::move(leftSpire8Ritem));
		mAllRitems.push_back(std::move(rightSpire1Ritem));
		mAllRitems.push_back(std::move(rightSpire2Ritem));
		mAllRitems.push_back(std::move(rightSpire3Ritem));
		mAllRitems.push_back(std::move(rightSpire4Ritem));
		mAllRitems.push_back(std::move(rightSpire5Ritem));
		mAllRitems.push_back(std::move(rightSpire6Ritem));
		mAllRitems.push_back(std::move(rightSpire7Ritem));
		mAllRitems.push_back(std::move(rightSpire8Ritem));
		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
	}

	objCBIndex = 44 + 52;
	float offset = 0;
	
	float scale = 5.0f;
	float wallWidth = 0.5f;
	int steps = 6;
	float outterOffset = scale * steps;
	
	float vert;
	float hor;

	
	///border
	for (int i = 0; i < steps; ++i)
	{
		auto MazeWallTop = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&MazeWallTop->World, XMMatrixScaling(scale, wallWidth, wallWidth)*XMMatrixTranslation(offset + scale/2 - outterOffset / 2, 0.0f, 0.0f - outterOffset / 2));
		XMStoreFloat4x4(&MazeWallTop->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		MazeWallTop->ObjCBIndex = objCBIndex;
		MazeWallTop->Mat = mMaterials["stone0"].get();
		MazeWallTop->Geo = mGeometries["shapeGeo"].get();
		MazeWallTop->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		MazeWallTop->IndexCount = MazeWallTop->Geo->DrawArgs["box"].IndexCount;
		MazeWallTop->StartIndexLocation = MazeWallTop->Geo->DrawArgs["box"].StartIndexLocation;
		MazeWallTop->BaseVertexLocation = MazeWallTop->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWallTop.get());
		mAllRitems.push_back(std::move(MazeWallTop));

		objCBIndex++;

		auto MazeWallBot = std::make_unique<RenderItem>();
		
		XMStoreFloat4x4(&MazeWallBot->World, XMMatrixScaling(scale, wallWidth, wallWidth)*XMMatrixTranslation(offset + scale/2 - outterOffset / 2, 0.0f, outterOffset - outterOffset / 2));
		XMStoreFloat4x4(&MazeWallBot->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		MazeWallBot->ObjCBIndex = objCBIndex;
		MazeWallBot->Mat = mMaterials["stone0"].get();
		MazeWallBot->Geo = mGeometries["shapeGeo"].get();
		MazeWallBot->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		MazeWallBot->IndexCount = MazeWallBot->Geo->DrawArgs["box"].IndexCount;
		MazeWallBot->StartIndexLocation = MazeWallBot->Geo->DrawArgs["box"].StartIndexLocation;
		MazeWallBot->BaseVertexLocation = MazeWallBot->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWallBot.get());
		mAllRitems.push_back(std::move(MazeWallBot));

		offset += scale;
		objCBIndex++;
	}

	offset = 0.0f;

	for (int i = 0; i < steps; ++i)
	{
		auto MazeWallRight = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&MazeWallRight->World, XMMatrixScaling(wallWidth, wallWidth, scale)*XMMatrixTranslation(0.0f - outterOffset / 2.0f, 0.0f, offset + scale/2.0f - outterOffset / 2.0f));
		XMStoreFloat4x4(&MazeWallRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		MazeWallRight->ObjCBIndex = objCBIndex;
		MazeWallRight->Mat = mMaterials["stone0"].get();
		MazeWallRight->Geo = mGeometries["shapeGeo"].get();
		MazeWallRight->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		MazeWallRight->IndexCount = MazeWallRight->Geo->DrawArgs["box"].IndexCount;
		MazeWallRight->StartIndexLocation = MazeWallRight->Geo->DrawArgs["box"].StartIndexLocation;
		MazeWallRight->BaseVertexLocation = MazeWallRight->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWallRight.get());
		mAllRitems.push_back(std::move(MazeWallRight));

		objCBIndex++;

		auto MazeWallLeft = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&MazeWallLeft->World, XMMatrixScaling(wallWidth, wallWidth, scale)*XMMatrixTranslation(outterOffset - outterOffset/2.0f, 0.0f, offset + scale/2.0f - outterOffset / 2.0f));
		XMStoreFloat4x4(&MazeWallLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
		MazeWallLeft->ObjCBIndex = objCBIndex;
		MazeWallLeft->Mat = mMaterials["stone0"].get();
		MazeWallLeft->Geo = mGeometries["shapeGeo"].get();
		MazeWallLeft->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		MazeWallLeft->IndexCount = MazeWallLeft->Geo->DrawArgs["box"].IndexCount;
		MazeWallLeft->StartIndexLocation = MazeWallLeft->Geo->DrawArgs["box"].StartIndexLocation;
		MazeWallLeft->BaseVertexLocation = MazeWallLeft->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWallLeft.get());
		mAllRitems.push_back(std::move(MazeWallLeft));

		offset += scale;
		objCBIndex++;
	}
	
	//nested for loop horizontal then verticle

	int num;

	for (int row = 0; row < steps; row++)
	{
		for (int col = 0; col < steps; col++)
		{
			num = rand() % 5;
			if (num > 1) {
				auto MazeWallVert = std::make_unique<RenderItem>();

				//row * scale, 0.0f, col * scale
				//XMMatrixTranslation(0.0f - outterOffset / 2, 0.0f, offset + scale / 2 - outterOffset / 2)

				XMStoreFloat4x4(&MazeWallVert->World, XMMatrixScaling(wallWidth, wallWidth, scale)*XMMatrixTranslation(row * scale - outterOffset / 2.0f + scale / 2.0f, 0.0f, col * scale - outterOffset / 2.0f + scale / 2.0f));
				XMStoreFloat4x4(&MazeWallVert->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
				MazeWallVert->ObjCBIndex = objCBIndex;
				MazeWallVert->Mat = mMaterials["stone0"].get();
				MazeWallVert->Geo = mGeometries["shapeGeo"].get();
				MazeWallVert->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				MazeWallVert->IndexCount = MazeWallVert->Geo->DrawArgs["box"].IndexCount;
				MazeWallVert->StartIndexLocation = MazeWallVert->Geo->DrawArgs["box"].StartIndexLocation;
				MazeWallVert->BaseVertexLocation = MazeWallVert->Geo->DrawArgs["box"].BaseVertexLocation;
				mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWallVert.get());
				mAllRitems.push_back(std::move(MazeWallVert));

				objCBIndex++;
			}

			num = rand() % 5;
			if(num > 1)
			{
				auto MazeWallHor = std::make_unique<RenderItem>();

				XMStoreFloat4x4(&MazeWallHor->World, XMMatrixScaling(scale, wallWidth, wallWidth)*XMMatrixTranslation(row * scale - outterOffset / 2.0f + scale / 2.0f, 0.0f, col * scale - outterOffset / 2.0f + scale / 2.0f));
				XMStoreFloat4x4(&MazeWallHor->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
				MazeWallHor->ObjCBIndex = objCBIndex;
				MazeWallHor->Mat = mMaterials["stone0"].get();
				MazeWallHor->Geo = mGeometries["shapeGeo"].get();
				MazeWallHor->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				MazeWallHor->IndexCount = MazeWallHor->Geo->DrawArgs["box"].IndexCount;
				MazeWallHor->StartIndexLocation = MazeWallHor->Geo->DrawArgs["box"].StartIndexLocation;
				MazeWallHor->BaseVertexLocation = MazeWallHor->Geo->DrawArgs["box"].BaseVertexLocation;
				mRitemLayer[(int)RenderLayer::Opaque].push_back(MazeWallHor.get());
				mAllRitems.push_back(std::move(MazeWallHor));

				objCBIndex++;
			}

		}

	}

	objCBIndex++;

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = 3;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));

	// All the render items are opaque.
	for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void LitColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
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

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		
		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
		

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> LitColumnsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

float LitColumnsApp::GetHillsHeight(float x, float z)const
{
	return 0.3f*(z*sinf(0.1f*x) + x*cosf(0.1f*z));
}

XMFLOAT3 LitColumnsApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
		1.0f,
		-0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}
