/*
	BASS "live" spectrum analyser example
	Copyright (c) 2002-2010 Un4seen Developments Ltd.
*/

#include <windows.h>
#include <aygshell.h>
#include <stdio.h>
#include <math.h>
#include "bass.h"

int SPECWIDTH;	// display width
int SPECHEIGHT;	// height

HWND win=NULL;

HRECORD chan;	// recording channel

HDC specdc=0;
HBITMAP specbmp=0;
BYTE *specbuf;

int specmode=0,specpos=0; // spectrum mode (and marker pos for 2nd mode)

// display error messages
void Error(const wchar_t *es)
{
	wchar_t mes[200];
	wsprintf(mes,L"%s\n(error code: %d)",es,BASS_ErrorGetCode());
	MessageBox(win,mes,0,0);
}

// update the spectrum display - the interesting bit :)
void CALLBACK UpdateSpectrum(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	static DWORD quietcount=0;
	HDC dc;
	int x,y,y1;

	if (specmode==3) { // waveform
		short *buf;
		memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
		buf=(short*)alloca(SPECWIDTH*sizeof(short)); // allocate buffer for data
		BASS_ChannelGetData(chan,buf,SPECWIDTH*sizeof(short)); // get the sample data
		for (x=0;x<SPECWIDTH;x++) {
			int v=(32767-buf[x])*SPECHEIGHT/65536; // invert and scale to fit display
			if (!x) y=v;
			do { // draw line from previous sample...
				if (y<v) y++;
				else if (y>v) y--;
				specbuf[y*SPECWIDTH+x]=1+abs(y-SPECHEIGHT/2)*254/SPECHEIGHT;
			} while (y!=v);
		}
	} else {
		int fft[512];
		BASS_ChannelGetData(chan,fft,BASS_DATA_FFT1024); // get the FFT data

		if (!specmode) { // "normal" FFT
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
			for (x=0;x<SPECWIDTH/2;x++) {
#if 1
				y=sqrt(fft[x+1]/(float)(1<<24))*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
#else
				y=((fft[x+1]>>8)*10*SPECHEIGHT)>>16; // scale it (linearly)
#endif
				if (y>SPECHEIGHT) y=SPECHEIGHT; // cap it
				if (x && (y1=(y+y1)/2)) // interpolate from previous to make the display smoother
					while (--y1>=0) specbuf[y1*SPECWIDTH+x*2-1]=1+(y1*127/SPECHEIGHT);
				y1=y;
				while (--y>=0) specbuf[y*SPECWIDTH+x*2]=1+(y*127/SPECHEIGHT); // draw level
			}
		} else if (specmode==1) { // logarithmic, combine bins
			int b0=0;
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);
#define BANDS 28
			for (x=0;x<BANDS;x++) {
				int peak=0;
				int b1=pow(2,x*9.0/(BANDS-1));
				if (b1>511) b1=511;
				if (b1<=b0) b1=b0+1; // make sure it uses at least 1 FFT bin
				for (;b0<b1;b0++)
					if (peak<fft[1+b0]) peak=fft[1+b0];
				y=sqrt(peak/(float)(1<<24))*3*SPECHEIGHT-4; // scale it (sqrt to make low values more visible)
				if (y>SPECHEIGHT) y=SPECHEIGHT; // cap it
				while (--y>=0)
					memset(specbuf+y*SPECWIDTH+x*(SPECWIDTH/BANDS),1+(y*127/SPECHEIGHT),SPECWIDTH/BANDS-2); // draw bar
			}
		} else { // "3D"
			for (x=0;x<SPECHEIGHT;x++) {
				y=sqrt(fft[x+1]/(float)(1<<24))*3*127; // scale it (sqrt to make low values more visible)
				if (y>127) y=127; // cap it
				specbuf[x*SPECWIDTH+specpos]=128+y; // plot it
			}
			// move marker onto next position
			specpos=(specpos+1)%SPECWIDTH;
			for (x=0;x<SPECHEIGHT;x++) specbuf[x*SPECWIDTH+specpos]=255;
		}
	}

	// update the display
	dc=GetDC(win);
	BitBlt(dc,0,0,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
	if (LOWORD(BASS_ChannelGetLevel(chan))<500) { // check if it's quiet
		quietcount++;
		if (quietcount>40 && (quietcount&16)) { // it's been quiet for over a second
			RECT r={0,0,SPECWIDTH,SPECHEIGHT};
			SetTextColor(dc,0xffffff);
			SetBkMode(dc,TRANSPARENT);
			DrawText(dc,L"make some noise!",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
		}
	} else
		quietcount=0; // not quiet
	ReleaseDC(win,dc);
}

// Recording callback - not doing anything with the data
BOOL CALLBACK DuffRecording(HRECORD handle, const void *buffer, DWORD length, void *user)
{
	return TRUE; // continue recording
}

// window procedure
long FAR PASCAL SpectrumWindowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
		case WM_PAINT:
			if (GetUpdateRect(h,0,0)) {
				PAINTSTRUCT p;
				HDC dc;
				if (!(dc=BeginPaint(h,&p))) return 0;
				BitBlt(dc,0,0,SPECWIDTH,SPECHEIGHT,specdc,0,0,SRCCOPY);
				EndPaint(h,&p);
			}
			return 0;

		case WM_LBUTTONUP:
			specmode=(specmode+1)%4; // swap spectrum mode
			memset(specbuf,0,SPECWIDTH*SPECHEIGHT);	// clear display
			return 0;

		case WM_COMMAND:
			switch (LOWORD(w)) {
				case IDCANCEL:
				case IDOK:
					DestroyWindow(h);
					return 1;
			}
			return 0;

		case WM_ACTIVATE:
			if (LOWORD(w)!=WA_INACTIVE) SHFullScreen(h,SHFS_HIDESIPBUTTON);
			return 0;

		case WM_CREATE:
			win=h;
			// initialize BASS recording (default device)
			if (!BASS_RecordInit(-1)) {
				Error(L"Can't initialize device");
				return -1;
			}
			// start recording (44100hz mono 16-bit)
			if (!(chan=BASS_RecordStart(48000,1,0,&DuffRecording,0))) {
				Error(L"Can't start recording");
				return -1;
			}
			{ // go full screen
				SHINITDLGINFO shidi;
				shidi.dwMask = SHIDIM_FLAGS;
				shidi.dwFlags = SHIDIF_DONEBUTTON | SHIDIF_FULLSCREENNOMENUBAR | SHIDIF_SIPDOWN;
				shidi.hDlg = h;
				SHInitDialog(&shidi);
			}
			{ // get display dimensions
				RECT r;
				GetClientRect(h,&r);
				SPECHEIGHT=r.bottom;
				SPECWIDTH=r.right;
				if (SPECWIDTH&3) SPECWIDTH+=4-(SPECWIDTH&3); // bitmap width needs to be a multiple of 4
				{ // create bitmap to draw spectrum in (8 bit for easy updating)
					BYTE data[2000]={0};
					BITMAPINFOHEADER *bh=(BITMAPINFOHEADER*)data;
					RGBQUAD *pal=(RGBQUAD*)(data+sizeof(*bh));
					int a;
					bh->biSize=sizeof(*bh);
					bh->biWidth=SPECWIDTH;
					bh->biHeight=SPECHEIGHT; // upside down (line 0=bottom)
					bh->biPlanes=1;
					bh->biBitCount=8;
					bh->biClrUsed=bh->biClrImportant=256;
					// setup palette
					for (a=1;a<128;a++) {
						pal[a].rgbGreen=256-2*a;
						pal[a].rgbRed=2*a;
					}
					for (a=0;a<32;a++) {
						pal[128+a].rgbBlue=8*a;
						pal[128+32+a].rgbBlue=255;
						pal[128+32+a].rgbRed=8*a;
						pal[128+64+a].rgbRed=255;
						pal[128+64+a].rgbBlue=8*(31-a);
						pal[128+64+a].rgbGreen=8*a;
						pal[128+96+a].rgbRed=255;
						pal[128+96+a].rgbGreen=255;
						pal[128+96+a].rgbBlue=8*a;
					}
					// create the bitmap
					specbmp=CreateDIBSection(0,(BITMAPINFO*)bh,DIB_RGB_COLORS,(void**)&specbuf,NULL,0);
					specdc=CreateCompatibleDC(0);
					SelectObject(specdc,specbmp);
				}
			}
			// setup update timer (20hz)
			SetTimer(h,1,50,UpdateSpectrum);
			break;

		case WM_DESTROY:
			BASS_RecordFree();
			if (specdc) DeleteDC(specdc);
			if (specbmp) DeleteObject(specbmp);
			PostQuitMessage(0);
			break;
	}
	return DefWindowProc(h, m, w, l);
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASS wc={0};
    MSG msg;

	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		MessageBox(0,L"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}

	// register window class and create the window
	wc.lpfnWndProc = SpectrumWindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = L"BASS-Spectrum";
	if (!RegisterClass(&wc) || !CreateWindow(L"BASS-Spectrum",
			L"BASS \"live\" spectrum (click to toggle mode)",
			WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL, hInstance, NULL)) {
		Error(L"Can't create window");
		return 0;
	}
	ShowWindow(win, SW_SHOWNORMAL);

	while (GetMessage(&msg,NULL,0,0)>0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
