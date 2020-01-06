// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "DuplicationManager.h"

//
// Constructor sets up references / variables
//
DUPLICATIONMANAGER::DUPLICATIONMANAGER() : m_DeskDupl(nullptr),
                                           m_AcquiredDesktopImage(nullptr),
                                           m_MetaDataBuffer(nullptr),
                                           m_MetaDataSize(0),
                                           m_OutputNumber(0),
                                           m_Device(nullptr)
{
    RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

//
// Destructor simply calls CleanRefs to destroy everything
//
DUPLICATIONMANAGER::~DUPLICATIONMANAGER()
{
    if (m_DeskDupl)
    {
        m_DeskDupl->Release();
        m_DeskDupl = nullptr;
    }

    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    if (m_MetaDataBuffer)
    {
        delete [] m_MetaDataBuffer;
        m_MetaDataBuffer = nullptr;
    }

    if (m_Device)
    {
        m_Device->Release();
        m_Device = nullptr;
    }
}

//
// Initialize duplication interfaces
//
DUPL_RETURN DUPLICATIONMANAGER::InitDupl(_In_ ID3D11Device* Device, UINT Output)
{
    m_OutputNumber = Output;

    // Take a reference on the device
    m_Device = Device;
    m_Device->AddRef();

    // Get DXGI device
    IDXGIDevice* DxgiDevice = nullptr;
    HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DXGI Device", L"Error", hr);
    }

    // Get DXGI adapter
    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get parent DXGI Adapter", L"Error", hr, SystemTransitionsExpectedErrors);
    }

    // Get output
    IDXGIOutput* DxgiOutput = nullptr;
    hr = DxgiAdapter->EnumOutputs(Output, &DxgiOutput);
    DxgiAdapter->Release();
    DxgiAdapter = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to get specified output in DUPLICATIONMANAGER", L"Error", hr, EnumOutputsExpectedErrors);
    }

    DxgiOutput->GetDesc(&m_OutputDesc);

    // QI for Output 1
    IDXGIOutput1* DxgiOutput1 = nullptr;
    hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
    DxgiOutput->Release();
    DxgiOutput = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DxgiOutput1 in DUPLICATIONMANAGER", L"Error", hr);
    }

    // Create desktop duplication
    hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DeskDupl);
    DxgiOutput1->Release();
    DxgiOutput1 = nullptr;
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
        {
            MessageBoxW(nullptr, L"There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.", L"Error", MB_OK);
            return DUPL_RETURN_ERROR_UNEXPECTED;
        }
        return ProcessFailure(m_Device, L"Failed to get duplicate output in DUPLICATIONMANAGER", L"Error", hr, CreateDuplicationExpectedErrors);
    }



    return DUPL_RETURN_SUCCESS;
}

//
// Retrieves mouse info and write it into PtrInfo
//
DUPL_RETURN DUPLICATIONMANAGER::GetMouse(_Inout_ PTR_INFO* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, INT OffsetX, INT OffsetY)
{
    // A non-zero mouse update timestamp indicates that there is a mouse position update and optionally a shape change
    if (FrameInfo->LastMouseUpdateTime.QuadPart == 0)
    {
        return DUPL_RETURN_SUCCESS;
    }

    bool UpdatePosition = true;

    // Make sure we don't update pointer position wrongly
    // If pointer is invisible, make sure we did not get an update from another output that the last time that said pointer
    // was visible, if so, don't set it to invisible or update.
    if (!FrameInfo->PointerPosition.Visible && (PtrInfo->WhoUpdatedPositionLast != m_OutputNumber))
    {
        UpdatePosition = false;
    }

    // If two outputs both say they have a visible, only update if new update has newer timestamp
    if (FrameInfo->PointerPosition.Visible && PtrInfo->Visible && (PtrInfo->WhoUpdatedPositionLast != m_OutputNumber) && (PtrInfo->LastTimeStamp.QuadPart > FrameInfo->LastMouseUpdateTime.QuadPart))
    {
        UpdatePosition = false;
    }

    // Update position
    if (UpdatePosition)
    {
        PtrInfo->Position.x = FrameInfo->PointerPosition.Position.x + m_OutputDesc.DesktopCoordinates.left - OffsetX;
        PtrInfo->Position.y = FrameInfo->PointerPosition.Position.y + m_OutputDesc.DesktopCoordinates.top - OffsetY;
        PtrInfo->WhoUpdatedPositionLast = m_OutputNumber;
        PtrInfo->LastTimeStamp = FrameInfo->LastMouseUpdateTime;
        PtrInfo->Visible = FrameInfo->PointerPosition.Visible != 0;
    }

    // No new shape
    if (FrameInfo->PointerShapeBufferSize == 0)
    {
        return DUPL_RETURN_SUCCESS;
    }

    // Old buffer too small
    if (FrameInfo->PointerShapeBufferSize > PtrInfo->BufferSize)
    {
        if (PtrInfo->PtrShapeBuffer)
        {
            delete [] PtrInfo->PtrShapeBuffer;
            PtrInfo->PtrShapeBuffer = nullptr;
        }
        PtrInfo->PtrShapeBuffer = new (std::nothrow) BYTE[FrameInfo->PointerShapeBufferSize];
        if (!PtrInfo->PtrShapeBuffer)
        {
            PtrInfo->BufferSize = 0;
            return ProcessFailure(nullptr, L"Failed to allocate memory for pointer shape in DUPLICATIONMANAGER", L"Error", E_OUTOFMEMORY);
        }

        // Update buffer size
        PtrInfo->BufferSize = FrameInfo->PointerShapeBufferSize;
    }

    // Get shape
    UINT BufferSizeRequired;
    HRESULT hr = m_DeskDupl->GetFramePointerShape(FrameInfo->PointerShapeBufferSize, reinterpret_cast<VOID*>(PtrInfo->PtrShapeBuffer), &BufferSizeRequired, &(PtrInfo->ShapeInfo));
    if (FAILED(hr))
    {
        delete [] PtrInfo->PtrShapeBuffer;
        PtrInfo->PtrShapeBuffer = nullptr;
        PtrInfo->BufferSize = 0;
        return ProcessFailure(m_Device, L"Failed to get frame pointer shape in DUPLICATIONMANAGER", L"Error", hr, FrameInfoExpectedErrors);
    }

    return DUPL_RETURN_SUCCESS;
}


void debugHR(HRESULT hr)
{

	char str[4096];
	std::stringstream ss;
	ss << "0x" << std::hex << hr << std::endl;


	sprintf_s(str, "Op failed: %s\n", ss.str().c_str());
	printfN(str);
}



bool cpu_frame_ready;


//
// Get next frame and write it into Data
//
_Success_(*Timeout == false && return == DUPL_RETURN_SUCCESS)
DUPL_RETURN DUPLICATIONMANAGER::GetFrame(_Out_ FRAME_DATA* Data, _Out_ bool* Timeout)
{
    IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    // Get new frame
    HRESULT hr = m_DeskDupl->AcquireNextFrame(500, &FrameInfo, &DesktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
        *Timeout = true;
        return DUPL_RETURN_SUCCESS;
    }
    *Timeout = false;

    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to acquire next frame in DUPLICATIONMANAGER", L"Error", hr, FrameInfoExpectedErrors);
    }

    // If still holding old frame, destroy it
    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    // QI for IDXGIResource
    hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_AcquiredDesktopImage));
    DesktopResource->Release();
    DesktopResource = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for ID3D11Texture2D from acquired IDXGIResource in DUPLICATIONMANAGER", L"Error", hr);
    }

    // Get metadata
    if (FrameInfo.TotalMetadataBufferSize)
    {
        // Old buffer too small
        if (FrameInfo.TotalMetadataBufferSize > m_MetaDataSize)
        {
            if (m_MetaDataBuffer)
            {
                delete [] m_MetaDataBuffer;
                m_MetaDataBuffer = nullptr;
            }
            m_MetaDataBuffer = new (std::nothrow) BYTE[FrameInfo.TotalMetadataBufferSize];
            if (!m_MetaDataBuffer)
            {
                m_MetaDataSize = 0;
                Data->MoveCount = 0;
                Data->DirtyCount = 0;
                return ProcessFailure(nullptr, L"Failed to allocate memory for metadata in DUPLICATIONMANAGER", L"Error", E_OUTOFMEMORY);
            }
            m_MetaDataSize = FrameInfo.TotalMetadataBufferSize;
        }

        UINT BufSize = FrameInfo.TotalMetadataBufferSize;

        // Get move rectangles
        hr = m_DeskDupl->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(m_MetaDataBuffer), &BufSize);
        if (FAILED(hr))
        {
            Data->MoveCount = 0;
            Data->DirtyCount = 0;
            return ProcessFailure(nullptr, L"Failed to get frame move rects in DUPLICATIONMANAGER", L"Error", hr, FrameInfoExpectedErrors);
        }
        Data->MoveCount = BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

        BYTE* DirtyRects = m_MetaDataBuffer + BufSize;
        BufSize = FrameInfo.TotalMetadataBufferSize - BufSize;

        // Get dirty rectangles
        hr = m_DeskDupl->GetFrameDirtyRects(BufSize, reinterpret_cast<RECT*>(DirtyRects), &BufSize);
        if (FAILED(hr))
        {
            Data->MoveCount = 0;
            Data->DirtyCount = 0;
            return ProcessFailure(nullptr, L"Failed to get frame dirty rects in DUPLICATIONMANAGER", L"Error", hr, FrameInfoExpectedErrors);
        }
        Data->DirtyCount = BufSize / sizeof(RECT);

        Data->MetaData = m_MetaDataBuffer;
    }

    Data->Frame = m_AcquiredDesktopImage;
    Data->FrameInfo = FrameInfo;


    D3D11_TEXTURE2D_DESC frameDesc;
    Data->Frame->GetDesc(&frameDesc);

    char str[1024];



	ID3D11Texture2D* pTexture = NULL;
	ID3D11Device* pd3dDevice = m_Device; // Don't forget to initialize this
   

	D3D11_TEXTURE2D_DESC newDesc = frameDesc;
	newDesc.Usage = D3D11_USAGE_STAGING;
	newDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	newDesc.MiscFlags = 0;
	newDesc.BindFlags = 0;
	hr = pd3dDevice->CreateTexture2D(&newDesc, NULL, &pTexture); // If first run, init our texture for getting frame to CPU
	if (FAILED(hr))
	{
		sprintf_s(str, "Create Texture2D failed...\n");
		printfN(str);
		debugHR(hr);
		pTexture = NULL;
	}


	// Grab a device context
	ID3D11DeviceContext* conty;
	pd3dDevice->GetImmediateContext(&conty);

	// Use our context to clone our frame into memory we can mangle
	conty->CopyResource(pTexture, Data->Frame);

	// Some indexing (DA FIRST ONE)
	UINT subresource = D3D11CalcSubresource(0, 0, 0);

	// Block GPU from touching our resource, grab a reference to it
	D3D11_MAPPED_SUBRESOURCE resource;

	hr = conty->Map(pTexture, subresource, D3D11_MAP_READ, 0, &resource);
	if (FAILED(hr))
	{
		sprintf_s(str, "Map failed\n");
		printfN(str);
		debugHR(hr);
	}


	datagoeshere = Data;

	const unsigned char* source = static_cast<const unsigned char*>(resource.pData);
	Data->width = newDesc.Width;
	Data->height = newDesc.Height;


	unsigned int texsizebytes = resource.DepthPitch; // Width*Height*BitDepth // TODO: FIX

	sprintf(str, "texsizebytes: %d\npitch: %d\ndepth: %d\nresource.pData: %p\n", texsizebytes, resource.RowPitch, resource.DepthPitch, resource.pData);
	//printfN(str);


	if (texsizebytes != Data->cpu_frame_size)
	{
		sprintf(str, "CPU FRAME SIZE CHANGED!!!!!!, oldsize: %u, newsize: %u\n\n", Data->cpu_frame_size, texsizebytes);
		printfN(str);
	}

	// If first run, malloc buffer to copy frames into
	if (cpu_frame_ready == false || texsizebytes != Data->cpu_frame_size)
	{
		Data->cpu_frame = malloc(texsizebytes + 1);
		if (Data->cpu_frame == NULL)
		{
			sprintf(str, "\n\nERROR, FAILED TO ALLOC CPU FRAME\n\n");
			printfN(str);
		}
		else
		{
			Data->cpu_frame_size = texsizebytes;
			cpu_frame_ready = true;
		}
	}

	// If frame ready, copy to it, modulo for multi monitor too tho
	if (cpu_frame_ready == true)
	{
		memcpy(Data->cpu_frame, resource.pData, texsizebytes);
		sprintf(str, "Succsfully copied frame\n\n");
		//printfN(str);
	}

	sprintf_s(str, "pitch: %u, depth: %u\n", resource.RowPitch, resource.DepthPitch);
	//printfN(str);

	sprintf_s(str, "pixel1: b: %u, g: %u, r: %u, a: %u\n", *(((BYTE*)resource.pData) + 0), *(((BYTE*)resource.pData) + 1), *(((BYTE*)resource.pData) + 2), *(((BYTE*)resource.pData) + 3));
	//printfN(str);




	// For when we're done copying, allow GPU to access again
	conty->Unmap(pTexture, subresource);
	if (FAILED(hr))
	{
		sprintf_s(str, "UnMap failed\n");
		printfN(str);
		debugHR(hr);
	}


	pTexture->Release();


    

	// Let the other thread know data has been setup
	if (datagoeshere == NULL)
	{
		sprintf_s(str, "Frame width: %d; frame height: %d; size: %d, type: %d; GUESS TYPE: %d; ADDR: %p\n", frameDesc.Width, frameDesc.Height, frameDesc.ArraySize, frameDesc.Format, DXGI_FORMAT_B8G8R8A8_UNORM, (void*)Data);
		printfN(str);
		datagoeshere = (void*)Data;
	}




	/*


	HEY DANTE
	
	//Variable Declaration
	IDXGIOutputDuplication* lDeskDupl;
	IDXGIResource* lDesktopResource = nullptr;
	DXGI_OUTDUPL_FRAME_INFO lFrameInfo;
	ID3D11Texture2D* lAcquiredDesktopImage;
	ID3D11Texture2D* lDestImage;
	ID3D11DeviceContext* lImmediateContext;
	UCHAR* g_iMageBuffer = nullptr;

	//Screen capture start here
	hr = lDeskDupl->AcquireNextFrame(20, &lFrameInfo, &lDesktopResource);

	// >QueryInterface for ID3D11Texture2D
	hr = lDesktopResource->QueryInterface(IID_PPV_ARGS(&lAcquiredDesktopImage));
	lDesktopResource->Release();

	// Copy image into GDI drawing texture
	lImmediateContext->CopyResource(lDestImage, lAcquiredDesktopImage);
	lAcquiredDesktopImage->Release();
	lDeskDupl->ReleaseFrame();

	// Copy GPU Resource to CPU
	D3D11_TEXTURE2D_DESC desc;
	lDestImage->GetDesc(&desc);
	D3D11_MAPPED_SUBRESOURCE resource;
	UINT subresource = D3D11CalcSubresource(0, 0, 0);
	lImmediateContext->Map(lDestImage, subresource, D3D11_MAP_READ_WRITE, 0, &resource);

	std::unique_ptr<BYTE> pBuf(new BYTE[resource.RowPitch * desc.Height]);
	UINT lBmpRowPitch = lOutputDuplDesc.ModeDesc.Width * 4;
	BYTE* sptr = reinterpret_cast<BYTE*>(resource.pData);
	BYTE* dptr = pBuf.get() + resource.RowPitch * desc.Height - lBmpRowPitch;
	UINT lRowPitch = std::min<UINT>(lBmpRowPitch, resource.RowPitch);

	for (size_t h = 0; h < lOutputDuplDesc.ModeDesc.Height; ++h)
	{
		memcpy_s(dptr, lBmpRowPitch, sptr, lRowPitch);
		sptr += resource.RowPitch;
		dptr -= lBmpRowPitch;
	}

	lImmediateContext->Unmap(lDestImage, subresource);
	long g_captureSize = lRowPitch * desc.Height;
	g_iMageBuffer = new UCHAR[g_captureSize];
	g_iMageBuffer = (UCHAR*)malloc(g_captureSize);

	//Copying to UCHAR buffer 
	memcpy(g_iMageBuffer, pBuf, g_captureSize);*/
    return DUPL_RETURN_SUCCESS;
}


//      
// Release frame
//
DUPL_RETURN DUPLICATIONMANAGER::DoneWithFrame()
{
    HRESULT hr = m_DeskDupl->ReleaseFrame();
    if (FAILED(hr))
    {
        return ProcessFailure(m_Device, L"Failed to release frame in DUPLICATIONMANAGER", L"Error", hr, FrameInfoExpectedErrors);
    }

    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    return DUPL_RETURN_SUCCESS;
}

//
// Gets output desc into DescPtr
//
void DUPLICATIONMANAGER::GetOutputDesc(_Out_ DXGI_OUTPUT_DESC* DescPtr)
{
    *DescPtr = m_OutputDesc;
}
