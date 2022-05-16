//***************************************************************************************
// ShapesApp.cpp 
//
// Hold down '1' key to view scene in wireframe mode.
//***************************************************************************************

#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "Common/FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;


#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")


const int gNumFrameResources = 3;
const float width = 50;
const float depth = 50;


enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	AlphaTestedTreeSprites,
	Count
};


// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TWorld = MathHelper::Identity4x4();

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
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

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

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();

	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildTreeSpritesGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;
	XMFLOAT3 GetTreePosition(float minX, float maxX, float minZ, float maxZ, float treeHeightOffset)const;

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	//  ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;


	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];


	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.6f * XM_PI;
	float mPhi = 0.4f * XM_PI;
	float mRadius = 90.0f;

	POINT mLastMousePos;

	UINT objCBIndex = 0;
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
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildTreeSpritesGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
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
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
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
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);
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
    if(GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;
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
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water0"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu -= 0.1f * gt.DeltaTime();
	//tv -= 0.5f * gt.DeltaTime();

	/*if (tu <= 0.0f)
		tu += 1.0f;*/

	if (tv <= 0.0f)
		tv += 0.5f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;

	auto gutsymat = mMaterials["gutsy"].get();

	float& tu2 = gutsymat->MatTransform(3, 0);
	float& tv2 = gutsymat->MatTransform(3, 1);

	tu2 += 0.1f  * gt.DeltaTime();
	tv2 += 0.1f * gt.DeltaTime();



	gutsymat->MatTransform(3, 0) = tu2;
	gutsymat->MatTransform(3, 1) = tv2;

	// Material has changed, so need to update cbuffer.
	gutsymat->NumFramesDirty = gNumFrameResources;
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX tWorld = XMLoadFloat4x4(&e->TWorld);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TWorld, XMMatrixTranspose(MathHelper::InverseTranspose(world)));
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
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
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

	//lights
	mMainPassCB.AmbientLight = { 0.2f, 0.2f, 0.2f, 0.5f };
	//directional light
	mMainPassCB.Lights[0].Direction = { -0.5f, -0.35f, 0.5f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.5, 0.3f };
	//pointlights
	mMainPassCB.Lights[1].Position = { -25.0f, 5.5f, -25.0f };
	mMainPassCB.Lights[1].Strength = { 2.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[2].Position = { 25.0f, 5.5f, -25.0f };
	mMainPassCB.Lights[2].Strength = { 2.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[3].Position = { -26.0f, 5.5f, 25.0f };
	mMainPassCB.Lights[3].Strength = { 2.0f, 1.0f, 0.0f };
	mMainPassCB.Lights[4].Position = { 26.0f, 5.5f, 25.0f };
	mMainPassCB.Lights[4].Strength = { 1.0, 0.0f, 0.0f };
	mMainPassCB.Lights[5].Position = { 0.0f, 5.5f, -25.0f };
	mMainPassCB.Lights[5].Strength = { 1.0, 0.0f, 0.0f };
	mMainPassCB.Lights[6].Position = { -26.0f, 5.5f, 0.5f };
	mMainPassCB.Lights[6].Strength = { 1.0, 0.0f, 0.0f };
	mMainPassCB.Lights[7].Position = { 26.0f, 5.5f, 0.0f };
	mMainPassCB.Lights[7].Strength = { 1.0, 0.0f, 0.0f };
	mMainPassCB.Lights[8].Position = { 0.0f, 5.5f, 25.0f };
	mMainPassCB.Lights[8].Strength = { 1.0, 0.0f, 0.0f };
	//spotlight
	mMainPassCB.Lights[9].Position = { 0.0f, 5.0f, 00.0f };
	mMainPassCB.Lights[9].Direction = { 0.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[9].SpotPower = 3.0f;
	mMainPassCB.Lights[9].Strength = { 2.1f, 2.1f, 2.1f };
	mMainPassCB.Lights[9].FalloffEnd = 20.0f;


	
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"Textures/BloodWall.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto sandTex = std::make_unique<Texture>();
	sandTex->Name = "sandTex";
	sandTex->Filename = L"Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), sandTex->Filename.c_str(),
		sandTex->Resource, sandTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"Textures/lava.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"Textures/corona.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), iceTex->Filename.c_str(),
		iceTex->Resource, iceTex->UploadHeap));

	auto redTex = std::make_unique<Texture>();
	redTex->Name = "redTex";
	redTex->Filename = L"Textures/gutsy.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), redTex->Filename.c_str(),
		redTex->Resource, redTex->UploadHeap));

	auto flagTex = std::make_unique<Texture>();
	flagTex->Name = "flagTex";
	flagTex->Filename = L"Textures/Dragon1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), flagTex->Filename.c_str(),
		flagTex->Resource, flagTex->UploadHeap));

	auto boneTex = std::make_unique<Texture>();
	boneTex->Name = "boneTex";
	boneTex->Filename = L"Textures/door.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), boneTex->Filename.c_str(),
		boneTex->Resource, boneTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"Textures/treeArray.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));



	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[sandTex->Name] = std::move(sandTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[redTex->Name] = std::move(redTex);
	mTextures[flagTex->Name] = std::move(flagTex);
	mTextures[boneTex->Name] = std::move(boneTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
	


}
//If we have 3 frame resources and n render items, then we have three 3n object constant
//buffers and 3 pass constant buffers.Hence we need 3(n + 1) constant buffer views(CBVs).
//Thus we will need to modify our CBV heap to include the additional descriptors :

void ShapesApp::BuildDescriptorHeaps()
{
	//
	 // Create the SRV heap.
	 //
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 10;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto sandTex = mTextures["sandTex"]->Resource;
	auto redTex = mTextures["redTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto flagTex = mTextures["flagTex"]->Resource;
	auto boneTex = mTextures["boneTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
	

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = sandTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = sandTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(sandTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = redTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = redTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(redTex.Get(), &srvDesc, hDescriptor);

	//// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = waterTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = iceTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);


	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = flagTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = flagTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(flagTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = boneTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(boneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	
}


//A root signature defines what resources need to be bound to the pipeline before issuing a draw call and
//how those resources get mapped to shader input registers. there is a limit of 64 DWORDs that can be put in a root signature.
void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,  // number of descriptors
		0); // register t0

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Performance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0); // register b0
	slotRootParameter[2].InitAsConstantBufferView(1); // register b1
	slotRootParameter[3].InitAsConstantBufferView(2); // register b2

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

	if (errorBlob != nullptr)
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
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", defines, "PS", "ps_5_1");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\color.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mStdInputLayout =
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

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    //walls
	GeometryGenerator::MeshData wall1 = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(width*1, depth*1, 60*2, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 20, 20);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(0.6f, 1.3f, 20, 20);
	GeometryGenerator::MeshData building = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(1.0f, 0.1f, 19, 19);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1.0f, 1.0f, 0.5f, 12);
	GeometryGenerator::MeshData door = geoGen.CreatePrism(2.0f, 1.0f, 1.0f);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(1.0f, 1.0f, 1.0f);
	GeometryGenerator::MeshData prism = geoGen.Createpyramid(1.0f, 1.0f, 1.0f);
	GeometryGenerator::MeshData water = geoGen.CreateGrid(width * 2, depth * 2, 60 * 2, 40);
	GeometryGenerator::MeshData grid2 = geoGen.CreateGrid(width * 5, depth * 5, 60 * 2, 40);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)wall1.Vertices.size();
	UINT waterVertexOffset = gridVertexOffset + (UINT)water.Vertices.size();
	UINT sphereVertexOffset = waterVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT buildingVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT torusVertexOffset = buildingVertexOffset + (UINT)building.Vertices.size();
	UINT diamondVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
	UINT doorVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT wedgeVertexOffset = doorVertexOffset + (UINT)door.Vertices.size();
	UINT prismVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
	UINT grid2VertexOffset = prismVertexOffset + (UINT)prism.Vertices.size();
	
	// Cache the starting index for each object in the concatenated index buffer.

	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)wall1.Indices32.size();
	UINT waterIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT sphereIndexOffset = waterIndexOffset + (UINT)water.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT buildingIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT torusIndexOffset = buildingIndexOffset + (UINT)building.Indices32.size();
	UINT diamondIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();
	UINT doorIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT wedgeIndexOffset = doorIndexOffset + (UINT)door.Indices32.size();
	UINT prismIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
	UINT grid2IndexOffset = prismIndexOffset + (UINT)prism.Indices32.size();
	
    // Define the SubmeshGeometry that cover different 
    // regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)wall1.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;


	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry waterSubmesh;
	waterSubmesh.IndexCount = (UINT)water.Indices32.size();
	waterSubmesh.StartIndexLocation = waterIndexOffset;
	waterSubmesh.BaseVertexLocation = waterVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry buildingSubmesh;
	buildingSubmesh.IndexCount = (UINT)building.Indices32.size();
	buildingSubmesh.StartIndexLocation = buildingIndexOffset;
	buildingSubmesh.BaseVertexLocation = buildingVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry doorSubmesh;
	doorSubmesh.IndexCount = (UINT)door.Indices32.size();
	doorSubmesh.StartIndexLocation = doorIndexOffset;
	doorSubmesh.BaseVertexLocation = doorVertexOffset;

	SubmeshGeometry wedgeSubmesh;
	wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
	wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
	wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

	SubmeshGeometry prismSubmesh;
	prismSubmesh.IndexCount = (UINT)prism.Indices32.size();
	prismSubmesh.StartIndexLocation = prismIndexOffset;
	prismSubmesh.BaseVertexLocation = prismVertexOffset;

	SubmeshGeometry grid2Submesh;
	grid2Submesh.IndexCount = (UINT)grid2.Indices32.size();
	grid2Submesh.StartIndexLocation = grid2IndexOffset;
	grid2Submesh.BaseVertexLocation = grid2VertexOffset;


	
	////
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		wall1.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		cone.Vertices.size() +
		building.Vertices.size() +
		torus.Vertices.size() +
		diamond.Vertices.size() +
		door.Vertices.size() +
		wedge.Vertices.size()+
		prism.Vertices.size()+
		water.Vertices.size() +
		grid2.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for(size_t i = 0; i < wall1.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wall1.Vertices[i].Position;
		vertices[k].Normal = wall1.Vertices[i].Normal;
		vertices[k].TexC = wall1.Vertices[i].TexC;
	}

	for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[k].Pos = p;
		vertices[k].Pos.y = GetHillsHeight(p.x, p.z);
		vertices[k].Normal = GetHillsNormal(p.x, p.z);
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < water.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = water.Vertices[i].Position;
		vertices[k].Normal = water.Vertices[i].Normal;
		vertices[k].TexC = water.Vertices[i].TexC;
	}
	for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}
	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Normal = cone.Vertices[i].Normal;
		vertices[k].TexC = cone.Vertices[i].TexC;
	}
	
	for (size_t i = 0; i < building.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = building.Vertices[i].Position;
		vertices[k].Normal = building.Vertices[i].Normal;
		vertices[k].TexC = building.Vertices[i].TexC;
	}
	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Normal = torus.Vertices[i].Normal;
		vertices[k].TexC = torus.Vertices[i].TexC;
		
	}
	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
	}
	for (size_t i = 0; i < door.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = door.Vertices[i].Position;
		vertices[k].Normal = door.Vertices[i].Normal;
		vertices[k].TexC = door.Vertices[i].TexC;
	}
	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Normal = wedge.Vertices[i].Normal;
		vertices[k].TexC = wedge.Vertices[i].TexC;
	}
	for (size_t i = 0; i < prism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = prism.Vertices[i].Position;
		vertices[k].Normal = prism.Vertices[i].Normal;
		vertices[k].TexC = prism.Vertices[i].TexC;
	}
	for (size_t i = 0; i < grid2.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid2.Vertices[i].Position;
		vertices[k].Normal = grid2.Vertices[i].Normal;
		vertices[k].TexC = grid2.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(wall1.GetIndices16()), std::end(wall1.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(water.GetIndices16()), std::end(water.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(building.GetIndices16()), std::end(building.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(door.GetIndices16()), std::end(door.GetIndices16()));
	indices.insert(indices.end(), std::begin(wedge.GetIndices16()), std::end(wedge.GetIndices16()));
	indices.insert(indices.end(), std::begin(prism.GetIndices16()), std::end(prism.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid2.GetIndices16()), std::end(grid2.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

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
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["building"] = buildingSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["door"] = doorSubmesh;
	geo->DrawArgs["wedge"] = wedgeSubmesh;
	geo->DrawArgs["prism"] = prismSubmesh;
	geo->DrawArgs["water"] = waterSubmesh;
	geo->DrawArgs["grid2"] = grid2Submesh;

	mGeometries[geo->Name] = std::move(geo);
}
void ShapesApp::BuildTreeSpritesGeometry()
{
	//step5
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	const float m_size = 20.0f;
	const float m_halfHeight = m_size / 3.0f;   

	static const int treeCount = 80;
	std::array<TreeSpriteVertex, treeCount> vertices;
	//left side trees
	for (UINT i = 0; i < treeCount * 0.6; ++i)
	{
		vertices[i].Pos = GetTreePosition(50, 100, -100,80, 14);
		vertices[i].Size = XMFLOAT2(m_size, m_size);
	}
	//right side trees
	for (UINT i = treeCount * 0.3; i < treeCount * 0.7; ++i)
	{
		vertices[i].Pos = vertices[i].Pos = GetTreePosition(-100, -50, -100, 80, 14);
		vertices[i].Size = XMFLOAT2(m_size, m_size);
	}
	//top side trees
	for (UINT i = treeCount * 0.7; i < treeCount; ++i)
	{
		vertices[i].Pos = vertices[i].Pos = GetTreePosition(-49, 49, 40, 70, 14);
		vertices[i].Size = XMFLOAT2(m_size, m_size);
	}

	for (UINT i = treeCount * 0.9; i < treeCount; ++i)
	{
		vertices[i].Pos = vertices[i].Pos = GetTreePosition(-49, 49, -40, -70, 14);
		vertices[i].Size = XMFLOAT2(m_size, m_size);
	}


	std::array<std::uint16_t, treeCount> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
		27, 28, 29, 30, 31, 32, 33, 34,
		35, 36, 37, 38, 39, 40,
		41, 42, 43, 44, 45, 46 , 47 ,48 ,49 ,50 ,51,
		52,53,54,55,56,57,58,59,60,61,
		62,63,64,65,66,67,68,69,70,71,
		72,73,74,75,76,77,78,79
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

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
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

	//there is abug with F2 key that is supposed to turn on the multisampling!
//Set4xMsaaState(true);
	//m4xMsaaState = true;

	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

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

	//transparentPsoDesc.BlendState.AlphaToCoverageEnable = true;

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
	//step1
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}
void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	bricks0->Roughness = 0.9f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(0.8f, 0.8f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	stone0->Roughness = 0.9f;

	auto sand0 = std::make_unique<Material>();
	sand0->Name = "sand0";
	sand0->MatCBIndex = 2;
	sand0->DiffuseSrvHeapIndex = 2;
	sand0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sand0->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
	sand0->Roughness = 0.95f;

	auto gutsy = std::make_unique<Material>();
	gutsy->Name = "gutsy";
	gutsy->MatCBIndex = 3;
	gutsy->DiffuseSrvHeapIndex = 3;
	gutsy->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	gutsy->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
	gutsy->Roughness = 0.3f;

	auto Water0 = std::make_unique<Material>();
	Water0->Name = "water0";
	Water0->MatCBIndex = 4;
	Water0->DiffuseSrvHeapIndex = 4;
	Water0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	Water0->FresnelR0 = XMFLOAT3(1.0f, 1.0f, 1.0f);
	Water0->Roughness = 1.0f;

	auto Ice0 = std::make_unique<Material>();
	Ice0->Name = "ice0";
	Ice0->MatCBIndex = 5;
	Ice0->DiffuseSrvHeapIndex = 5;
	Ice0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.8f);
	Ice0->FresnelR0 = XMFLOAT3(1.0f, 1.0f, 1.0f);
	Ice0->Roughness = 0.1f;

	auto flag0 = std::make_unique<Material>();
	flag0->Name = "flag0";
	flag0->MatCBIndex = 6;
	flag0->DiffuseSrvHeapIndex = 6;
	flag0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	flag0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	flag0->Roughness = 0.7f;

	auto door = std::make_unique<Material>();
	door->Name = "door";
	door->MatCBIndex = 7;
	door->DiffuseSrvHeapIndex = 7;
	door->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	door->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	door->Roughness = 0.25f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 8;
	treeSprites->DiffuseSrvHeapIndex = 8;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;


	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["gutsy"] = std::move(gutsy);
	mMaterials["ice0"] = std::move(Ice0);
	mMaterials["water0"] = std::move(Water0);
	mMaterials["sand0"] = std::move(sand0);
	mMaterials["flag0"] = std::move(flag0);
	mMaterials["door"] = std::move(door);
	mMaterials["treeSprites"] = std::move(treeSprites);
	


}
void ShapesApp::BuildRenderItems()
{

    auto gridRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(5.00f, 1.50f, 1.50f) * XMMatrixRotationX(-0.55f) * XMMatrixTranslation(0.0f, 10.0f, 100.0f));
    
	gridRitem->ObjCBIndex = 0;
	gridRitem->Mat = mMaterials["sand0"].get();
	gridRitem->Mat->NormalSrvHeapIndex = 1;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	UINT objCBIndex = 1;

	auto gridRitem2 = std::make_unique<RenderItem>();
	gridRitem2->World = MathHelper::Identity4x4();
	gridRitem2->ObjCBIndex = objCBIndex++;
	gridRitem2->Mat = mMaterials["sand0"].get();
	gridRitem2->Mat->NormalSrvHeapIndex = 1;
	gridRitem2->Geo = mGeometries["shapeGeo"].get();
	gridRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem2->IndexCount = gridRitem2->Geo->DrawArgs["grid2"].IndexCount;
	gridRitem2->StartIndexLocation = gridRitem2->Geo->DrawArgs["grid2"].StartIndexLocation;
	gridRitem2->BaseVertexLocation = gridRitem2->Geo->DrawArgs["grid2"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem2.get());
	mAllRitems.push_back(std::move(gridRitem2));

	
	

		auto leftwallRitem = std::make_unique<RenderItem>();
		auto rightwallRitem = std::make_unique<RenderItem>();
		auto upperwallRitem = std::make_unique<RenderItem>();
		auto lowerwallRitem1 = std::make_unique<RenderItem>();
		auto lowerwallRitem2 = std::make_unique<RenderItem>();
		auto lowerwallRitem3 = std::make_unique<RenderItem>();


		XMStoreFloat4x4(&leftwallRitem->World, XMMatrixScaling(1.0f, 15.0f, 50.0f) * XMMatrixTranslation(25.0f, 4.0f, 0.0f));
		leftwallRitem->ObjCBIndex = objCBIndex++;
		leftwallRitem->Mat = mMaterials["bricks0"].get();
		leftwallRitem->Mat->NormalSrvHeapIndex = 1;
		leftwallRitem->Geo = mGeometries["shapeGeo"].get();
		leftwallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftwallRitem->IndexCount = leftwallRitem->Geo->DrawArgs["box"].IndexCount;
		leftwallRitem->StartIndexLocation = leftwallRitem->Geo->DrawArgs["box"].StartIndexLocation;
		leftwallRitem->BaseVertexLocation = leftwallRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftwallRitem.get());

		XMStoreFloat4x4(&rightwallRitem->World, XMMatrixScaling(1.0f, 15.0f, 50.0f) * XMMatrixTranslation(-25.0f, 4.0f, 0.0f));
		rightwallRitem->ObjCBIndex = objCBIndex++;
		rightwallRitem->Mat = mMaterials["bricks0"].get();
		rightwallRitem->Mat->NormalSrvHeapIndex = 1;
		rightwallRitem->Geo = mGeometries["shapeGeo"].get();
		rightwallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightwallRitem->IndexCount = rightwallRitem->Geo->DrawArgs["box"].IndexCount;
		rightwallRitem->StartIndexLocation = rightwallRitem->Geo->DrawArgs["box"].StartIndexLocation;
		rightwallRitem->BaseVertexLocation = rightwallRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightwallRitem.get());

		XMStoreFloat4x4(&upperwallRitem->World, XMMatrixScaling(1.0f, 15.0f, 50.0f)  *XMMatrixRotationY(1.57f) * XMMatrixTranslation(0.0f, 4.0f, 25.0f));
		upperwallRitem->ObjCBIndex = objCBIndex++;
		upperwallRitem->Mat = mMaterials["bricks0"].get();
		upperwallRitem->Mat->NormalSrvHeapIndex = 1;
		upperwallRitem->Geo = mGeometries["shapeGeo"].get();
		upperwallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		upperwallRitem->IndexCount = upperwallRitem->Geo->DrawArgs["box"].IndexCount;
		upperwallRitem->StartIndexLocation = upperwallRitem->Geo->DrawArgs["box"].StartIndexLocation;
		upperwallRitem->BaseVertexLocation = upperwallRitem->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(upperwallRitem.get());

		XMStoreFloat4x4(&lowerwallRitem1->World, XMMatrixScaling(1.0f, 15.0f, 20.0f) * XMMatrixRotationY(1.57f) * XMMatrixTranslation(15.0f, 4.0f, -25.0f));
		lowerwallRitem1->ObjCBIndex = objCBIndex++;
		lowerwallRitem1->Mat = mMaterials["bricks0"].get();
		lowerwallRitem1->Mat->NormalSrvHeapIndex = 1;
		lowerwallRitem1->Geo = mGeometries["shapeGeo"].get();
		lowerwallRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerwallRitem1->IndexCount = lowerwallRitem1->Geo->DrawArgs["box"].IndexCount;
		lowerwallRitem1->StartIndexLocation = lowerwallRitem1->Geo->DrawArgs["box"].StartIndexLocation;
		lowerwallRitem1->BaseVertexLocation = lowerwallRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerwallRitem1.get());

		XMStoreFloat4x4(&lowerwallRitem2->World, XMMatrixScaling(1.0f, 15.0f, 20.0f) * XMMatrixRotationY(1.57f) * XMMatrixTranslation(-15.0f, 4.0f, -25.0f));
		lowerwallRitem2->ObjCBIndex = objCBIndex++;
		lowerwallRitem2->Mat = mMaterials["bricks0"].get();
		lowerwallRitem2->Mat->NormalSrvHeapIndex = 1;
		lowerwallRitem2->Geo = mGeometries["shapeGeo"].get();
		lowerwallRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerwallRitem2->IndexCount = lowerwallRitem2->Geo->DrawArgs["box"].IndexCount;
		lowerwallRitem2->StartIndexLocation = lowerwallRitem2->Geo->DrawArgs["box"].StartIndexLocation;
		lowerwallRitem2->BaseVertexLocation = lowerwallRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerwallRitem2.get());

		XMStoreFloat4x4(&lowerwallRitem3->World, XMMatrixScaling(1.0f, 4.0f, 10.0f) * XMMatrixRotationY(1.57f) * XMMatrixTranslation(0.0f, 9.5f, -25.0f));
		lowerwallRitem3->ObjCBIndex = objCBIndex++;
		lowerwallRitem3->Mat = mMaterials["bricks0"].get();
		lowerwallRitem3->Mat->NormalSrvHeapIndex = 1;
		lowerwallRitem3->Geo = mGeometries["shapeGeo"].get();
		lowerwallRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerwallRitem3->IndexCount = lowerwallRitem3->Geo->DrawArgs["box"].IndexCount;
		lowerwallRitem3->StartIndexLocation = lowerwallRitem3->Geo->DrawArgs["box"].StartIndexLocation;
		lowerwallRitem3->BaseVertexLocation = lowerwallRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerwallRitem3.get());

		mAllRitems.push_back(std::move(leftwallRitem));
		mAllRitems.push_back(std::move(rightwallRitem));
		mAllRitems.push_back(std::move(upperwallRitem));
		mAllRitems.push_back(std::move(lowerwallRitem1));
		mAllRitems.push_back(std::move(lowerwallRitem2));
		mAllRitems.push_back(std::move(lowerwallRitem3));
	


		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto lowerCylRitem = std::make_unique<RenderItem>();
		auto lowerrihtCylRitem = std::make_unique<RenderItem>();


		XMStoreFloat4x4(&leftCylRitem->World, XMMatrixScaling(5.0f, 8.3f, 5.0f) * XMMatrixTranslation(-25.0f, 9.5f, -25.0f));
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["stone0"].get();
		leftCylRitem->Mat->NormalSrvHeapIndex = 1;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());


		XMStoreFloat4x4(&rightCylRitem->World, XMMatrixScaling(5.0f, 8.3f, 5.0f) * XMMatrixTranslation(25.0f, 9.5f, 25.0f));
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["stone0"].get();
		rightCylRitem->Mat->NormalSrvHeapIndex = 1;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());


		XMStoreFloat4x4(&lowerCylRitem->World, XMMatrixScaling(5.0f, 8.0f, 5.0f) * XMMatrixTranslation(25.0f, 9.0f, -25.0f));
		lowerCylRitem->ObjCBIndex = objCBIndex++;
		lowerCylRitem->Mat = mMaterials["stone0"].get();
		lowerCylRitem->Mat->NormalSrvHeapIndex = 1;
		lowerCylRitem->Geo = mGeometries["shapeGeo"].get();
		lowerCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerCylRitem->IndexCount = lowerCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		lowerCylRitem->StartIndexLocation = lowerCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		lowerCylRitem->BaseVertexLocation = lowerCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerCylRitem.get());


		XMStoreFloat4x4(&lowerrihtCylRitem->World, XMMatrixScaling(5.0f, 8.0f, 5.0f) * XMMatrixTranslation(-25.0f, 9.0f, 25.0f));
		lowerrihtCylRitem->ObjCBIndex = objCBIndex++;
		lowerrihtCylRitem->Mat = mMaterials["stone0"].get();
		lowerrihtCylRitem->Mat->NormalSrvHeapIndex = 1;
		lowerrihtCylRitem->Geo = mGeometries["shapeGeo"].get();
		lowerrihtCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerrihtCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		lowerrihtCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		lowerrihtCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerrihtCylRitem.get());




		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(lowerCylRitem));
		mAllRitems.push_back(std::move(lowerrihtCylRitem));



		auto leftConeRitem = std::make_unique<RenderItem>();
		auto rightConelRitem = std::make_unique<RenderItem>();
		auto lowerConeRitem = std::make_unique<RenderItem>();
		auto lowerrihtConeRitem = std::make_unique<RenderItem>();


		XMStoreFloat4x4(&leftConeRitem->World, XMMatrixScaling(5.0f, 7.0f, 5.0f)* XMMatrixTranslation(-25.0f, 25.0f, -25.0f));
		leftConeRitem->ObjCBIndex = objCBIndex++;
		leftConeRitem->Mat = mMaterials["bricks0"].get();
		leftConeRitem->Mat->NormalSrvHeapIndex = 1;
		leftConeRitem->Geo = mGeometries["shapeGeo"].get();
		leftConeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftConeRitem->IndexCount = leftConeRitem->Geo->DrawArgs["cone"].IndexCount;
		leftConeRitem->StartIndexLocation = leftConeRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		leftConeRitem->BaseVertexLocation = leftConeRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(leftConeRitem.get());

		XMStoreFloat4x4(&rightConelRitem->World, XMMatrixScaling(5.0f, 7.0f, 5.0f)* XMMatrixTranslation(25.0f, 25.0f, 25.0f));
		rightConelRitem->ObjCBIndex = objCBIndex++;
		rightConelRitem->Mat = mMaterials["bricks0"].get();
		rightConelRitem->Mat->NormalSrvHeapIndex = 1;
		rightConelRitem->Geo = mGeometries["shapeGeo"].get();
		rightConelRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightConelRitem->IndexCount = rightConelRitem->Geo->DrawArgs["cone"].IndexCount;
		rightConelRitem->StartIndexLocation = rightConelRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		rightConelRitem->BaseVertexLocation = rightConelRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(rightConelRitem.get());

		XMStoreFloat4x4(&lowerConeRitem->World, XMMatrixScaling(5.0f, 7.0f, 5.0f)* XMMatrixTranslation(25.0f, 25.0f, -25.0f));
		lowerConeRitem->ObjCBIndex = objCBIndex++;
		lowerConeRitem->Mat = mMaterials["bricks0"].get();
		lowerConeRitem->Mat->NormalSrvHeapIndex = 1;
		lowerConeRitem->Geo = mGeometries["shapeGeo"].get();
		lowerConeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerConeRitem->IndexCount = lowerConeRitem->Geo->DrawArgs["cone"].IndexCount;
		lowerConeRitem->StartIndexLocation = lowerConeRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		lowerConeRitem->BaseVertexLocation = lowerConeRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerConeRitem.get());

		XMStoreFloat4x4(&lowerrihtConeRitem->World, XMMatrixScaling(5.0f, 7.0f, 5.0f)* XMMatrixTranslation(-25.0f, 25.0f, 25.0f));
		lowerrihtConeRitem->ObjCBIndex = objCBIndex++;
		lowerrihtConeRitem->Mat = mMaterials["bricks0"].get();
		lowerrihtConeRitem->Mat->NormalSrvHeapIndex = 1;
		lowerrihtConeRitem->Geo = mGeometries["shapeGeo"].get();
		lowerrihtConeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lowerrihtConeRitem->IndexCount = rightConelRitem->Geo->DrawArgs["cone"].IndexCount;
		lowerrihtConeRitem->StartIndexLocation = rightConelRitem->Geo->DrawArgs["cone"].StartIndexLocation;
		lowerrihtConeRitem->BaseVertexLocation = rightConelRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(lowerrihtConeRitem.get());



		mAllRitems.push_back(std::move(leftConeRitem));
		mAllRitems.push_back(std::move(rightConelRitem));
		mAllRitems.push_back(std::move(lowerConeRitem));
		mAllRitems.push_back(std::move(lowerrihtConeRitem));



		auto building = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&building->World, XMMatrixScaling(2.0f, 5.0f, 5.0f)* XMMatrixTranslation(0.0f, 2.0f, 0.0f));
		building->ObjCBIndex = objCBIndex++;
		building->Mat = mMaterials["gutsy"].get();
		building->Mat->NormalSrvHeapIndex = 1;
		building->Geo = mGeometries["shapeGeo"].get();
		building->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		building->IndexCount = building->Geo->DrawArgs["building"].IndexCount;
		building->StartIndexLocation = building->Geo->DrawArgs["building"].StartIndexLocation;
		building->BaseVertexLocation = building->Geo->DrawArgs["building"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(building.get());
		mAllRitems.push_back(std::move(building));

	

		auto torus = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&torus->World, XMMatrixScaling(2.2f, 2.2f, 2.2f)* XMMatrixRotationY(3.5f)* XMMatrixRotationX(3.0f)* XMMatrixTranslation(0.0f, 10.0f, 0.0f));
		torus->ObjCBIndex = objCBIndex++;
		torus->Mat = mMaterials["water0"].get();
		torus->Mat->NormalSrvHeapIndex = 1;
		torus->Geo = mGeometries["shapeGeo"].get();
		torus->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		torus->IndexCount = torus->Geo->DrawArgs["torus"].IndexCount;
		torus->StartIndexLocation = torus->Geo->DrawArgs["torus"].StartIndexLocation;
		torus->BaseVertexLocation = torus->Geo->DrawArgs["torus"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(torus.get());
		mAllRitems.push_back(std::move(torus));

		auto torus1 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&torus1->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)*XMMatrixRotationX(0.75f)* XMMatrixRotationY(0.75f)* XMMatrixTranslation(0.0f, 10.0f, 0.0f));
		torus1->ObjCBIndex = objCBIndex++;
		torus1->Mat = mMaterials["water0"].get();
		torus1->Mat->NormalSrvHeapIndex = 1;
		torus1->Geo = mGeometries["shapeGeo"].get();
		torus1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		torus1->IndexCount = torus1->Geo->DrawArgs["torus"].IndexCount;
		torus1->StartIndexLocation = torus1->Geo->DrawArgs["torus"].StartIndexLocation;
		torus1->BaseVertexLocation = torus1->Geo->DrawArgs["torus"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(torus1.get());
		mAllRitems.push_back(std::move(torus1));

		auto torus2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&torus2->World, XMMatrixScaling(1.8f, 1.8f, 1.8f)* XMMatrixRotationX(1.5f)* XMMatrixRotationY(1.5f)* XMMatrixTranslation(0.0f, 10.0f, 0.0f));
		torus2->ObjCBIndex = objCBIndex++;
		torus2->Mat = mMaterials["water0"].get();
		torus2->Mat->NormalSrvHeapIndex = 1;
		torus2->Geo = mGeometries["shapeGeo"].get();
		torus2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		torus2->IndexCount = torus2->Geo->DrawArgs["torus"].IndexCount;
		torus2->StartIndexLocation = torus2->Geo->DrawArgs["torus"].StartIndexLocation;
		torus2->BaseVertexLocation = torus2->Geo->DrawArgs["torus"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(torus2.get());
		mAllRitems.push_back(std::move(torus2));

		auto torus3 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&torus3->World, XMMatrixScaling(2.6f, 2.6f, 2.6f)* XMMatrixRotationX(2.25f)* XMMatrixRotationY(2.25f)* XMMatrixTranslation(0.0f, 10.0f, 0.0f));
		torus3->ObjCBIndex = objCBIndex++;
		torus3->Mat = mMaterials["water0"].get();
		torus3->Mat->NormalSrvHeapIndex = 1;
		torus3->Geo = mGeometries["shapeGeo"].get();
		torus3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		torus3->IndexCount = torus3->Geo->DrawArgs["torus"].IndexCount;
		torus3->StartIndexLocation = torus3->Geo->DrawArgs["torus"].StartIndexLocation;
		torus3->BaseVertexLocation = torus3->Geo->DrawArgs["torus"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(torus3.get());
		mAllRitems.push_back(std::move(torus3));



		auto diamond = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&diamond->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)* XMMatrixTranslation(0.0f, 10.0f, 0.0f));
		diamond->ObjCBIndex = objCBIndex++;
		diamond->Mat = mMaterials["ice0"].get();
		diamond->Mat->NormalSrvHeapIndex = 1;
		diamond->Geo = mGeometries["shapeGeo"].get();
		diamond->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		diamond->IndexCount = diamond->Geo->DrawArgs["diamond"].IndexCount;
		diamond->StartIndexLocation = diamond->Geo->DrawArgs["diamond"].StartIndexLocation;
		diamond->BaseVertexLocation = diamond->Geo->DrawArgs["diamond"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(diamond.get());
		mAllRitems.push_back(std::move(diamond));


		auto door = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&door->World, XMMatrixScaling(10.0f, 5.0f, 2.0f)* XMMatrixTranslation(0.0f, 10.0f,-27.3f));
		door->ObjCBIndex = objCBIndex++;
		door->Mat = mMaterials["door"].get();
		door->Mat->NormalSrvHeapIndex = 1;
		door->Geo = mGeometries["shapeGeo"].get();
		door->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		door->IndexCount = door->Geo->DrawArgs["door"].IndexCount;
		door->StartIndexLocation = door->Geo->DrawArgs["door"].StartIndexLocation;
		door->BaseVertexLocation = door->Geo->DrawArgs["door"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(door.get());
		mAllRitems.push_back(std::move(door));

		auto wedge1 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&wedge1->World, XMMatrixScaling(8.0f, 2.0f, 10.0f)* XMMatrixRotationRollPitchYaw(0.0f, -1.57f,0.0f)* XMMatrixTranslation(0.0f, 1.0f, -28.0f));
		wedge1->ObjCBIndex = objCBIndex++;
		wedge1->Mat = mMaterials["door"].get();
		wedge1->Mat->NormalSrvHeapIndex = 1;
		wedge1->Geo = mGeometries["shapeGeo"].get();
		wedge1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wedge1->IndexCount = wedge1->Geo->DrawArgs["wedge"].IndexCount;
		wedge1->StartIndexLocation = wedge1->Geo->DrawArgs["wedge"].StartIndexLocation;
		wedge1->BaseVertexLocation = wedge1->Geo->DrawArgs["wedge"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(wedge1.get());
		mAllRitems.push_back(std::move(wedge1));



	
		auto prism = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&prism->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(0.0f, 5.5f, 0.0f));
		prism->ObjCBIndex = objCBIndex++;
		prism->Mat = mMaterials["gutsy"].get();
		prism->Mat->NormalSrvHeapIndex = 1;
		prism->Geo = mGeometries["shapeGeo"].get();
		prism->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		prism->IndexCount = prism->Geo->DrawArgs["prism"].IndexCount;
		prism->StartIndexLocation = prism->Geo->DrawArgs["prism"].StartIndexLocation;
		prism->BaseVertexLocation = prism->Geo->DrawArgs["prism"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(prism.get());
		mAllRitems.push_back(std::move(prism));

		auto water = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&water->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)* XMMatrixTranslation(0.0f, 1.5f, 0.0f));
		water->ObjCBIndex = objCBIndex++;
		water->Mat = mMaterials["water0"].get();
		water->Mat->NormalSrvHeapIndex = 1;
		water->Geo = mGeometries["shapeGeo"].get();
		water->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		water->IndexCount = water->Geo->DrawArgs["water"].IndexCount;
		water->StartIndexLocation = water->Geo->DrawArgs["water"].StartIndexLocation;
		water->BaseVertexLocation = water->Geo->DrawArgs["water"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(water.get());
		mAllRitems.push_back(std::move(water));





		auto maze1 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze1->World, XMMatrixScaling(1.0f, 7.0f, 8.0f)* XMMatrixTranslation(-5.0f, 2.0f, -18.0f));
		maze1->ObjCBIndex = objCBIndex++;
		maze1->Mat = mMaterials["door"].get();
		maze1->Mat->NormalSrvHeapIndex = 1;
		maze1->Geo = mGeometries["shapeGeo"].get();
		maze1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze1->IndexCount = maze1->Geo->DrawArgs["box"].IndexCount;
		maze1->StartIndexLocation = maze1->Geo->DrawArgs["box"].StartIndexLocation;
		maze1->BaseVertexLocation = maze1->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze1.get());
		mAllRitems.push_back(std::move(maze1));


		auto maze2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze2->World, XMMatrixScaling(1.0f, 7.0f, 8.0f)* XMMatrixTranslation(5.0f, 2.0f, -18.0f));
		maze2->ObjCBIndex = objCBIndex++;
		maze2->Mat = mMaterials["door"].get();
		maze2->Mat->NormalSrvHeapIndex = 1;
		maze2->Geo = mGeometries["shapeGeo"].get();
		maze2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze2->IndexCount = maze2->Geo->DrawArgs["box"].IndexCount;
		maze2->StartIndexLocation = maze2->Geo->DrawArgs["box"].StartIndexLocation;
		maze2->BaseVertexLocation = maze2->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze2.get());
		mAllRitems.push_back(std::move(maze2));


		auto maze3 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze3->World, XMMatrixScaling(10.0f, 7.0f, 3.0f)* XMMatrixTranslation(0.0f, 2.0f, -8.0f));
		maze3->ObjCBIndex = objCBIndex++;
		maze3->Mat = mMaterials["door"].get();
		maze3->Mat->NormalSrvHeapIndex = 1;
		maze3->Geo = mGeometries["shapeGeo"].get();
		maze3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze3->IndexCount = maze3->Geo->DrawArgs["box"].IndexCount;
		maze3->StartIndexLocation = maze3->Geo->DrawArgs["box"].StartIndexLocation;
		maze3->BaseVertexLocation = maze3->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze3.get());
		mAllRitems.push_back(std::move(maze3));

		auto maze4 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze4->World, XMMatrixScaling(6.8f, 7.0f, 3.0f)* XMMatrixTranslation(-13.7f, 2.0f, -13.0f));
		maze4->ObjCBIndex = objCBIndex++;
		maze4->Mat = mMaterials["door"].get();
		maze4->Mat->NormalSrvHeapIndex = 1;
		maze4->Geo = mGeometries["shapeGeo"].get();
		maze4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze4->IndexCount = maze4->Geo->DrawArgs["box"].IndexCount;
		maze4->StartIndexLocation = maze4->Geo->DrawArgs["box"].StartIndexLocation;
		maze4->BaseVertexLocation = maze4->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze4.get());
		mAllRitems.push_back(std::move(maze4));

		auto maze5 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze5->World, XMMatrixScaling(6.8f, 7.0f, 3.0f)* XMMatrixTranslation(13.7f, 2.0f, -12.9f));
		maze5->ObjCBIndex = objCBIndex++;
		maze5->Mat = mMaterials["door"].get();
		maze5->Mat->NormalSrvHeapIndex = 1;
		maze5->Geo = mGeometries["shapeGeo"].get();
		maze5->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze5->IndexCount = maze5->Geo->DrawArgs["box"].IndexCount;
		maze5->StartIndexLocation = maze5->Geo->DrawArgs["box"].StartIndexLocation;
		maze5->BaseVertexLocation = maze5->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze5.get());
		mAllRitems.push_back(std::move(maze5));


		auto maze6 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze6->World, XMMatrixScaling(1.0f, 7.0f, 13.0f)* XMMatrixTranslation(-13.5f, 2.0f, 0.0f));
		maze6->ObjCBIndex = objCBIndex++;
		maze6->Mat = mMaterials["door"].get();
		maze6->Mat->NormalSrvHeapIndex = 1;
		maze6->Geo = mGeometries["shapeGeo"].get();
		maze6->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze6->IndexCount = maze6->Geo->DrawArgs["box"].IndexCount;
		maze6->StartIndexLocation = maze6->Geo->DrawArgs["box"].StartIndexLocation;
		maze6->BaseVertexLocation = maze6->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze6.get());
		mAllRitems.push_back(std::move(maze6));



		auto maze7 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze7->World, XMMatrixScaling(1.0f, 7.0f, 13.0f)* XMMatrixTranslation(13.5f, 2.0f, 0.0f));
		maze7->ObjCBIndex = objCBIndex++;
		maze7->Mat = mMaterials["door"].get();
		maze7->Mat->NormalSrvHeapIndex = 1;
		maze7->Geo = mGeometries["shapeGeo"].get();
		maze7->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze7->IndexCount = maze7->Geo->DrawArgs["box"].IndexCount;
		maze7->StartIndexLocation = maze7->Geo->DrawArgs["box"].StartIndexLocation;
		maze7->BaseVertexLocation = maze7->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze7.get());
		mAllRitems.push_back(std::move(maze7));


		auto maze8 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&maze8->World, XMMatrixScaling(3.8f, 7.0f, 3.0f)* XMMatrixTranslation(9.3f, 2.0f, 7.9f));
		maze8->ObjCBIndex = objCBIndex++;
		maze8->Mat = mMaterials["door"].get();
		maze8->Mat->NormalSrvHeapIndex = 1;
		maze8->Geo = mGeometries["shapeGeo"].get();
		maze8->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze8->IndexCount = maze8->Geo->DrawArgs["box"].IndexCount;
		maze8->StartIndexLocation = maze8->Geo->DrawArgs["box"].StartIndexLocation;
		maze8->BaseVertexLocation = maze8->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze8.get());
		mAllRitems.push_back(std::move(maze8));


		auto maze9 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&maze9->World, XMMatrixScaling(3.8f, 7.0f, 3.0f)* XMMatrixTranslation(-9.3f, 2.0f, 7.9f));
		maze9->ObjCBIndex = objCBIndex++;
		maze9->Mat = mMaterials["door"].get();
		maze9->Mat->NormalSrvHeapIndex = 1;
		maze9->Geo = mGeometries["shapeGeo"].get();
		maze9->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze9->IndexCount = maze9->Geo->DrawArgs["box"].IndexCount;
		maze9->StartIndexLocation = maze9->Geo->DrawArgs["box"].StartIndexLocation;
		maze9->BaseVertexLocation = maze9->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze9.get());
		mAllRitems.push_back(std::move(maze9));


		auto maze10 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze10->World, XMMatrixScaling(1.0f, 7.0f, 8.0f)* XMMatrixTranslation(-5.1f, 2.0f, 13.0f));
		maze10->ObjCBIndex = objCBIndex++;
		maze10->Mat = mMaterials["door"].get();
		maze10->Mat->NormalSrvHeapIndex = 1;
		maze10->Geo = mGeometries["shapeGeo"].get();
		maze10->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze10->IndexCount = maze10->Geo->DrawArgs["box"].IndexCount;
		maze10->StartIndexLocation = maze10->Geo->DrawArgs["box"].StartIndexLocation;
		maze10->BaseVertexLocation = maze10->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze10.get());
		mAllRitems.push_back(std::move(maze10));



		auto maze11 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&maze11->World, XMMatrixScaling(5.0f, 7.0f, 3.0f)* XMMatrixTranslation(11.2f, 2.0f, 17.0f));
		maze11->ObjCBIndex = objCBIndex++;
		maze11->Mat = mMaterials["door"].get();
		maze11->Mat->NormalSrvHeapIndex = 1;
		maze11->Geo = mGeometries["shapeGeo"].get();
		maze11->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze11->IndexCount = maze11->Geo->DrawArgs["box"].IndexCount;
		maze11->StartIndexLocation = maze11->Geo->DrawArgs["box"].StartIndexLocation;
		maze11->BaseVertexLocation = maze11->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze11.get());
		mAllRitems.push_back(std::move(maze11));


		auto maze12 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze12->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(-16.5f, 2.0f, -8.0f));
		maze12->ObjCBIndex = objCBIndex++;
		maze12->Mat = mMaterials["door"].get();
		maze12->Mat->NormalSrvHeapIndex = 1;
		maze12->Geo = mGeometries["shapeGeo"].get();
		maze12->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze12->IndexCount = maze12->Geo->DrawArgs["box"].IndexCount;
		maze12->StartIndexLocation = maze12->Geo->DrawArgs["box"].StartIndexLocation;
		maze12->BaseVertexLocation = maze12->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze12.get());
		mAllRitems.push_back(std::move(maze12));

		auto maze13 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze13->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(16.5f, 2.0f, -8.0f));
		maze13->ObjCBIndex = objCBIndex++;
		maze13->Mat = mMaterials["door"].get();
		maze13->Mat->NormalSrvHeapIndex = 1;
		maze13->Geo = mGeometries["shapeGeo"].get();
		maze13->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze13->IndexCount = maze13->Geo->DrawArgs["box"].IndexCount;
		maze13->StartIndexLocation = maze13->Geo->DrawArgs["box"].StartIndexLocation;
		maze13->BaseVertexLocation = maze13->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze13.get());
		mAllRitems.push_back(std::move(maze13));


		auto maze14 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze14->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(22.0f, 2.0f, -3.0f));
		maze14->ObjCBIndex = objCBIndex++;
		maze14->Mat = mMaterials["door"].get();
		maze14->Mat->NormalSrvHeapIndex = 1;
		maze14->Geo = mGeometries["shapeGeo"].get();
		maze14->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze14->IndexCount = maze14->Geo->DrawArgs["box"].IndexCount;
		maze14->StartIndexLocation = maze14->Geo->DrawArgs["box"].StartIndexLocation;
		maze14->BaseVertexLocation = maze14->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze14.get());
		mAllRitems.push_back(std::move(maze14));

		auto maze15 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze15->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(16.5f, 2.0f, 2.0f));
		maze15->ObjCBIndex = objCBIndex++;
		maze15->Mat = mMaterials["door"].get();
		maze15->Mat->NormalSrvHeapIndex = 1;
		maze15->Geo = mGeometries["shapeGeo"].get();
		maze15->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze15->IndexCount = maze15->Geo->DrawArgs["box"].IndexCount;
		maze15->StartIndexLocation = maze15->Geo->DrawArgs["box"].StartIndexLocation;
		maze15->BaseVertexLocation = maze15->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze15.get());
		mAllRitems.push_back(std::move(maze15));

		auto maze16 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze16->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(22.0f, 2.0f, 7.0f));
		maze16->ObjCBIndex = objCBIndex++;
		maze16->Mat = mMaterials["door"].get();
		maze16->Mat->NormalSrvHeapIndex = 1;
		maze16->Geo = mGeometries["shapeGeo"].get();
		maze16->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze16->IndexCount = maze16->Geo->DrawArgs["box"].IndexCount;
		maze16->StartIndexLocation = maze16->Geo->DrawArgs["box"].StartIndexLocation;
		maze16->BaseVertexLocation = maze16->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze16.get());
		mAllRitems.push_back(std::move(maze16));



		auto maze17 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze17->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(-22.0f, 2.0f, -3.0f));
		maze17->ObjCBIndex = objCBIndex++;
		maze17->Mat = mMaterials["door"].get();
		maze17->Mat->NormalSrvHeapIndex = 1;
		maze17->Geo = mGeometries["shapeGeo"].get();
		maze17->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze17->IndexCount = maze17->Geo->DrawArgs["box"].IndexCount;
		maze17->StartIndexLocation = maze17->Geo->DrawArgs["box"].StartIndexLocation;
		maze17->BaseVertexLocation = maze17->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze17.get());
		mAllRitems.push_back(std::move(maze17));

		auto maze18 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze18->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(-16.5f, 2.0f, 2.0f));
		maze18->ObjCBIndex = objCBIndex++;
		maze18->Mat = mMaterials["door"].get();
		maze18->Mat->NormalSrvHeapIndex = 1;
		maze18->Geo = mGeometries["shapeGeo"].get();
		maze18->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze18->IndexCount = maze18->Geo->DrawArgs["box"].IndexCount;
		maze18->StartIndexLocation = maze18->Geo->DrawArgs["box"].StartIndexLocation;
		maze18->BaseVertexLocation = maze18->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze18.get());
		mAllRitems.push_back(std::move(maze18));

		auto maze19 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze19->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(-22.0f, 2.0f, 7.0f));
		maze19->ObjCBIndex = objCBIndex++;
		maze19->Mat = mMaterials["door"].get();
		maze19->Mat->NormalSrvHeapIndex = 1;
		maze19->Geo = mGeometries["shapeGeo"].get();
		maze19->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze19->IndexCount = maze19->Geo->DrawArgs["box"].IndexCount;
		maze19->StartIndexLocation = maze19->Geo->DrawArgs["box"].StartIndexLocation;
		maze19->BaseVertexLocation = maze19->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze19.get());
		mAllRitems.push_back(std::move(maze19));

		auto maze20 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze20->World, XMMatrixScaling(1.0f, 7.0f, 8.0f)* XMMatrixTranslation(5.1f, 2.0f, 13.0f));
		maze20->ObjCBIndex = objCBIndex++;
		maze20->Mat = mMaterials["door"].get();
		maze20->Mat->NormalSrvHeapIndex = 1;
		maze20->Geo = mGeometries["shapeGeo"].get();
		maze20->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze20->IndexCount = maze20->Geo->DrawArgs["box"].IndexCount;
		maze20->StartIndexLocation = maze20->Geo->DrawArgs["box"].StartIndexLocation;
		maze20->BaseVertexLocation = maze20->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze20.get());
		mAllRitems.push_back(std::move(maze20));


		auto maze21 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&maze21->World, XMMatrixScaling(5.0f, 7.0f, 3.0f)* XMMatrixTranslation(-11.2f, 2.0f, 17.0f));
		maze21->ObjCBIndex = objCBIndex++;
		maze21->Mat = mMaterials["door"].get();
		maze21->Mat->NormalSrvHeapIndex = 1;
		maze21->Geo = mGeometries["shapeGeo"].get();
		maze21->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze21->IndexCount = maze21->Geo->DrawArgs["box"].IndexCount;
		maze21->StartIndexLocation = maze21->Geo->DrawArgs["box"].StartIndexLocation;
		maze21->BaseVertexLocation = maze21->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze21.get());
		mAllRitems.push_back(std::move(maze21));

		auto maze22 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze22->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(-2.2f, 2.0f, 17.0f));
		maze22->ObjCBIndex = objCBIndex++;
		maze22->Mat = mMaterials["door"].get();
		maze22->Mat->NormalSrvHeapIndex = 1;
		maze22->Geo = mGeometries["shapeGeo"].get();
		maze22->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze22->IndexCount = maze22->Geo->DrawArgs["box"].IndexCount;
		maze22->StartIndexLocation = maze22->Geo->DrawArgs["box"].StartIndexLocation;
		maze22->BaseVertexLocation = maze22->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze22.get());
		mAllRitems.push_back(std::move(maze22));

		auto maze23 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze23->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(2.2f, 2.0f, 12.45f));
		maze23->ObjCBIndex = objCBIndex++;
		maze23->Mat = mMaterials["door"].get();
		maze23->Mat->NormalSrvHeapIndex = 1;
		maze23->Geo = mGeometries["shapeGeo"].get();
		maze23->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze23->IndexCount = maze23->Geo->DrawArgs["box"].IndexCount;
		maze23->StartIndexLocation = maze23->Geo->DrawArgs["box"].StartIndexLocation;
		maze23->BaseVertexLocation = maze23->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze23.get());
		mAllRitems.push_back(std::move(maze23));

		auto maze24 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&maze24->World, XMMatrixScaling(1.0f, 7.0f, 3.0f)* XMMatrixTranslation(-2.2f, 2.0f, 7.9f));
		maze24->ObjCBIndex = objCBIndex++;
		maze24->Mat = mMaterials["door"].get();
		maze24->Mat->NormalSrvHeapIndex = 1;
		maze24->Geo = mGeometries["shapeGeo"].get();
		maze24->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		maze24->IndexCount = maze24->Geo->DrawArgs["box"].IndexCount;
		maze24->StartIndexLocation = maze24->Geo->DrawArgs["box"].StartIndexLocation;
		maze24->BaseVertexLocation = maze24->Geo->DrawArgs["box"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::Opaque].push_back(maze24.get());
		mAllRitems.push_back(std::move(maze24));



		auto treeSpritesRitem = std::make_unique<RenderItem>();
		treeSpritesRitem->World = MathHelper::Identity4x4();
		treeSpritesRitem->ObjCBIndex = objCBIndex++;
		treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
		treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
		treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
		treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
		treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
		mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
		mAllRitems.push_back(std::move(treeSpritesRitem));



	
}


//The DrawRenderItems method is invoked in the main Draw call:
void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}

}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ShapesApp::GetStaticSamplers()
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


float ShapesApp::GetHillsHeight(float x, float z)const
{
	return -0.11 * (z * sinf(0.3f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 ShapesApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.11f * z * cosf(0.7f * x) - 0.1f * cosf(0.6f * z),
		3.0f,
		-2.2f * sinf(0.2f * x) + 0.14f * x * sinf(0.2f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

XMFLOAT3 ShapesApp::GetTreePosition(float minX, float maxX, float minZ, float maxZ, float treeHeightOffset)const
{
	XMFLOAT3 pos(0.0f, -3.0f, 0.0f);


	pos.x = MathHelper::RandF(minX, maxX);
	pos.z = MathHelper::RandF(minZ, maxZ);
	pos.y = 0;


	// Move tree slightly above land height.
	pos.y += treeHeightOffset - 4;

	return pos;
}

