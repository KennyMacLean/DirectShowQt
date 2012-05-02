#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <QtGui>
#include <streams.h>
#include <string>
#include <vector>

//extern HWND g_hWnd;
//extern int blah;
//extern IMediaEventEx *mediaEvent;

using namespace std;

#define ReleaseCOM(x) { if(x){ x->Release();x = 0; } }

#define WM_GRAPHNOTIFY WM_APP + 1  

#define REMOVE_SOUND

extern WCHAR rootDirectory[512];


EXTERN_C const IID IID_ISampleGrabberCB;
MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")ISampleGrabberCB : public IUnknown
{
public:    
	virtual HRESULT STDMETHODCALLTYPE SampleCB(         double SampleTime,        IMediaSample *pSample) = 0;
	virtual HRESULT STDMETHODCALLTYPE BufferCB(         double SampleTime,        BYTE *pBuffer,        long BufferLen) = 0;    
};

EXTERN_C const CLSID CLSID_NullRenderer;
EXTERN_C const CLSID CLSID_SampleGrabber;
EXTERN_C const IID IID_ISampleGrabber;

MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")ISampleGrabber : public IUnknown
{
public:    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
		   virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *pType) = 0;
		   virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *pType) = 0;
		   virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
		   virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *pBufferSize, long *pBuffer) = 0;
		   virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **ppSample) = 0;
		   virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB *pCallback,long WhichMethodToCallback) = 0;
};

class VideoCapture;

class FrameRequest
{
public:
	double time;
	wstring filename;
	string id;
	VideoCapture *videoCapturer;

	FrameRequest()
	{
		time = 0.0f;
		filename = L"";
		id = "";
		videoCapturer = NULL;
	}

	FrameRequest(double time, string id)
	{
		this->time = time;
		this->filename = filename;
		this->id = id;
		this->videoCapturer = NULL;
	}
	FrameRequest(const FrameRequest &from)
	{
		time = from.time;
		filename = from.filename;
		id = from.id;
		videoCapturer = from.videoCapturer;
	}
};


class VideoCapture : public QWidget
{
	Q_OBJECT

	IGraphBuilder *graphBuilder;
	IMediaControl *mediaControl;
	IMediaPosition *mediaPosition;
	IMediaEventEx *mediaEvent;
	
	//IBasicAudio *audioControl;

	LONG	vidWidth;
	LONG    vidHeight;
	LONG    vidPitch; 

	double duration;
	double playhead;
	double storedPlayhead;
	//WCHAR filename[512];
	wstring filename;
	
	FrameRequest frameRequest;
	bool frameRequestInProgress;
	

	AM_MEDIA_TYPE mt;

	IBaseFilter *grabberBase, *NULLRenderer;
	ISampleGrabber *videoGrabber;

	long videoBufferSize; 
	long *videoBuffer; 
	


public:
	//int flags;
	//bool empty;
	QImage *playingVideoTex;


	VideoCapture(wstring videoFilename)
	{
		frameRequestInProgress = false;
		filename = videoFilename;
		playingVideoTex = NULL;
		graphBuilder = NULL;
		mediaControl = NULL;
		mediaPosition = NULL;
		mediaEvent = NULL;

		videoBufferSize = 0;
		videoBuffer = NULL;
		grabberBase = NULL;
		NULLRenderer = NULL;
		videoGrabber = NULL;
		//audioControl = NULL;
		storedPlayhead = 0;
		//empty = true;
		
		CoInitialize(NULL);

		initDirectShow();
	}


	~VideoCapture()
	{
		cleanupDirectShow();
		if(playingVideoTex)
		{
			delete playingVideoTex;
			playingVideoTex = NULL;
		}
		CoUninitialize();
	}

	void flushVideo()
	{
		cleanupDirectShow();
		//empty = true;
	}

	void setVideoSource(wstring newFilename)
	{
		cleanupDirectShow();

		filename = newFilename;

		initDirectShow();
	}

	
	bool winEvent(MSG *message, long *result)
	{
		switch (message->message)
		{
			case WM_GRAPHNOTIFY:
				long evCode;
				LONG_PTR param1, param2;
				while (SUCCEEDED(mediaEvent->GetEvent(&evCode, &param1, &param2, 0)))
				{
					mediaEvent->FreeEventParams(evCode, param1, param2);
					// unintuitively this is the code we want. When the playhead is moved
					// to the frame that we want to capture, the DirectShow needs to pause first and this
					// is the event that is generated when the pause action is complete
					if (evCode == EC_PAUSED)
					{
						// capture frame
						renderFrame();
						return true;
					}
				} 
				return true;
		}
		return false;
	}

	bool sendRequest(FrameRequest request)
	{
		if(frameRequestInProgress)
			return false;

		//if(empty)
		//	initDirectShow();

		frameRequestInProgress = true;

		frameRequest = request;

		mediaControl->Run();

		setPlayhead(request.time);

		//renderFrame();

		return true;
	}

	void finishedCapturing()
	{
		//if(!empty)
			mediaControl->Stop();
	}

signals:
	void frameCaptured(FrameRequest request);


protected:
	IPin* getPin(IBaseFilter *pFilter, PIN_DIRECTION PinDir)
	{    
		BOOL       bFound = FALSE;    
		IEnumPins  *pEnum;    
		IPin       *pPin;    
		HRESULT hr = pFilter->EnumPins(&pEnum);   
		if (FAILED(hr))    
		{        
			return NULL;   
		}	
		int i = 0;    
		while(pEnum->Next(1, &pPin, 0) == S_OK)   
		{        
			PIN_DIRECTION PinDirThis;        
			pPin->QueryDirection(&PinDirThis);        
			bFound = (PinDir == PinDirThis);		
			if(bFound)            
				break;        
			pPin->Release();    
		}    
		pEnum->Release();    
		return (bFound ? pPin : NULL);  
	}


	void initDirectShow()
	{  

		duration = 0.0f;
		playhead = storedPlayhead;
		storedPlayhead = 0.0f;


		CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&graphBuilder);
		CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&grabberBase);
		
		grabberBase->QueryInterface(IID_ISampleGrabber, (void**)&videoGrabber);  
		ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE)); 

		mt.majortype = MEDIATYPE_Video;   
		mt.subtype = MEDIASUBTYPE_RGB24;
		mt.formattype = FORMAT_VideoInfo;  


		videoGrabber->SetMediaType(&mt);
		videoGrabber->SetBufferSamples(TRUE);  
		videoGrabber->SetOneShot(FALSE);

	 
		CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&NULLRenderer);
		graphBuilder->RenderFile(filename.c_str(), NULL);
		graphBuilder->AddFilter(NULLRenderer, L"Null Renderer");  
		graphBuilder->AddFilter(grabberBase, L"Sample Grabber"); 
	  
		graphBuilder->QueryInterface(IID_IMediaControl, (void**)&mediaControl);  
		graphBuilder->QueryInterface(IID_IMediaEvent, (void**)&mediaEvent);  
		graphBuilder->QueryInterface(IID_IMediaPosition, (void**)&mediaPosition); 

		
		mediaEvent->SetNotifyWindow((OAHWND)winId(), WM_GRAPHNOTIFY, 0);
		

		//locate default video renderer	
		IBaseFilter* pVidRenderer = NULL;
		graphBuilder->FindFilterByName(L"Video Renderer", &pVidRenderer);

		if(pVidRenderer)	
		{		
			//get input pin of video renderer		
			IPin* ipin = getPin(pVidRenderer, PINDIR_INPUT);		
			IPin* opin = NULL;	

			//find out who the renderer is connected to and disconnect from them		
			ipin->ConnectedTo(&opin);		
			ipin->Disconnect();		
			opin->Disconnect();		
			ReleaseCOM(ipin);		

			//remove the default renderer from the graph				
			graphBuilder->RemoveFilter(pVidRenderer);		
			ReleaseCOM(pVidRenderer);		

			//see if the video renderer was originally connected to 		
			//a color space converter		
			IBaseFilter* pColorConverter = NULL;		
			graphBuilder->FindFilterByName(L"Color Space Converter", &pColorConverter);		
			if(pColorConverter)		
			{			
				ReleaseCOM(opin);			
				//remove the converter from the graph as well			
				ipin = getPin(pColorConverter, PINDIR_INPUT);			
				ipin->ConnectedTo(&opin);			
				ipin->Disconnect();			
				opin->Disconnect();			
				ReleaseCOM(ipin);						
				graphBuilder->RemoveFilter(pColorConverter);			
				ReleaseCOM(pColorConverter);		
			}	
#ifdef REMOVE_SOUND
			{
				IBaseFilter* pAudioFilter = NULL;		
				graphBuilder->FindFilterByName(L"Default DirectSound Device", &pAudioFilter);	
				if(pAudioFilter)
				{
					IPin* outPin = NULL;
				
					ipin = getPin(pAudioFilter, PINDIR_INPUT);			
					ipin->ConnectedTo(&outPin);			
					PIN_INFO pinInfo;
					outPin->QueryPinInfo(&pinInfo);
					pAudioFilter = pinInfo.pFilter;
					ReleaseCOM(outPin);	
					ReleaseCOM(ipin);
				
					ipin = getPin(pAudioFilter, PINDIR_INPUT);			
					ipin->ConnectedTo(&outPin);			
					ipin->Disconnect();			
					outPin->Disconnect();			
					ReleaseCOM(outPin);	
					ReleaseCOM(ipin);	
					graphBuilder->RemoveFilter(pAudioFilter);			
					ReleaseCOM(pAudioFilter);								
				}
			}
#endif

			//get the input pin of the sample grabber		
			ipin = getPin(grabberBase, PINDIR_INPUT);	

			//connect the filter that was originally connected to the default renderer		
			//to the sample grabber		
			graphBuilder->Connect(opin, ipin);		
			ReleaseCOM(ipin);		
			ReleaseCOM(opin);		

			//get output pin of sample grabber		
			opin = getPin(grabberBase, PINDIR_OUTPUT);	

			//get input pin of null renderer		
			ipin = getPin(NULLRenderer, PINDIR_INPUT);	

			//connect them		
			graphBuilder->Connect(opin, ipin);		
			ReleaseCOM(ipin);		
			ReleaseCOM(opin);	
		}	

		mediaControl->Run();

		//empty = false;
		
	}


	void setPlayhead(float time)
	{

		playhead = time;
		if(S_FALSE == mediaPosition->put_CurrentPosition(time))
		{
			playhead = time;
		}
	}
	
	void doRenderSample( BYTE * bmpBuffer )
	{
		DWORD * bmpPixel = NULL;
		
		unsigned int dwordWidth = vidWidth / 4; // aligned width of the row, in DWORDS (pixel by 3 bytes over sizeof(DWORD))

		if(!playingVideoTex)
			return;

		// ok... we are getting 3 DWORDs (bmpPixel[3]) and extracting 4 pixels from it
		for(unsigned int row = 0; row < (UINT)vidHeight; row++, bmpBuffer  += vidPitch)
		{
			bmpPixel = (DWORD*)bmpBuffer;
			
			for(unsigned int col = 0; col < dwordWidth; col++, bmpPixel +=3)
			{
				playingVideoTex->setPixel(col*4,    vidHeight - row-1, bmpPixel[0] | 0xFF000000);
				playingVideoTex->setPixel(col*4 + 1,vidHeight - row-1, ((bmpPixel[1]<<8)  | 0xFF000000) | (bmpPixel[0]>>24));
				playingVideoTex->setPixel(col*4 + 2,vidHeight - row-1, ((bmpPixel[2]<<16) | 0xFF000000) | (bmpPixel[1]>>16));
				playingVideoTex->setPixel(col*4 + 3,vidHeight - row-1, 0xFF000000 | (bmpPixel[2]>>8));

			}
		}
	}

	void renderFrame()
	{  
		if(!videoGrabber || !frameRequestInProgress )
			return;
		
		AM_MEDIA_TYPE mediaType;

		
		videoGrabber->GetCurrentBuffer(&videoBufferSize, videoBuffer);
		videoGrabber->GetConnectedMediaType(&mediaType);

		if (mediaType.formattype == FORMAT_VideoInfo && playingVideoTex == NULL)
		{
			// really strange if statement??	
			if (mediaType.cbFormat >= sizeof(VIDEOINFOHEADER))
			{
				VIDEOINFOHEADER *pVih = reinterpret_cast<VIDEOINFOHEADER*>(mediaType.pbFormat);
				vidWidth = pVih->bmiHeader.biWidth;
				vidHeight = pVih->bmiHeader.biHeight;
				vidPitch  = (vidWidth * 3 + 3) & ~(3); 

				mediaPosition->put_CurrentPosition(playhead);

				mediaPosition->get_Duration(&duration);

				playingVideoTex = new QImage(vidWidth,vidHeight,QImage::Format_ARGB32);
			}
		}

		if(videoBufferSize !=0 && videoBuffer ==NULL)
		{
			videoBuffer = new long[videoBufferSize];
			// Direct show is very quirky, the first time you call GetCurrentBuffer you get info on what to set the 
			// parameters to (the next time you call) so... a tiny bit of recursion is required
			renderFrame();
			return;
		}
		else if(videoBuffer !=NULL && playingVideoTex != NULL)
		{
			doRenderSample((BYTE*)videoBuffer);
		}

		frameRequestInProgress = false;

		FrameRequest r;// = frameRequest;
		r.videoCapturer = this;
		r.filename = filename;
		r.time = frameRequest.time;
		r.id = frameRequest.id;

		emit frameCaptured(r);

	}

	void cleanupDirectShow()
	{
		if(videoBuffer)
		{
			delete[] videoBuffer;
			videoBuffer = NULL;
		}
		if(mediaControl)
			mediaControl->Stop();
	
		videoBufferSize = 0;
	
		//ReleaseCOM(playingVideoTex);
		if(playingVideoTex)
		{
			delete playingVideoTex;
			playingVideoTex = NULL;
		}

		ReleaseCOM(mediaControl);
		ReleaseCOM(mediaPosition);
		ReleaseCOM(mediaEvent);

		ReleaseCOM(grabberBase);
		ReleaseCOM(NULLRenderer);
		ReleaseCOM(videoGrabber);
		//ReleaseCOM(audioControl);
		ReleaseCOM(graphBuilder);
	}
	

};




#endif