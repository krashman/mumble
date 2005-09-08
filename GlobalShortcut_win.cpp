/* Copyright (C) 2005, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "GlobalShortcut_win.h"
#include "MainWindow.h"
#include "Global.h"

#undef FAILED
#define FAILED(Status) (static_cast<HRESULT>(Status)<0)

#define DX_SAMPLE_BUFFER_SIZE 512

const GUID GlobalShortcutWin::c_guidApp = { /* ab3baca6-64bc-4d32-9752-49ae9748c06e */
    0xab3baca6,
    0x64bc,
    0x4d32,
    {0x97, 0x52, 0x49, 0xae, 0x97, 0x48, 0xc0, 0x6e}
};

static GlobalShortcutWin *gsw = NULL;

GlobalShortcutWin::GlobalShortcutWin() {
	gsw = this;
	ref = 0;
	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(timeTicked()));

    HRESULT hr;

    ZeroMemory( &diafGame, sizeof(DIACTIONFORMAT) );

    pDI = NULL;
    diaActions = NULL;

    if(FAILED( hr = DirectInput8Create( GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<VOID**>(&pDI), NULL)))
    {
		qFatal("GlobalShortcutWin: Failed to create d8input");
        return;
	}

	qsUsername="MumbleUser";

	remap();

	timer->start(20);
}

void GlobalShortcutWin::add(GlobalShortcut *gs) {
	qmShortcuts[gs->idx] = gs;
	bNeedRemap = true;
}

void GlobalShortcutWin::remove(GlobalShortcut *gs) {
	qmShortcuts.remove(gs->idx);
	bNeedRemap = true;
}

void GlobalShortcutWin::remap() {
	HRESULT hr;

   	bNeedRemap = false;

	unacquire();
	for(int i=0;i<qlDevices.size();i++)
		qlDevices[i]->Release();
	qlDevices.clear();

	if (qmShortcuts.isEmpty())
		return;

	if (diaActions)
		delete [] diaActions;

	diaActions = new DIACTION[qmShortcuts.count()];

	int cnt = 0;
	QMapIterator<int, GlobalShortcut *> i(qmShortcuts);
	while (i.hasNext()) {
		i.next();
		GlobalShortcut *gs = i.value();
		memset(&diaActions[cnt], 0, sizeof(DIACTION));
		diaActions[cnt].uAppData = gs->idx;
		diaActions[cnt].dwSemantic = DIBUTTON_ANY(gs->idx);
		diaActions[cnt].lptszActionName = reinterpret_cast<const wchar_t *>(gs->name.utf16());

		cnt++;
	}

    ZeroMemory( &diafGame, sizeof(DIACTIONFORMAT) );
    diafGame.dwSize          = sizeof(DIACTIONFORMAT);
    diafGame.dwActionSize    = sizeof(DIACTION);
    diafGame.dwDataSize      = qmShortcuts.count() * sizeof(DWORD);
    diafGame.guidActionMap   = c_guidApp;
    diafGame.dwGenre         = DIVIRTUAL_REMOTE_CONTROL;
    diafGame.dwNumActions    = qmShortcuts.count();
    diafGame.rgoAction       = diaActions;
    diafGame.lAxisMin        = -100;
    diafGame.lAxisMax        = 100;
    diafGame.dwBufferSize    = DX_SAMPLE_BUFFER_SIZE;
    wcscpy( diafGame.tszActionMap, reinterpret_cast<const wchar_t *>(QString("Mumble").utf16()) );

	hr=pDI->EnumDevicesBySemantics(reinterpret_cast<const wchar_t *>(qsUsername.utf16()), &diafGame, EnumSuitableDevicesCB, this, 0L);
	if (FAILED(hr))
		qWarning("GlobalShortcutWin: EnumDevicesBySemantics failed 0x%08lx", hr);
}

BOOL GlobalShortcutWin::EnumSuitableDevicesCB(LPCDIDEVICEINSTANCE pdidi, LPDIRECTINPUTDEVICE8 pdidDevice, DWORD, DWORD, LPVOID pContext)
{
	GlobalShortcutWin *cbgsw=static_cast<GlobalShortcutWin *>(pContext);
	HRESULT hr;

	pdidDevice->Unacquire();

	pdidDevice->SetCooperativeLevel( g.mw->winId(), DISCL_NONEXCLUSIVE|DISCL_BACKGROUND );

    hr = pdidDevice->BuildActionMap( &cbgsw->diafGame, reinterpret_cast<const wchar_t *>(cbgsw->qsUsername.utf16()), 0 );
    if( FAILED(hr) ) {
		qWarning("GlobalShortcutWin: Failed BuildActionMap: 0x%08lx", hr);
	}

    // Set the action map for the current device
    hr = pdidDevice->SetActionMap( &cbgsw->diafGame, reinterpret_cast<const wchar_t *>(cbgsw->qsUsername.utf16()), 0 );
    if( FAILED(hr) ) {
		qWarning("GlobalShortcutWin: Failed SetActionMap: 0x%08lx", hr);
	}

	pdidDevice->AddRef();

	cbgsw->qlDevices.append(pdidDevice);

    return DIENUM_CONTINUE;
}

void GlobalShortcutWin::configure()
{
    HRESULT hr;

    remap();

    // Initialize all the colors here
    DICOLORSET dics;
    ZeroMemory(&dics, sizeof(DICOLORSET));
    dics.dwSize = sizeof(DICOLORSET);

    // Fill in all the params
    DICONFIGUREDEVICESPARAMS dicdp;
    ZeroMemory(&dicdp, sizeof(dicdp));
    dicdp.dwSize = sizeof(dicdp);
    dicdp.dwcFormats     = 1;
    dicdp.lprgFormats    = &diafGame;
    dicdp.hwnd           = g.mw->winId();
    dicdp.lpUnkDDSTarget = NULL;
    dicdp.dwcUsers       = 1;
    dicdp.lptszUserNames = const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(qsUsername.utf16()));

	unacquire();

    hr = pDI->ConfigureDevices( NULL, &dicdp, DICD_EDIT, NULL );
    if( FAILED(hr) ) {
		qWarning("GlobalShortcutWin: ConfigureDevices failed 0x%08lx", hr);
		return;
	}

	remap();
}


GlobalShortcutWin::~GlobalShortcutWin() {
	unacquire();
	for(int i=0;i<qlDevices.size();i++)
		qlDevices[i]->Release();
	qlDevices.clear();

	pDI->Release();
}

void GlobalShortcutWin::unacquire() {
	for(int i=0;i<qlDevices.size();i++)
		qlDevices[i]->Unacquire();
}

void GlobalShortcutWin::timeTicked() {
    if (bNeedRemap)
    	remap();

	for(int i=0;i<qlDevices.size();i++) {
        DIDEVICEOBJECTDATA rgdod[DX_SAMPLE_BUFFER_SIZE];
        DWORD   dwItems = DX_SAMPLE_BUFFER_SIZE;
        HRESULT hr;

        qlDevices[i]->Acquire();
        qlDevices[i]->Poll();

        hr = qlDevices[i]->GetDeviceData( sizeof(DIDEVICEOBJECTDATA), rgdod, &dwItems, 0 );
        if( FAILED(hr) )
            continue;

        for( DWORD j=0; j<dwItems; j++ )
        {
            bool  bButtonDown = (rgdod[j].dwData==0x80) ? true : false;
            if (qmShortcuts.contains(rgdod[j].uAppData)) {
				GlobalShortcut *gs = qmShortcuts[rgdod[j].uAppData];
				emit gs->triggered(bButtonDown);
				if (bButtonDown)
					emit gs->down();
				else
					emit gs->up();
			}
		}
	}
}

GlobalShortcut::GlobalShortcut(QObject *p, int index, QString qsName) : QObject(p) {
	if (! gsw)
	  gsw = new GlobalShortcutWin();
	gsw->ref++;
	idx = index;
	name=qsName;
	gsw->add(this);
}

GlobalShortcut::~GlobalShortcut()
{
	gsw->remove(this);
	gsw->ref--;
	if (gsw->ref == 0) {
		delete gsw;
		gsw = 0;
	}
}

void GlobalShortcut::configure()
{
	gsw->configure();
}
