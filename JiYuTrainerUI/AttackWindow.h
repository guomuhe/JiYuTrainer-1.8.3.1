#pragma once
#include "stdafx.h"
#include "sciter-x.h"
#include "sciter-x-host-callback.h"
#include "CommonWindow.h"
#include "../JiYuTrainer/AppPublic.h"
#include "../JiYuTrainer/JyUdpAttack.h"

extern JTApp* currentApp;

class ScanIPWindow;
class AttackWindow : public sciter::host<AttackWindow>, public CommonWindow
{
public:
	AttackWindow(HWND parentHWnd);
	~AttackWindow();

private:

	bool on_event(HELEMENT he, HELEMENT target, BEHAVIOR_EVENTS type, UINT_PTR reason) override;
	LRESULT onWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL* handled) override;

	ScanIPWindow* currentScanIPWnd = nullptr;
	HWND m_hMainWnd = nullptr;  // ★ 保存主窗口，子窗口直接 owned 于主窗口

	sciter::dom::element result_list;
	sciter::dom::element input_target_ip;
	sciter::dom::element input_target_port;
	sciter::dom::element input_send_message;
	sciter::dom::element input_send_cmd;
	
	bool SendCommonCheck(std::vector<std::wstring>* ip, DWORD* port);
	void SendCmd();
	void SendText();
	void SendShutdown();
	void SendReboott();
	void AddStringToReult(LPCWSTR str);

protected:
	bool onLoadHtml(LPCBYTE pData, DWORD len) override {
		static const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
		BYTE* buf = new BYTE[len + 3];
		memcpy(buf, bom, 3);
		memcpy(buf + 3, pData, len);
		bool r = load_html(buf, len + 3);
		delete[] buf;
		return r;
	}
	sciter::value docunmentComplete() override;

};

