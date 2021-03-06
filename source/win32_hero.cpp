//Include the windows header
#include <Windows.h>
#include <stdint.h>
#include <xinput.h>
#include <dsound.h>
#include <math.h>
#include "./hero.cpp"

static LPDIRECTSOUNDBUFFER secondaryBuffer;

///@todo: move to common
void GSwap(void *ptr1, void *ptr2)
{
	void *temp = ptr1;
	ptr1 = ptr2;
	ptr2 = temp;
}

typedef DWORD xinput_get_state(DWORD dwUserIndex, XINPUT_STATE *pState);
DWORD XInputGetStateStub(DWORD dwUserIndex, XINPUT_STATE *pState)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

typedef DWORD xinput_set_state(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration);
DWORD XInputSetStateStub(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
{
	return ERROR_DEVICE_NOT_CONNECTED;
}

typedef HRESULT WINAPI direct_sound_create(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter);

static xinput_get_state *XInputGetState_ = XInputGetStateStub;
static xinput_set_state *XInputSetState_ = XInputSetStateStub;

#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

static void ProcessXInputDigitalButton(GameButtonState *newState, GameButtonState *oldState, DWORD buttonBit, DWORD xinputButtonState)
{
	newState->endedDown = xinputButtonState && buttonBit;
	newState->isHalfTransition = newState->endedDown != oldState->endedDown;
}

static void LoadXInput()
{
	HMODULE XInputLibrary = LoadLibrary("xinput1_3.dll");

	if (XInputLibrary)
	{
		XInputGetState = (xinput_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
		XInputSetState = (xinput_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
	}
}

static void InitSound(HWND Window)
{
	HMODULE DSoundLibrary = LoadLibrary("dsound.dll");

	if (DSoundLibrary)
	{
		direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");
		LPDIRECTSOUND pDirectSound;
		WAVEFORMATEX waveFormat = {};

		waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		waveFormat.nChannels = 2;
		waveFormat.nSamplesPerSec = AUDIO_SAMPLE_PER_SEC;
		waveFormat.wBitsPerSample = 16;
		waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
		waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
		waveFormat.cbSize = 0;

		if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &pDirectSound, 0)))
		{
			if (SUCCEEDED(pDirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
			{
				DSBUFFERDESC primaryBufferDescription = {};
				LPDIRECTSOUNDBUFFER primaryBuffer;

				primaryBufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
				primaryBufferDescription.dwSize = sizeof(primaryBufferDescription);

				if (SUCCEEDED(pDirectSound->CreateSoundBuffer(&primaryBufferDescription, &primaryBuffer, 0)))
				{
					if (SUCCEEDED(primaryBuffer->SetFormat(&waveFormat)))
					{
						OutputDebugString("Format Set");
					}
					else
					{

					}
				}
				else
				{

				}
			}
			else
			{

			}
		}
		else
		{

		}

		DSBUFFERDESC bufferDescription = {};
		bufferDescription.dwSize = sizeof(bufferDescription);
		bufferDescription.dwFlags = 0;
		bufferDescription.dwBufferBytes = AUDIO_BUFFER_SIZE;
		bufferDescription.lpwfxFormat = &waveFormat;

		if (SUCCEEDED(pDirectSound->CreateSoundBuffer(&bufferDescription, &secondaryBuffer, NULL)))
		{

		}
	}
}

void FillSoundBuffer(DWORD byteToLock, DWORD bytesToWrite, GameOutputSoundBuffer *pSoundBuffer)
{
	void *region1 = NULL;
	DWORD region1Size = 0;
	void *region2 = NULL;
	DWORD region2Size = 0;

	if (SUCCEEDED(secondaryBuffer->Lock(byteToLock, bytesToWrite, &region1, &region1Size, &region2, &region2Size, 0)))
	{
		//todo: assert region1/2Size is valid
		DWORD region1SampleCount = region1Size / BYTES_PER_SAMPLE;
		DWORD region2SampleCount = region2Size / BYTES_PER_SAMPLE;
		INT16 *sampleOut = (INT16 *)region1;
		INT16 *samples = (INT16 *)pSoundBuffer->samples;

		for (int i = 0; i < region1SampleCount; i++)
		{
			*sampleOut++ = *(samples++);
			*sampleOut++ = *(samples++);
//			pSoundBuffer->soundData.runningSampleIndex ++;
		}

		sampleOut = (INT16 *)region2;

		for (int i = 0; i < region2SampleCount; i++)
		{
			*sampleOut++ = *(samples++);
			*sampleOut++ = *(samples++);
//			pSoundBuffer->soundData.runningSampleIndex++;
		}

		if (!(pSoundBuffer->soundData.soundPlaying))
		{
			if (FAILED(secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING)))
			{

			}
			else
			{
				pSoundBuffer->soundData.soundPlaying = true;
			}
		}

		if (FAILED(secondaryBuffer->Unlock(region1, region1Size, region2, region2Size)))
		{
			//todo: handle here
		}
	}
}

struct	Win32OffScreenBuffer
{
	BITMAPINFO bitmapInfo;
	void *bitmapMemory;
	int bitmapWidth;
	int bitmapHeight;
};

static Win32OffScreenBuffer gOffBuffer;
static bool gRunning = true;

//Devive Independent Bitmap
static void ResizeDIBSection(Win32OffScreenBuffer *buffer, int width, int height)
{
	if (buffer->bitmapMemory)
	{
		VirtualFree(buffer->bitmapMemory, 0, MEM_RELEASE);
	}

	buffer->bitmapWidth = width;
	buffer->bitmapHeight = height;

	buffer->bitmapInfo.bmiHeader.biSize = sizeof(buffer->bitmapInfo.bmiHeader);
	buffer->bitmapInfo.bmiHeader.biWidth = buffer->bitmapWidth;
	buffer->bitmapInfo.bmiHeader.biHeight = -(buffer->bitmapHeight);
	buffer->bitmapInfo.bmiHeader.biPlanes = 1;
	buffer->bitmapInfo.bmiHeader.biBitCount = 32;
	buffer->bitmapInfo.bmiHeader.biCompression = BI_RGB;

	buffer->bitmapMemory = VirtualAlloc(0, (buffer->bitmapWidth * buffer->bitmapHeight) * BYTES_PER_PIXEL, MEM_COMMIT, PAGE_READWRITE);

	return;
}

static void RefreshWindow(HDC deviceContext, RECT *windowRect, Win32OffScreenBuffer *buffer, int x, int y, int width, int height)
{
	int windowWidth = windowRect->right - windowRect->left;
	int windowHeight = windowRect->bottom - windowRect->top;

	StretchDIBits(deviceContext,
		0, 0, windowWidth, windowHeight,
		0, 0, buffer->bitmapWidth, buffer->bitmapHeight,
		buffer->bitmapMemory,
		&(buffer->bitmapInfo),
		DIB_RGB_COLORS, 
		SRCCOPY);

	return;
}


LRESULT CALLBACK
MainWindowProc(HWND   window,
	UINT   message,
	WPARAM wParam,
	LPARAM lParam)
{
	LRESULT result = 0;

	switch (message)
	{
		case WM_SIZE:
		{
			OutputDebugString("WM_SIZE\n");
		}break;
		case WM_QUIT:
		{
			OutputDebugStringA("WM_QUIT\n");
			gRunning = false;
		}break;
		case WM_DESTROY:
		{
			OutputDebugStringA("WM_DESTROY\n");
		}break;
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
		case WM_KEYDOWN:
		case WM_KEYUP:
		{
			uint32 VKCode = wParam;

			bool wasDown = ((lParam & (1 << 30)) != 0);
			bool isDown = ((lParam & (1 << 31)) == 0);

			if (wasDown != isDown)
			{
				if (VKCode == 'W')
				{

				}
				else if (VKCode == 'A')
				{

				}
				else if (VKCode == 'S')
				{

				}
				else if (VKCode == 'D')
				{

				}
				else if (VKCode == 'Q')
				{

				}
				else if (VKCode == 'E')
				{

				}
				else if (VKCode == VK_UP)
				{

				}
				else if (VKCode == VK_LEFT)
				{

				}
				else if (VKCode == VK_RIGHT)
				{

				}
				else if (VKCode == VK_ESCAPE)
				{
					OutputDebugString("ESCAPE: ");

					if (isDown)
					{
						OutputDebugString("isDown ");
					}

					if (wasDown)
					{
						OutputDebugString("wasDown");
					}

					OutputDebugString("\n");

				}
				else if (VKCode == VK_SPACE)
				{

				}
			}
			
			if (VKCode == VK_F4 && (lParam & (1 << 29)))
			{
				gRunning = false;
			}
		}break;
		case WM_CLOSE:
		{
			OutputDebugStringA("WM_CLOSE\n");
			gRunning = false;
		}break;
		case WM_PAINT:
		{
			OutputDebugStringA("WM_PAINT\n");
			
			static bool flag = true;
			PAINTSTRUCT lpPaint = {};
			RECT ClientRect;
			GetClientRect(window, &ClientRect);

			HDC context = BeginPaint(window, &lpPaint);
			static DWORD operation = BLACKNESS;

			int X = lpPaint.rcPaint.left;
			int Y = lpPaint.rcPaint.top;
			int width = lpPaint.rcPaint.right - lpPaint.rcPaint.left;
			int height = lpPaint.rcPaint.bottom - lpPaint.rcPaint.top;

			RefreshWindow(context, &ClientRect, &gOffBuffer, X, Y, width, height);

			//PatBlt(context, X, Y, width, height, operation);

			if (operation == BLACKNESS)
			{
				operation = WHITENESS;
			}
			else
			{
				operation = BLACKNESS;
			}
			EndPaint(window, &lpPaint);
		}break;
		default:
		{
			OutputDebugString("default\n");
			result = DefWindowProc(window, message, wParam, lParam);
		}
	}

	return result;

}

//Main entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {

	//Set up window class
	WNDCLASS wnd;
	wnd.cbClsExtra = 0;
	wnd.cbWndExtra = 0;
	wnd.hCursor = LoadCursor(0, IDC_ARROW);
	wnd.hIcon = LoadIcon(0, IDI_WINLOGO);
	wnd.lpszMenuName = 0;
	wnd.style = 0;
	wnd.hbrBackground = 0;
	wnd.lpfnWndProc = MainWindowProc;
	wnd.hInstance = hInstance;
	wnd.lpszClassName = "HandMadeHeroWindowClass";

	LoadXInput();

	ResizeDIBSection(&gOffBuffer, 1280, 720);

	GameInput input = {};
	GameButtonInput inputs[2];
	GameButtonInput *newInput = &inputs[0];
	GameButtonInput *oldInput = &inputs[1];

	//Register window class
	if (RegisterClass(&wnd))
	{
		//Create window
		//! This returns NULL
		HWND hWnd = CreateWindowEx(
			0,
			wnd.lpszClassName,
			"Handmade Hero",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			NULL,
			NULL,
			hInstance,
			NULL
		);

		//Simple check to see if window creation failed
		if (hWnd == NULL) {
			//Pause
			system("PAUSE");
			return -1;
		}

		//Show the window
		ShowWindow(hWnd, nCmdShow);

		InitSound(hWnd);

		//Main message loop
		MSG msg;
		int XOffset = 0;
		int YOffset = 0;
		SoundData_ soundData = {};
		LARGE_INTEGER performacnceFrequency;
		LARGE_INTEGER startCounter;
		INT64 startCycle = 0;

		soundData.Hertz = 256;
		soundData.WavePeriod = AUDIO_SAMPLE_PER_SEC / soundData.Hertz;
		soundData.halfWavePeriod = soundData.WavePeriod / 2;
		soundData.volume = 3000;
		soundData.soundPlaying = false;

		QueryPerformanceFrequency(&performacnceFrequency);
		QueryPerformanceCounter(&startCounter);

		startCycle = __rdtsc();

		while (gRunning) {

			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				if(msg.message == WM_QUIT)
				{
					gRunning = false;
				}
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			int maxControllerCount = XUSER_MAX_COUNT;
			if (maxControllerCount > ARRAY_LENGTH(input.controllers))
			{
				maxControllerCount = ARRAY_LENGTH(input.controllers);
			}
			for (int i = 0; i < maxControllerCount; i++)
			{
				XINPUT_STATE controllerState;

				if (XInputGetState(i, &controllerState) == ERROR_SUCCESS)
				{
					//The controller state is plugged in

					XINPUT_GAMEPAD *gamepad = &(controllerState.Gamepad);

					bool up = (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
					ProcessXInputDigitalButton(&newInput->up, &oldInput->up, gamepad->wButtons, XINPUT_GAMEPAD_DPAD_UP);

					bool down = (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
					ProcessXInputDigitalButton(&newInput->down, &oldInput->down, gamepad->wButtons, XINPUT_GAMEPAD_DPAD_DOWN);

					bool left = (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
					ProcessXInputDigitalButton(&newInput->left, &oldInput->left, gamepad->wButtons, XINPUT_GAMEPAD_DPAD_LEFT);

					bool right = (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
					ProcessXInputDigitalButton(&newInput->right, &oldInput->right, gamepad->wButtons, XINPUT_GAMEPAD_DPAD_RIGHT);

					bool leftShoulder = (gamepad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
					ProcessXInputDigitalButton(&newInput->leftShoulder, &oldInput->leftShoulder, gamepad->wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
				
					bool rightShoulder = (gamepad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
					ProcessXInputDigitalButton(&newInput->rightShoulder, &oldInput->rightShoulder, gamepad->wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);

					bool start = (gamepad->wButtons & XINPUT_GAMEPAD_START);
					bool back = (gamepad->wButtons & XINPUT_GAMEPAD_BACK);

					bool AButton = (gamepad->wButtons & XINPUT_GAMEPAD_A);
					bool BButton = (gamepad->wButtons & XINPUT_GAMEPAD_B);
					bool XButton = (gamepad->wButtons & XINPUT_GAMEPAD_X);
					bool YButton = (gamepad->wButtons & XINPUT_GAMEPAD_Y);

					INT16 StickX = gamepad->sThumbLX;
					INT16 StickY = gamepad->sThumbLY;

					newInput->isAnalog = true;
					newInput->startX = oldInput->endX;
					newInput->startY = oldInput->endY;

					double X;
					if (gamepad->sThumbLX < 0)
					{
						X = gamepad->sThumbLX / -32768;
					}
					else
					{
						X = gamepad->sThumbLX / 32767;
					}

					newInput->minX = newInput->maxX = newInput->endX = X;

					double Y;
					if (gamepad->sThumbLX < 0)
					{
						Y = gamepad->sThumbLY / -32768;
					}
					else
					{
						Y = gamepad->sThumbLY / 32767;
					}

					newInput->minY = newInput->maxY = newInput->endY = Y;

					input.controllers[0] = *newInput;

					XOffset += StickX >> 12;
					YOffset += StickY >> 12;
				}
				else
				{
					//The controller is not available
				}
			}

			//DirectSound output test

			DWORD playCursor = 0;
			DWORD writeCursor = 0;

			if (SUCCEEDED(secondaryBuffer->GetCurrentPosition(&playCursor, &writeCursor)))
			{
				DWORD byteToLock = soundData.runningSampleIndex * BYTES_PER_SAMPLE % AUDIO_BUFFER_SIZE;
				DWORD writePointer = 0;
				DWORD bytesToWrite = 0;

				if (byteToLock == playCursor)
				{
					if (!(soundData.soundPlaying))
					{
						bytesToWrite = AUDIO_BUFFER_SIZE;
					}
				}
				else if (byteToLock > playCursor)
				{
					bytesToWrite = AUDIO_BUFFER_SIZE - byteToLock;
					bytesToWrite += playCursor;
				}
				else
				{
					bytesToWrite = playCursor - byteToLock;
				}

				GameOutputSoundBuffer gameOutputSoundBuffer = {};
				INT16 samples[48000 * 2];
				gameOutputSoundBuffer.sampleCount = bytesToWrite / BYTES_PER_SAMPLE;
				gameOutputSoundBuffer.soundData = soundData;
				gameOutputSoundBuffer.samples = samples;

				GameUpdateAndRender((GameOffScreenBuffer *)&gOffBuffer, &gameOutputSoundBuffer, &input);

				FillSoundBuffer(byteToLock, bytesToWrite, &gameOutputSoundBuffer);

			}

			RECT ClientRect;
			GetClientRect(hWnd, &ClientRect);

			HDC context = GetDC(hWnd);

			RefreshWindow(context, &ClientRect, &gOffBuffer, 0, 0, gOffBuffer.bitmapWidth, gOffBuffer.bitmapHeight);
			
			ReleaseDC(hWnd, context);

			XOffset++;
			LARGE_INTEGER endCounter;
			INT64 endCycle = __rdtsc();
			QueryPerformanceCounter(&endCounter);
			INT64 elapsedCycles = endCycle - startCycle;
			INT64 counterDifference = endCounter.QuadPart - startCounter.QuadPart;
			INT64 timeElapsed = (1000 * counterDifference / performacnceFrequency.QuadPart);
			INT64 framesPerSecond = performacnceFrequency.QuadPart / counterDifference;
			INT64 millionCyclesEPF = elapsedCycles / (1000 * 1000);
			char stringBuffer[512];
			wsprintf(stringBuffer, "\nTime Elapsed is: %ldms/f FPS: %ldf/s MillionCyclesElapsedPerFrame: %dmc/f\n", timeElapsed, framesPerSecond, millionCyclesEPF);
			OutputDebugString(stringBuffer);
			
			startCounter = endCounter;
			startCycle = endCycle;

			GSwap((void*)newInput, (void *)oldInput);
		}
	}

	return S_OK;
}