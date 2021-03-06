#include "StdAfx.h"
#include "Microphone.h"
#include <fstream>
#include "Folders.h"

using namespace std;

namespace WebinariaApplication
{
	namespace WebinariaUserInterface
	{
		HINSTANCE CMicrophone::HInstance = 0;
		const wchar_t MCLASSNAME[] = L"WUI::Microphone";
		int CMicrophone::MeterVolume = 0;
		int CMicrophone::MinVolume = 0;
		int CMicrophone::MaxVolume = 0;
		HWND CMicrophone::HProgress = 0;
		bool	CMicrophone::Show = false;
		int CMicrophone::i = 0;
		HMIXER CMicrophone::hMixer = 0;
		HWAVEIN	CMicrophone::waveInHandle = NULL;
		WAVEHDR	CMicrophone::waveHeader[2];   
		WAVEFORMATEX CMicrophone::waveFormat;

		// Here we import the function from USER32.DLL 
		HMODULE CMicrophone::hUser32 = GetModuleHandle(_T("USER32.DLL")); 
		lpfnSetLayeredWindowAttributes CMicrophone::SetLayeredWindowAttributes = 
		(lpfnSetLayeredWindowAttributes)GetProcAddress(hUser32, "SetLayeredWindowAttributes"); 

		//////////////////////////////////////////////////////////////////////////
		//							Public methods								//
		//////////////////////////////////////////////////////////////////////////
		// Default constructor
		CMicrophone::CMicrophone(HWND hWnd, HINSTANCE hInstance):	event_handler(HANDLE_BEHAVIOR_EVENT),
																	Volume(0),
																	Mute(false),
																	Caption(NULL),
																	hMainWnd(hWnd)
		{
			Caption = new wchar_t[12];
			wcscpy(Caption, L"Microphone");

			// Initialize global window class
			RegisterWindowClass(hInstance);

			// Set window style
			UINT style = WS_CHILDWINDOW | WS_POPUPWINDOW | WS_EX_LAYERED;

			// Create main window
			HWnd = CreateWindowW(MCLASSNAME, Caption, style ,400, 400, 130, 200, hMainWnd, NULL, HInstance, NULL);

			// Set progress bar style
			style = WS_CHILD | WS_VISIBLE | PBS_SMOOTH | PBS_VERTICAL;

			// Create progress bar
			HProgress = CreateWindowW(	PROGRESS_CLASS, 
										NULL,
										style,
										32,
										47,
										20,
										96,
										HWnd,
										NULL,
										NULL,
										NULL);

			// Save window associated data
			self(HWnd,this);

			// Callback for HTMLayout document elements
			HTMLayoutSetCallback(HWnd,&callback,this);

            TCHAR ui[MAX_PATH];
            Folders::FromRecorderUI(ui, _T("micro.htm"));

			// Loading document elements
			if(HTMLayoutLoadFile(HWnd, ui))
			{
				htmlayout::dom::element r = Root();

				Body					= r.find_first("body");
				WindowCaption			= r.get_element_by_id("caption");
				CloseButton				= r.get_element_by_id("close");

				OKButton				= r.get_element_by_id("OK");
				VolumeSlider			= r.get_element_by_id("Volume");
				MuteCheckBox			= r.get_element_by_id("Mute");

				htmlayout::attach_event_handler(r, this);

				SetWindowCaption();
			}
			else
			{
				//problem with resource loading
			}
		}

		// Virtual destructor
		CMicrophone::~CMicrophone(void)
		{
			if (Caption != NULL)
				delete[] Caption;
			// Post close message
			::PostMessage(HWnd, WM_CLOSE, 0,0); 
			UnregisterClass(MCLASSNAME,HInstance);
		}

		// Show popup dialog
		bool CMicrophone::ShowPopupDialog()
		{
			if (Show)
				return false;
			GetVolume();
			if (!GetMute())
				MuteCheckBox.set_attribute("disabled",L"false");
			GetMeter(HWnd);

			SendMessage (HProgress, PBM_SETRANGE32, 0, 512);

			::ShowWindow(HWnd,SW_SHOWNORMAL);
			Show = true;

			MMRESULT err;	               

            // Clear out both of our WAVEHDRs. At the very least, waveInPrepareHeader() expects the dwFlags field to be cleared
            ZeroMemory(&waveHeader[0], sizeof(WAVEHDR) * 2);

            // Initialize the WAVEFORMATEX for 16-bit, 44KHz, stereo. That's what I want to record 
            waveFormat.wFormatTag      = WAVE_FORMAT_PCM;
            waveFormat.nChannels       = 2;
            waveFormat.nSamplesPerSec  = 44100;
            waveFormat.wBitsPerSample  = 16;
            waveFormat.nBlockAlign     = waveFormat.nChannels * (waveFormat.wBitsPerSample/8);
            waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
            waveFormat.cbSize          = 0;

            // Open the default WAVE In Device, specifying my callback. Note that if this device doesn't
            // support 16-bit, 44KHz, stereo recording, then Windows will attempt to open another device
            // that does. So don't make any assumptions about the name of the device that opens. After
            // waveInOpen returns the handle, use waveInGetID to fetch its ID, and then waveInGetDevCaps
            // to retrieve the actual name	                         
            if ((err = waveInOpen(&waveInHandle, WAVE_MAPPER, &waveFormat, (DWORD)HWnd, 0, CALLBACK_WINDOW)) != 0)
            {
				CError::ErrMsg(HWnd, L"Can't open WAVE In Device!");
                return false;
            }                        

	        // Allocate, prepare, and queue two buffers that the driver can use to record blocks of audio data.
	        // (ie, We're using double-buffering. You can use more buffers if you'd like, and you should do that
	        // if you suspect that you may lag the driver when you're writing a buffer to disk and are too slow
	        // to requeue it with waveInAddBuffer. With more buffers, you can take your time requeueing
	        // each).
	        //
	        // I'll allocate 2 buffers large enough to hold 2 seconds worth of waveform data at 44Khz. NOTE:
	        // Just to make it easy, I'll use 1 call to VirtualAlloc to allocate both buffers, but you can
	        // use 2 separate calls since the buffers do NOT need to be contiguous. You should do the latter if
	        // using many, large buffers
	        waveHeader[1].dwBufferLength = waveHeader[0].dwBufferLength = 512;
	        if (!(waveHeader[0].lpData = (char*)VirtualAlloc(0, (waveHeader[0].dwBufferLength << 1), MEM_COMMIT, PAGE_READWRITE)))
	        {
		        CError::ErrMsg(HWnd, L"ERROR: Can't allocate memory for WAVE buffer!\n");
                waveInClose(waveInHandle);
                return false;
	        }

		    // Fill in WAVEHDR fields for buffer starting address. We've already filled in the size fields above */
		    waveHeader[1].lpData = waveHeader[0].lpData + waveHeader[0].dwBufferLength;

		    // Leave other WAVEHDR fields at 0 

		    // Prepare the 2 WAVEHDR's 
		    if ((err = waveInPrepareHeader(waveInHandle, &waveHeader[0], sizeof(WAVEHDR))))
		    {
                waveInClose(waveInHandle);
			    CError::ErrMsg(HWnd, L"Error preparing WAVEHDR 1! -- %08X\n", err);
                VirtualFree (waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE); 
                return false;
		    }

			if ((err = waveInPrepareHeader(waveInHandle, &waveHeader[1], sizeof(WAVEHDR))))
			{
                waveInClose(waveInHandle);
				CError::ErrMsg(HWnd, L"Error preparing WAVEHDR 2! -- %08X\n", err);
                VirtualFree (waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE); 
                return false;
			}
			
			// Queue first WAVEHDR (recording hasn't started yet) 
			if ((err = waveInAddBuffer(waveInHandle, &waveHeader[0], sizeof(WAVEHDR))))
			{
                waveInClose(waveInHandle);
				CError::ErrMsg(HWnd, L"Error queueing WAVEHDR 1! -- %08X\n", err);
                VirtualFree (waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE); 
                return false;
			}

			// Queue second WAVEHDR 
			if ((err = waveInAddBuffer(waveInHandle, &waveHeader[1], sizeof(WAVEHDR))))
			{
                waveInClose(waveInHandle);
				CError::ErrMsg(HWnd, L"Error queueing WAVEHDR 2! -- %08X\n", err);
                VirtualFree (waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE); 
                return false;
			}

            // Start recording
			if ((err = waveInStart(waveInHandle)))
			{
				CError::ErrMsg(HWnd, L"Error starting record! -- %08X\n", err);
                waveInClose(waveInHandle);							
                VirtualFree (waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE); 
                return false;
			}
			
			return true;
		}

		// Get microphone meter
		bool CMicrophone::GetMeter(HWND hWnd)
		{
		    int inDevIdx = -1;
			int	OldMeter = MeterVolume;

			// Open the mixer. This opens the mixer with a deviceID of 0. If you
			// have a single sound card/mixer, then this will open it. If you have
			// multiple sound cards/mixers, the deviceIDs will be 0, 1, 2, and
			// so on.
			MMRESULT rc = mixerOpen(&hMixer,0,0,NULL,MIXER_OBJECTF_MIXER);
			if (MMSYSERR_NOERROR != rc) 
			{
				CError::ErrMsg(hWnd, L"Couldn't open the mixer.");
			}

			// get dwLineID
			MIXERLINE mxl;
			mxl.cbStruct        = sizeof(MIXERLINE);
			mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_WAVEIN;

			if (mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_COMPONENTTYPE) != MMSYSERR_NOERROR)
			{
				mixerClose (hMixer);
				return FALSE;
			}

			// get dwControlID
			MIXERCONTROL      mxc;
			MIXERLINECONTROLS mxlc;
			DWORD             dwControlType = MIXERCONTROL_CONTROLTYPE_MIXER;

			mxlc.cbStruct      = sizeof(MIXERLINECONTROLS);
			mxlc.dwLineID      = mxl.dwLineID;
			mxlc.dwControlType = dwControlType;
			mxlc.cControls     = 0;
			mxlc.cbmxctrl      = sizeof(MIXERCONTROL);
			mxlc.pamxctrl      = &mxc;

			if (mixerGetLineControls((HMIXEROBJ)hMixer, &mxlc, MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE) != MMSYSERR_NOERROR)
			{
				// no mixer, try MUX
				dwControlType      = MIXERCONTROL_CONTROLTYPE_MUX;
				mxlc.cbStruct      = sizeof(MIXERLINECONTROLS);
				mxlc.dwLineID      = mxl.dwLineID;
				mxlc.dwControlType = dwControlType;
				mxlc.cControls     = 0;
				mxlc.cbmxctrl      = sizeof(MIXERCONTROL);
				mxlc.pamxctrl      = &mxc;
				if (mixerGetLineControls((HMIXEROBJ)hMixer, &mxlc, MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE) != MMSYSERR_NOERROR)
				{
					mixerClose (hMixer);
					return FALSE;
				}
			}

			if (mxc.cMultipleItems <= 0)
			{
				mixerClose (hMixer);
				return FALSE;
			}

			// get the index of the inDevice from available controls
			MIXERCONTROLDETAILS_LISTTEXT*  pmxcdSelectText = new MIXERCONTROLDETAILS_LISTTEXT[mxc.cMultipleItems];

			if (pmxcdSelectText != NULL)
			{
				MIXERCONTROLDETAILS mxcd;

				mxcd.cbStruct       = sizeof(MIXERCONTROLDETAILS);
				mxcd.dwControlID    = mxc.dwControlID;
				mxcd.cChannels      = 1;
				mxcd.cMultipleItems = mxc.cMultipleItems;
				mxcd.cbDetails      = sizeof(MIXERCONTROLDETAILS_LISTTEXT);
				mxcd.paDetails      = pmxcdSelectText;

				if (mixerGetControlDetails ((HMIXEROBJ)hMixer, &mxcd, MIXER_OBJECTF_HMIXER | MIXER_GETCONTROLDETAILSF_LISTTEXT) == MMSYSERR_NOERROR)
				{
					// determine which controls the inputDevice source line
					DWORD dwi;
					for (dwi = 0; dwi < mxc.cMultipleItems; dwi++)
					{
						// get the line information
						MIXERLINE mxl;
						mxl.cbStruct = sizeof(MIXERLINE);
						mxl.dwLineID = pmxcdSelectText[dwi].dwParam1;
						if (mixerGetLineInfo ((HMIXEROBJ)hMixer, &mxl, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_LINEID) == MMSYSERR_NOERROR && mxl.dwComponentType == MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE)
						{
							// found, dwi is the index.
							inDevIdx = dwi;
		//					break;
						}
					}

				}

				delete []pmxcdSelectText;
			}

			if (inDevIdx < 0)
			{
				mixerClose (hMixer);
				return FALSE;
			}

			// get all the values first
			MIXERCONTROLDETAILS_BOOLEAN* pmxcdSelectValue = new MIXERCONTROLDETAILS_BOOLEAN[mxc.cMultipleItems];

			if (pmxcdSelectValue != NULL)
			{
				MIXERCONTROLDETAILS mxcd;
				mxcd.cbStruct       = sizeof(MIXERCONTROLDETAILS);
				mxcd.dwControlID    = mxc.dwControlID;
				mxcd.cChannels      = 1;
				mxcd.cMultipleItems = mxc.cMultipleItems;
				mxcd.cbDetails      = sizeof(MIXERCONTROLDETAILS_BOOLEAN);
				mxcd.paDetails      = pmxcdSelectValue;
				if (mixerGetControlDetails((HMIXEROBJ)hMixer, &mxcd, MIXER_OBJECTF_HMIXER | MIXER_GETCONTROLDETAILSF_VALUE) == MMSYSERR_NOERROR)
				{
					// ASSERT(m_dwControlType == MIXERCONTROL_CONTROLTYPE_MIXER || m_dwControlType == MIXERCONTROL_CONTROLTYPE_MUX);

					// MUX restricts the line selection to one source line at a time.
					if (dwControlType == MIXERCONTROL_CONTROLTYPE_MUX)
					{
						ZeroMemory(pmxcdSelectValue, mxc.cMultipleItems * sizeof(MIXERCONTROLDETAILS_BOOLEAN));
					}

					// Turn on this input device
					pmxcdSelectValue[inDevIdx].fValue = 0x1;

					mxcd.cbStruct       = sizeof(MIXERCONTROLDETAILS);
					mxcd.dwControlID    = mxc.dwControlID;
					mxcd.cChannels      = 1;
					mxcd.cMultipleItems = mxc.cMultipleItems;
					mxcd.cbDetails      = sizeof(MIXERCONTROLDETAILS_BOOLEAN);
					mxcd.paDetails      = pmxcdSelectValue;
					if (mixerSetControlDetails ((HMIXEROBJ)hMixer, &mxcd, MIXER_OBJECTF_HMIXER | MIXER_SETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR)
					{
						delete []pmxcdSelectValue;
						mixerClose (hMixer);
						return FALSE;
					}
				}

				delete []pmxcdSelectValue;
			}

			mixerClose(hMixer);

			return true;
		}

		// Get microphone volume
		bool CMicrophone::GetVolume()
		{
			MMRESULT rc = mixerOpen(&hMixer,0,0,NULL,MIXER_OBJECTF_MIXER);
			if (MMSYSERR_NOERROR != rc) 
			{
				CError::ErrMsg(hMainWnd, L"Couldn't open the mixer.");
				return false;
			}
			MIXERLINECONTROLS mxlc;
			tMIXERCONTROLDETAILS mxcd;
			tMIXERCONTROLDETAILS_UNSIGNED vol;
			MIXERCONTROL mxc;
			MIXERLINE mxl;
			int intRet;
			int nMixerDevs;

			// Check if Mixer is available
			nMixerDevs = mixerGetNumDevs();
			if (nMixerDevs < 1)
				return false;

			if (hMixer)
			{
				mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE;
				mxl.cbStruct = sizeof(mxl);

				// Get line info
				intRet = mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_GETLINEINFOF_COMPONENTTYPE);

				if (intRet == MMSYSERR_NOERROR)
				{
				  ZeroMemory(&mxlc, sizeof(mxlc));
				  mxlc.cbStruct = sizeof(mxlc);
				  mxlc.dwLineID = mxl.dwLineID;
				  mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
				  mxlc.cControls = 1;
				  mxlc.cbmxctrl = sizeof(mxc);

				  mxlc.pamxctrl = &mxc;
				  intRet = mixerGetLineControls((HMIXEROBJ)hMixer, &mxlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);

				  if (intRet == MMSYSERR_NOERROR)
				  {
					ZeroMemory(&mxcd, sizeof(mxcd));
					mxcd.dwControlID = mxc.dwControlID;
					mxcd.cbStruct = sizeof(mxcd);
					mxcd.cMultipleItems = 0;
					vol.dwValue = 0;
					mxcd.cbDetails = sizeof(vol);
					mxcd.paDetails = &vol;
					mxcd.cChannels = 1;

					MinVolume = mxlc.pamxctrl->Bounds.lMinimum;
					MaxVolume = mxlc.pamxctrl->Bounds.lMaximum;
					SendMessage(HProgress, PBM_SETRANGE32, 0, 512);

					intRet = mixerGetControlDetails((HMIXEROBJ)hMixer, &mxcd, MIXER_GETCONTROLDETAILSF_VALUE);
					if (intRet != MMSYSERR_NOERROR)
					{
						CError::ErrMsg(HWnd, L"Get Volume Error");
						return false;
					}
					else
					{
						Volume = vol.dwValue;
						htmlayout::value_t val ((int)((float)Volume /(float)(MaxVolume - MinVolume) * 100.0f));
						htmlayout::set_value(VolumeSlider, val);
					}
				  }
				  else
				  {
						CError::ErrMsg(HWnd, L"Get Line Info Error");
						return false;
				  }
				  mixerClose(hMixer);
				}
			}
			return true;
		}

		// Set microphone volume
		bool CMicrophone::SetVolume()
		{
			MIXERLINECONTROLS mxlc;
			tMIXERCONTROLDETAILS mxcd;
			tMIXERCONTROLDETAILS_UNSIGNED vol;
			MIXERCONTROL mxc;
			MIXERLINE mxl;
			int intRet;

			MMRESULT rc = mixerOpen(&hMixer,0,0,NULL,MIXER_OBJECTF_MIXER);
			if (MMSYSERR_NOERROR != rc) 
			{
				CError::ErrMsg(hMainWnd, L"Couldn't open the mixer.");
				return false;
			}
			else
			{
				mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE;
				mxl.cbStruct = sizeof(mxl);

				// Get line info
				intRet = mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_GETLINEINFOF_COMPONENTTYPE);

				if (intRet == MMSYSERR_NOERROR)
				{
				  ZeroMemory(&mxlc, sizeof(mxlc));
				  mxlc.cbStruct = sizeof(mxlc);
				  mxlc.dwLineID = mxl.dwLineID;
				  mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
				  mxlc.cControls = 1;
				  mxlc.cbmxctrl = sizeof(mxc);

				  mxlc.pamxctrl = &mxc;
				  intRet = mixerGetLineControls((HMIXEROBJ)hMixer, &mxlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);

				  if (intRet == MMSYSERR_NOERROR)
				  {
					ZeroMemory(&mxcd, sizeof(mxcd));
					mxcd.dwControlID = mxc.dwControlID;
					mxcd.cbStruct = sizeof(mxcd);
					mxcd.cMultipleItems = 0;
					mxcd.cbDetails = sizeof(vol);
					mxcd.paDetails = &vol;
					mxcd.cChannels = 1;

					vol.dwValue = Volume;

					intRet = mixerSetControlDetails((HMIXEROBJ)hMixer, &mxcd, MIXER_SETCONTROLDETAILSF_VALUE);
					if (intRet != MMSYSERR_NOERROR)
						CError::ErrMsg(HWnd, L"Set Volume Error");
				  }
				  else
				  {
					CError::ErrMsg(HWnd, L"Get Line Info Error");
					return false;
				  }
				  mixerClose(hMixer);
				}
			}
			return true;
		}

		// Get microphone volume mute
		bool CMicrophone::GetMute()
		{
			MIXERLINECONTROLS mxlc;
			tMIXERCONTROLDETAILS mxcd;
			tMIXERCONTROLDETAILS_BOOLEAN mute;
			MIXERCONTROL mxc;
			MIXERLINE mxl;
			int intRet;
			int nMixerDevs;

			// Check if Mixer is available
			nMixerDevs = mixerGetNumDevs();
			if (nMixerDevs < 1)
				return false;

			MMRESULT rc = mixerOpen(&hMixer,0,0,NULL,MIXER_OBJECTF_MIXER);
			if (MMSYSERR_NOERROR != rc) 
			{
				CError::ErrMsg(hMainWnd, L"Couldn't open the mixer.");
				return false;
			}
			else
			{
				mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE;
				mxl.cbStruct = sizeof(mxl);

				// Get line info
				intRet = mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_GETLINEINFOF_COMPONENTTYPE);

				if (intRet == MMSYSERR_NOERROR)
				{
				  ZeroMemory(&mxlc, sizeof(mxlc));
				  mxlc.cbStruct = sizeof(mxlc);
				  mxlc.dwLineID = mxl.dwLineID;
				  mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
				  mxlc.cControls = 1;
				  mxlc.cbmxctrl = sizeof(mxc);

				  mxlc.pamxctrl = &mxc;
				  intRet = mixerGetLineControls((HMIXEROBJ)hMixer, &mxlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);

				  if (intRet == MMSYSERR_NOERROR)
				  {
					ZeroMemory(&mxcd, sizeof(mxcd));
					mxcd.dwControlID = mxc.dwControlID;
					mxcd.cbStruct = sizeof(mxcd);
					mxcd.cMultipleItems = 0;
					mute.fValue = false;
					mxcd.cbDetails = sizeof(mute);
					mxcd.paDetails = &mute;
					mxcd.cChannels = 1;

					intRet = mixerGetControlDetails((HMIXEROBJ)hMixer, &mxcd, MIXER_GETCONTROLDETAILSF_VALUE);
					if (intRet != MMSYSERR_NOERROR)
						CError::ErrMsg(HWnd, L"Set Mute Error");
					else
					{
						Mute = mute.fValue;
						if (Mute)
							MuteCheckBox.set_state(STATE_CHECKED,0);
						else
							MuteCheckBox.set_state(0,STATE_CHECKED);
					}
				  }
				  else
				  {
					  return false;
					//CError::ErrMsg(HWnd, L"Get Line Info Error");
				  }
				  mixerClose(hMixer);
				}
			}
			return true;
		}

		// Set microphone volume mute
		bool CMicrophone::SetMute()
		{
			MIXERLINECONTROLS mxlc;
			tMIXERCONTROLDETAILS mxcd;
			tMIXERCONTROLDETAILS_BOOLEAN mute;
			MIXERCONTROL mxc;
			MIXERLINE mxl;
			int intRet;
			int nMixerDevs;

			// Check if Mixer is available
			nMixerDevs = mixerGetNumDevs();
			if (nMixerDevs < 1)
				return false;

			MMRESULT rc = mixerOpen(&hMixer,0,0,NULL,MIXER_OBJECTF_MIXER);
			if (MMSYSERR_NOERROR != rc) 
			{
				CError::ErrMsg(hMainWnd, L"Couldn't open the mixer.");
			}
			else
			{
				mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE;
				mxl.cbStruct = sizeof(mxl);

				// Get line info
				intRet = mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_GETLINEINFOF_COMPONENTTYPE);

				if (intRet == MMSYSERR_NOERROR)
				{
				  ZeroMemory(&mxlc, sizeof(mxlc));
				  mxlc.cbStruct = sizeof(mxlc);
				  mxlc.dwLineID = mxl.dwLineID;
				  mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
				  mxlc.cControls = 1;
				  mxlc.cbmxctrl = sizeof(mxc);

				  mxlc.pamxctrl = &mxc;
				  intRet = mixerGetLineControls((HMIXEROBJ)hMixer, &mxlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);

				  if (intRet == MMSYSERR_NOERROR)
				  {
					ZeroMemory(&mxcd, sizeof(mxcd));
					mxcd.dwControlID = mxc.dwControlID;
					mxcd.cbStruct = sizeof(mxcd);
					mxcd.cMultipleItems = 0;
					mxcd.cbDetails = sizeof(mute);
					mxcd.paDetails = &mute;
					mxcd.cChannels = 1;

					mute.fValue = Mute;

					intRet = mixerSetControlDetails((HMIXEROBJ)hMixer, &mxcd, MIXER_SETCONTROLDETAILSF_VALUE);
					if (intRet != MMSYSERR_NOERROR)
						CError::ErrMsg(HWnd, L"Set Mute Error");
				  }
				  else
				  {
					  return false;
					//CError::ErrMsg(HWnd, L"Get Line Info Error");
				  }
				  mixerClose(hMixer);
				}
			}
			return true;
		}

		// Register customer window class
		ATOM CMicrophone::RegisterWindowClass(HINSTANCE hInstance)
		{
			// Window instance handle initialize
			HInstance = hInstance;

			WNDCLASSEXW wcex;

			// Set extension window options 
			wcex.cbSize			= sizeof(WNDCLASSEX); 
			wcex.style			= CS_VREDRAW | CS_HREDRAW;
			wcex.lpfnWndProc	= (WNDPROC)SoundDlgWindowProc;
			wcex.cbClsExtra		= 0;
			wcex.cbWndExtra		= 0;
			wcex.hInstance		= hInstance;
			wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_WEBINARIA_ICON);
			wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
			wcex.hbrBackground	= (HBRUSH)(COLOR_DESKTOP);
			wcex.lpszMenuName	= 0;
			wcex.lpszClassName	= MCLASSNAME;
			wcex.hIconSm		= LoadIcon(wcex.hInstance, (LPCTSTR)IDI_WEBINARIA_ICON);

			// Register window class
			return RegisterClassExW(&wcex);
		}
		
		// Set window caption
		void CMicrophone::SetWindowCaption( )
		{
			// If text is not null
			if(Caption)
			{
				  // Set window caption
				  ::SetWindowTextW(HWnd,Caption);
				  // If HTMLayout document contains caption
				  if( WindowCaption.is_valid() )
				  {
					// Set HTMLayout document caption text
					WindowCaption.set_text(Caption);
					// Update HTMLayout document caption
					WindowCaption.update(true);
				  }
			}
		}

		// Test HTMLayount controls under mouse pouinter
		int CMicrophone::HitTest(	const int & x,
									const int & y ) const
		{
			POINT pt; 
			pt.x = x; 
			pt.y = y;
			// Convert desktop mouse point to window mouse point
			::MapWindowPoints(HWND_DESKTOP,HWnd,&pt,1);

			// If mouse pointer is over window caption
			if( WindowCaption.is_valid() && WindowCaption.is_inside(pt) )
				return HTCAPTION;

			RECT body_rc = Body.get_location(ROOT_RELATIVE | CONTENT_BOX);

			// If mouse pointer is over Body
			if( PtInRect(&body_rc, pt) )
				return HTCLIENT;

			if( pt.y < body_rc.top + 10 ) 
			{
				// If mouse pointer is over LeftTop coner
				if( pt.x < body_rc.left + 10 ) 
					return HTTOPLEFT;
				// If mouse pointer is over RightTop coner
				if( pt.x > body_rc.right - 10 ) 
					return HTTOPRIGHT;
			}
			else 
			if( pt.y > body_rc.bottom - 10 ) 
			{
				// If mouse pointer is over LeftBottom coner
				if( pt.x < body_rc.left + 10 ) 
					return HTBOTTOMLEFT;
				// If mouse pointer is over RightBottom coner
				if( pt.x > body_rc.right - 10 ) 
					return HTBOTTOMRIGHT;
			}

			// If mouse pointer is above top border
			if( pt.y < body_rc.top ) 
				return HTTOP;
			// If mouse pointer is below bottom border
			if( pt.y > body_rc.bottom ) 
				return HTBOTTOM;
			// If mouse pointer is outer of the left border
			if( pt.x < body_rc.left ) 
				return HTLEFT;
			// If mouse pointer is outer of the right border
			if( pt.x > body_rc.right ) 
				return HTRIGHT;

			// If mouse pointer is in the client area
			return HTCLIENT;	
		}

		// Get root DOM element of the HTMLayout document
		HELEMENT CMicrophone::Root() const
		{
			return htmlayout::dom::element::root_element(HWnd);
		}

		// Обрабатывает события, происходящие с элементами документа HTMLayout
		BOOL CMicrophone::on_event(HELEMENT he, HELEMENT target, BEHAVIOR_EVENTS type, UINT reason )
		{
			// If close button click
			if (target == CloseButton || target == OKButton)
			{
				// Stop recording and tell the driver to unqueue/return all of our WAVEHDRs.
				// The driver will return any partially filled buffer that was currently
				// recording. Because we use waveInReset() instead of waveInStop(),
				// all of the other WAVEHDRs will also be returned via MM_WIM_DONE too
				waveInStop(waveInHandle);
				waveInReset(waveInHandle);                 
				waveInUnprepareHeader (waveInHandle, &waveHeader[0], sizeof(WAVEHDR));
				waveInUnprepareHeader (waveInHandle, &waveHeader[1], sizeof(WAVEHDR));

				waveInStop(waveInHandle);
				waveInClose(waveInHandle);
				VirtualFree(waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE);
				waveInHandle = 0;

				Show = false;
				// Minimize window
				::ShowWindow(HWnd,SW_HIDE);

				return TRUE;
			}

			if (target == VolumeSlider)
			{
				waveInStop(waveInHandle);
				Volume = (float)((int)htmlayout::get_value(VolumeSlider)) * (float)(MaxVolume - MinVolume)/ 100.0f + MinVolume;
				SetVolume();
				waveInReset(waveInHandle);
				waveInStart(waveInHandle);
				return true;
			}

			if (target == MuteCheckBox && type == BUTTON_PRESS)
			{
				Mute = !Mute;
				if (SetMute())
					return SetVolume();
				else
				{
					MuteCheckBox.set_attribute("disabled",L"false");
					return false;
				}
			}

			return TRUE;
		}

		// Handle dialog box message
		LRESULT CALLBACK CMicrophone::SoundDlgWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			LRESULT lResult;
			BOOL    bHandled;
						  
			// HTMLayout +
			// HTMLayout could be created as separate window using CreateWindow API.
			// But in this case we are attaching HTMLayout functionality
			// to the existing window delegating windows message handling to 
			// HTMLayoutProcND function.
			lResult = HTMLayoutProcND(hWnd,message,wParam,lParam, &bHandled);
			if(bHandled)
			  return lResult;
		// HTMLayout -

			CMicrophone* me = self(hWnd);

			switch (message) 
			{
			case MM_MIXM_CONTROL_CHANGE:
			case MM_MIXM_LINE_CHANGE:
				return 0;
			case WM_NCHITTEST:
				{
					// Update HTMLayout window
					htmlayout::dom::element r = htmlayout::dom::element::root_element(hWnd);
					r.update(true);
					// Test hit
					if(me)
						return me->HitTest( GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) );
				}
			
			case WM_ACTIVATE:
					/*Show = (wParam == WA_INACTIVE);*/
				return 0;

			case WM_NCCALCSIZE:   
				// We have no non-client areas
				return 0; 

			case WM_GETMINMAXINFO:
			{
				LRESULT lr = DefWindowProc(hWnd, message, wParam, lParam);
				MINMAXINFO* pmmi = (MINMAXINFO*)lParam;
				pmmi->ptMinTrackSize.x = ::HTMLayoutGetMinWidth(hWnd);
				RECT rc; 
				GetWindowRect(hWnd,&rc);
				pmmi->ptMinTrackSize.y = ::HTMLayoutGetMinHeight(hWnd, rc.right - rc.left);
				return lr;
			}

			case MM_WIM_DATA:
			{
                WAVEHDR* pHeader        = (WAVEHDR*) lParam;
                static int maxLeftSave  = 0;
                static int maxRightSave = 0;

				if (pHeader->dwBytesRecorded > 0)
				{										
                    
                    // OutputDebugMsg ("Recorded %d bytes\n", pHeader->dwBytesRecorded);
                    int          idx       = 0;
                    short int*   pSamples  = (short int*)pHeader->lpData;
                    int          sampleCnt = pHeader->dwBytesRecorded/(sizeof(short int));
                    
                    // ------------------------------------------------------------------

					// ******* Calculate Power Meter *******
					int lm = 0, rm = 0, maxLeft = 0, maxRight = 0;

                    for (idx = 0; idx < sampleCnt; idx += 2)
					{
						if ((lm = abs(pSamples[idx]) >> 6) > maxLeft)
							maxLeft = lm;

						if ((rm = abs(pSamples[idx+1]) >> 6) > maxRight)
							maxRight = rm;
					}

					if (maxLeft < maxLeftSave)
					{
						if ((maxLeft = maxLeftSave - 4) < 0)
							maxLeft = 0;

					}

					if (maxRight < maxRightSave)
					{
						if ((maxRight = maxRightSave - 4) < 0)
							maxRight = 0;
					}

					MeterVolume = (maxLeft + maxRight)/2;

					// Set the sound meter progress bars
					SendMessage(HProgress, PBM_SETPOS, MeterVolume, 0);
				}

				// Yes. Now we need to requeue this buffer so the driver can use it for another block of audio
				// data. NOTE: We shouldn't need to waveInPrepareHeader() a WAVEHDR that has already been prepared once					
				MMRESULT res = waveInAddBuffer(waveInHandle, pHeader, sizeof(WAVEHDR));
			}
			return 0;

			case WM_NCPAINT:     
				{
					// Check the current state of the dialog, and then add the WS_EX_LAYERED attribute 
					SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
					// Sets the window to 0 visibility for green colour for window coners
					SetLayeredWindowAttributes(hWnd,RGB(0,255,0), 0, LWA_COLORKEY); 
				}
				return 0; 

			case WM_CLOSE:
				// Destroy window
				::DestroyWindow(hWnd);
				return 0;

			case WM_DESTROY:
				// Stop recording and tell the driver to unqueue/return all of our WAVEHDRs.
				// The driver will return any partially filled buffer that was currently
				// recording. Because we use waveInReset() instead of waveInStop(),
				// all of the other WAVEHDRs will also be returned via MM_WIM_DONE too
				waveInReset(waveInHandle);                 
				waveInUnprepareHeader (waveInHandle, &waveHeader[0], sizeof(WAVEHDR));
				waveInUnprepareHeader (waveInHandle, &waveHeader[1], sizeof(WAVEHDR));

				waveInClose(waveInHandle);
				VirtualFree(waveHeader[0].lpData, (waveHeader[0].dwBufferLength << 1), MEM_RELEASE);
				waveInHandle = 0;
				// Delete window class
				delete me; 
				me = NULL;
				self(hWnd,0);
				PostQuitMessage(0);
				return 0;
			}
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
}