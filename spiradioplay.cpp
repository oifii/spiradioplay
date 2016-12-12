/*
 * Copyright (c) 2010-2016 Stephane Poirier
 *
 * stephane.poirier@oifii.org
 *
 * Stephane Poirier
 * 3532 rue Ste-Famille, #3
 * Montreal, QC, H2X 2L1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <windows.h>
#include <process.h>
#include <stdio.h>
#include "bass.h"

HWND win=NULL;
CRITICAL_SECTION lock;
DWORD req=0;	// request number/counter
HSTREAM chan;	// stream handle

/*
const char *urls[10]={ // preset stream URLs
	"http://www.radioparadise.com/musiclinks/rp_128-9.m3u", "http://www.radioparadise.com/musiclinks/rp_32.m3u",
	"http://ogg2.as34763.net/vr160.ogg", "http://ogg2.as34763.net/vr32.ogg",
	"http://ogg2.as34763.net/a8160.ogg", "http://ogg2.as34763.net/a832.ogg",
	"http://somafm.com/secretagent.pls", "http://somafm.com/secretagent24.pls",
	"http://somafm.com/suburbsofgoa.pls", "http://somafm.com/suburbsofgoa24.pls"
};
*/
const char *urls[10]={ // preset stream URLs
	"http://50.7.96.138:8666/stream", "http://50.7.96.138:8666/stream", //ambient sleeping pill
	"http://128.179.101.9:9010", "http://128.179.101.9:9010", //uzik radio
	"http://stream2.friskyradio.com:8000/frisky_mp3_hi", "http://stream2.friskyradio.com:8000/frisky_mp3_hi", //frisky radio
	"http://66.165.171.60:8000", "http://66.165.171.60:8000", //dogglounge deep house radio
	"http://88.191.145.93:8010", "http://88.191.145.93:8010" //psychedelik.com
};


char proxy[100]=""; // proxy server

// display error messages
void Error(const char *es)
{
	char mes[200];
	sprintf(mes,"%s\n(error code: %d)",es,BASS_ErrorGetCode());
	MessageBox(win,mes,0,0);
}

#define MESS(id,m,w,l) SendDlgItemMessage(win,id,m,(WPARAM)(w),(LPARAM)(l))

// update stream title from metadata
void DoMeta()
{
	const char *meta=BASS_ChannelGetTags(chan,BASS_TAG_META);
	if (meta) 
	{ 
		// got Shoutcast metadata
		const char *p=strstr(meta,"StreamTitle='"); // locate the title
		if (p) 
		{
			const char *p2=strstr(p,"';"); // locate the end of it
			if (p2) 
			{
				char *t=strdup(p+13);
				t[p2-(p+13)]=0;
				MESS(30,WM_SETTEXT,0,t);
				free(t);
			}
		}
	} 
	else 
	{
		meta=BASS_ChannelGetTags(chan,BASS_TAG_OGG);
		if (meta) 
		{ 
			// got Icecast/OGG tags
			const char *artist=NULL,*title=NULL,*p=meta;
			for (;*p;p+=strlen(p)+1) 
			{
				if (!strnicmp(p,"artist=",7)) // found the artist
					artist=p+7;
				if (!strnicmp(p,"title=",6)) // found the title
					title=p+6;
			}
			if (title) 
			{
				if (artist) 
				{
					char text[100];
					_snprintf(text,sizeof(text),"%s - %s",artist,title);
					MESS(30,WM_SETTEXT,0,text);
				} 
				else
				{
					MESS(30,WM_SETTEXT,0,title);
				}
			}
		}
	}
}

void CALLBACK MetaSync(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	DoMeta();
}

void CALLBACK EndSync(HSYNC handle, DWORD channel, DWORD data, void *user)
{
	MESS(31,WM_SETTEXT,0,"not playing");
	MESS(30,WM_SETTEXT,0,"");
	MESS(32,WM_SETTEXT,0,"");
}

void CALLBACK StatusProc(const void *buffer, DWORD length, void *user)
{
	if (buffer && !length && (DWORD)user==req) // got HTTP/ICY tags, and this is still the current request
		MESS(32,WM_SETTEXT,0,buffer); // display status
}

void __cdecl OpenURL(char *url)
{
	DWORD c,r;
	EnterCriticalSection(&lock); // make sure only 1 thread at a time can do the following
	r=++req; // increment the request counter for this request
	LeaveCriticalSection(&lock);
	KillTimer(win,0); // stop prebuffer monitoring
	BASS_StreamFree(chan); // close old stream
	MESS(31,WM_SETTEXT,0,"connecting...");
	MESS(30,WM_SETTEXT,0,"");
	MESS(32,WM_SETTEXT,0,"");
	c=BASS_StreamCreateURL(url,0,BASS_STREAM_BLOCK|BASS_STREAM_STATUS|BASS_STREAM_AUTOFREE,StatusProc,(void*)r); // open URL
	free(url); // free temp URL buffer
	EnterCriticalSection(&lock);
	if (r!=req) 
	{ 
		// there is a newer request, discard this stream
		LeaveCriticalSection(&lock);
		if (c) BASS_StreamFree(c);
		return;
	}
	chan=c; // this is now the current stream
	LeaveCriticalSection(&lock);
	if (!chan) 
	{ 
		// failed to open
		MESS(31,WM_SETTEXT,0,"not playing");
		Error("Can't play the stream");
	} 
	else
	{
		SetTimer(win,0,50,0); // start prebuffer monitoring
	}
}

BOOL CALLBACK dialogproc(HWND h,UINT m,WPARAM w,LPARAM l)
{
	switch (m) 
	{
		case WM_TIMER:
			{ 
				// monitor prebuffering progress
				DWORD progress=BASS_StreamGetFilePosition(chan,BASS_FILEPOS_BUFFER)
					*100/BASS_StreamGetFilePosition(chan,BASS_FILEPOS_END); // percentage of buffer filled
				if (progress>75 || !BASS_StreamGetFilePosition(chan,BASS_FILEPOS_CONNECTED)) 
				{ 
					// over 75% full (or end of download)
					KillTimer(win,0); // finished prebuffering, stop monitoring
					{ 
						// get the broadcast name and URL
						const char *icy=BASS_ChannelGetTags(chan,BASS_TAG_ICY);
						if (!icy) icy=BASS_ChannelGetTags(chan,BASS_TAG_HTTP); // no ICY tags, try HTTP
						if (icy) 
						{
							for (;*icy;icy+=strlen(icy)+1) 
							{
								if (!strnicmp(icy,"icy-name:",9))
								{
									MESS(31,WM_SETTEXT,0,icy+9);
								}
								if (!strnicmp(icy,"icy-url:",8))
								{
									MESS(32,WM_SETTEXT,0,icy+8);
								}
							}
						} 
						else
						{
							MESS(31,WM_SETTEXT,0,"");
						}
					}
					// get the stream title and set sync for subsequent titles
					DoMeta();
					BASS_ChannelSetSync(chan,BASS_SYNC_META,0,&MetaSync,0); // Shoutcast
					BASS_ChannelSetSync(chan,BASS_SYNC_OGG_CHANGE,0,&MetaSync,0); // Icecast/OGG
					// set sync for end of stream
					BASS_ChannelSetSync(chan,BASS_SYNC_END,0,&EndSync,0);
					// play it!
					BASS_ChannelPlay(chan,FALSE);
				} 
				else 
				{
					char text[20];
					sprintf(text,"buffering... %d%%",progress);
					MESS(31,WM_SETTEXT,0,text);
				}
			}
			break;

		case WM_COMMAND:
			switch (LOWORD(w)) 
			{
				case IDCANCEL:
					DestroyWindow(h);
					return 1;
				case 41:
					if (MESS(41,BM_GETCHECK,0,0))
						BASS_SetConfigPtr(BASS_CONFIG_NET_PROXY,NULL); // disable proxy
					else
						BASS_SetConfigPtr(BASS_CONFIG_NET_PROXY,proxy); // enable proxy
					break;
				default:
					if ((LOWORD(w)>=10 && LOWORD(w)<20) || LOWORD(w)==21) 
					{
						char *url;
						GetDlgItemText(win,40,proxy,sizeof(proxy)-1); // get proxy server
						if (LOWORD(w)==21) 
						{ 
							// custom stream URL
							char temp[200];
							MESS(20,WM_GETTEXT,sizeof(temp),temp);
							url=strdup(temp);
						} 
						else 
						{
							// preset
							url=strdup(urls[LOWORD(w)-10]);
						}
						// open URL in a new thread (so that main thread is free)
						//_beginthread(OpenURL,0,url);
						_beginthread((void(__cdecl*)(void*))OpenURL,0,url);
					}
			}
			break;

		case WM_INITDIALOG:
			win=h;
			// initialize default output device
			if (!BASS_Init(-1,44100,0,win,NULL)) 
			{
				Error("Can't initialize device");
				DestroyWindow(win);
			}
			BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST,1); // enable playlist processing
			BASS_SetConfig(BASS_CONFIG_NET_PREBUF,0); // minimize automatic pre-buffering, so we can do it (and display it) instead
			BASS_SetConfigPtr(BASS_CONFIG_NET_PROXY,proxy); // setup proxy server location
			InitializeCriticalSection(&lock);
			return 1;

		case WM_DESTROY:
			{
				BASS_Free();

				int nShowCmd = false;
				ShellExecuteA(NULL, "open", "end.bat", "", NULL, nShowCmd);
				exit(0);
			}
			break;
	}
	return 0;
}

/*
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		MessageBox(0,"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}

	// display the window
	DialogBox(hInstance,MAKEINTRESOURCE(1000),0,&dialogproc);

	return 0;
}
*/

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		MessageBox(0,"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}
	int nShowCmd = false;
	ShellExecuteA(NULL, "open", "begin.bat", "", NULL, nShowCmd);


	// create the dialog
	HWND hDialog = 0;
	hDialog = CreateDialog(hInstance,MAKEINTRESOURCE(1000),0,&dialogproc);
	// load icon 
	HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(100)); 
	// set icon
	SendMessage (hDialog, WM_SETICON, WPARAM (ICON_SMALL), LPARAM (hIcon));
	// show dialog
	ShowWindow(hDialog, SW_SHOWNORMAL);

    if (!hDialog)
    {
        char buf [100];
        wsprintf (buf, "Error x%x", GetLastError ());
        MessageBox (0, buf, "CreateDialog", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    MSG  msg;
    int status;
    while ((status = GetMessage (& msg, 0, 0, 0)) != 0)
    {
        if (status == -1)
            return -1;
        if (!IsDialogMessage (hDialog, & msg))
        {
            TranslateMessage ( & msg );
            DispatchMessage ( & msg );
        }
    }

    return msg.wParam;

	//return 0;
}
