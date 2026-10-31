#pragma once
#include "stdafx.h"
#include "sciter-x.h"
#include "sciter-x-host-callback.h"
#include "CommonWindow.h"
#include "../JiYuTrainer/AppPublic.h"

extern JTApp* currentApp;

class HelpWindow : public CommonWindow, public sciter::host<HelpWindow>
{
public:
	HelpWindow(HWND parentHWnd);
	~HelpWindow();

private:
	sciter::dom::element root;
protected:
	sciter::value docunmentComplete();

	bool on_event(HELEMENT he, HELEMENT target, BEHAVIOR_EVENTS type, UINT_PTR reason) override;
	LRESULT onWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL* handled) override;
	bool onLoadHtml(LPCBYTE pData, DWORD len) override {
		static const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
		BYTE* buf = new BYTE[len + 3];
		memcpy(buf, bom, 3);
		memcpy(buf + 3, pData, len);
		bool r = load_html(buf, len + 3);
		delete[] buf;
		return r;
	}
};

