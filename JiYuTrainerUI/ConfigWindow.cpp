#include "stdafx.h"
#include "ConfigWindow.h"
#include "../JiYuTrainer/AppPublic.h"
#include "../JiYuTrainer/StringHlp.h"
#include "resource.h"
#include <CommCtrl.h>
#include <windowsx.h>

extern JTApp* currentApp;
extern Logger * currentLogger;

SettingHlp *currentSettings;
HWND hTab = NULL;
HWND hTabMore = NULL;
HWND hTabDebug = NULL;

int g_nScrollPos = 0;   // 当前垂直滚动位置
int g_nMaxScroll = 0;   // 最大可滚动范围

// 滚动条步长
#define SCROLL_STEP 16

// 设置滚动范围
void InitScrollBar(HWND hDlg)
{
    RECT rcClient;
    GetClientRect(hDlg, &rcClient);

    // 遍历所有子控件，计算实际内容总高度
    int nContentHeight = rcClient.bottom;
    HWND hChild = GetWindow(hDlg, GW_CHILD);
    while (hChild)
    {
        RECT rc;
        GetWindowRect(hChild, &rc);
        MapWindowPoints(NULL, hDlg, (LPPOINT)&rc, 2);
        if (rc.bottom > nContentHeight)
            nContentHeight = rc.bottom;
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }

    // 额外留一点余地
    nContentHeight += 10;

    // 计算可滚动的最大范围
    g_nMaxScroll = nContentHeight - (rcClient.bottom - rcClient.top);
    if (g_nMaxScroll < 0) g_nMaxScroll = 0;

    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = g_nMaxScroll;
    si.nPage = (rcClient.bottom - rcClient.top) / 2;
    SetScrollInfo(hDlg, SB_VERT, &si, TRUE);
}

VOID ShowMoreSettings(HWND hWndMain)
{
	currentLogger = currentApp->GetLogger();
	currentSettings = currentApp->GetSettings();
	DialogBox(currentApp->GetInstance() , MAKEINTRESOURCE(IDD_SETTINGS), GetDesktopWindow(), SettingsDlgFunc);
}

INT_PTR CALLBACK SettingsDlgFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG: {	
		g_nScrollPos = 0;
		InitTab(hDlg);
		InitSettings(hDlg);
		InitScrollBar(hDlg);
		return TRUE;
	}
	case WM_COMMAND: {
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		if (LOWORD(wParam) == IDC_SAVE) {
			SaveSettings(hDlg);
		}
		return TRUE;
	}
	case WM_VSCROLL: {
		int nScrollCode = (int)LOWORD(wParam);
		int nPos = (int)HIWORD(wParam);
		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(hDlg, SB_VERT, &si);

		switch (nScrollCode)
		{
		case SB_LINEUP:
			si.nPos -= SCROLL_STEP;
			break;
		case SB_LINEDOWN:
			si.nPos += SCROLL_STEP;
			break;
		case SB_PAGEUP:
			si.nPos -= si.nPage;
			break;
		case SB_PAGEDOWN:
			si.nPos += si.nPage;
			break;
		case SB_THUMBTRACK:
			si.nPos = nPos;
			break;
		case SB_TOP:
			si.nPos = 0;
			break;
		case SB_BOTTOM:
			si.nPos = si.nMax;
			break;
		}

		// 范围限制
		if (si.nPos < si.nMin) si.nPos = si.nMin;
		if (si.nPos > si.nMax) si.nPos = si.nMax;

		int nDelta = si.nPos - g_nScrollPos;
		if (nDelta != 0)
		{
			g_nScrollPos = si.nPos;
			SetScrollInfo(hDlg, SB_VERT, &si, TRUE);
			ScrollWindowEx(hDlg, 0, -nDelta, NULL, NULL, NULL, NULL, SW_ERASE | SW_INVALIDATE);
			UpdateWindow(hDlg);
		}
		return TRUE;
	}
	case WM_MOUSEWHEEL: {
		int nDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(hDlg, SB_VERT, &si);

		int nNewPos = g_nScrollPos - (nDelta / WHEEL_DELTA) * SCROLL_STEP;
		if (nNewPos < si.nMin) nNewPos = si.nMin;
		if (nNewPos > si.nMax) nNewPos = si.nMax;

		int nScroll = nNewPos - g_nScrollPos;
		if (nScroll != 0)
		{
			g_nScrollPos = nNewPos;
			si.nPos = nNewPos;
			SetScrollInfo(hDlg, SB_VERT, &si, TRUE);
			ScrollWindowEx(hDlg, 0, -nScroll, NULL, NULL, NULL, NULL, SW_ERASE | SW_INVALIDATE);
			UpdateWindow(hDlg);
		}
		return TRUE;
	}
	case WM_SIZE: {
		InitScrollBar(hDlg);
		return TRUE;
	}
	case WM_CLOSE: {
		EndDialog(hDlg, wParam);
		return TRUE;
	}
	case WM_NOTIFY: {
		switch (((LPNMHDR)lParam)->code)
		{
		case TCN_SELCHANGING: {
			switch (TabCtrl_GetCurFocus(hTab))
			{
			case 0:
				ShowWindow(hTabMore, SW_HIDE);
				break;
			case 1:
				ShowWindow(hTabDebug, SW_HIDE);
				break;
			}
			break;
		}
		case TCN_SELCHANGE: {
			switch (TabCtrl_GetCurFocus(hTab))
			{
			case 0:
				ShowWindow(hTabMore, SW_SHOW);
				break;
			case 1:
				ShowWindow(hTabDebug, SW_SHOW);
				break;
			}
			break;
		}
		}
		break;
	}
	default:
		break;
	}
	return FALSE;
}
INT_PTR CALLBACK SettingsDlgPageFunc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG: {
		return TRUE;
	}
	case WM_CTLCOLORDLG: {
		return (INT_PTR)(HBRUSH)GetStockObject(WHITE_BRUSH);
	}
	case WM_CTLCOLORSTATIC: {
		HDC hdcStatic = (HDC)wParam;
		SetBkColor(hdcStatic, RGB(255, 255, 255));
		return (INT_PTR)(HBRUSH)GetStockObject(WHITE_BRUSH);
	}
	case WM_CTLCOLORBTN: {
		HDC hdcStatic = (HDC)wParam;
		SetBkColor(hdcStatic, RGB(255, 255, 255));
		return (INT_PTR)(HBRUSH)GetStockObject(WHITE_BRUSH);
	}
	case WM_COMMAND: {
		if (LOWORD(wParam) == IDC_CHECK_INI_11) {
			if (IsDlgButtonChecked(hDlg, IDC_CHECK_INI_11)) {
				EnableWindow(GetDlgItem(hDlg, IDC_CHECK_INI_12), FALSE);
				//CheckDlgButton(hDlg, IDC_CHECK_INI_11, BST_CHECKED);
			}
			else {
				EnableWindow(GetDlgItem(hDlg, IDC_CHECK_INI_12), TRUE);
				//CheckDlgButton(hDlg, IDC_CHECK_INI_11, currentSettings->GetSettingBool(L"SelfProtect", true) ? BST_CHECKED : BST_UNCHECKED);
			}
		}
		if (LOWORD(wParam) == IDC_CHECK_INI_16) {
			if (IsDlgButtonChecked(hDlg, IDC_CHECK_INI_16)) {
				currentApp->GetTrainerWorker()->Start();
			}
			else {
				currentApp->GetTrainerWorker()->Stop();
			}
		}
	}
	default:
		break;
	}
	return FALSE;
}

void SaveSettings(HWND hDlg) {
	if (IsDlgButtonChecked(hTabDebug, IDC_KILLPROC_TP))
		currentSettings->SetSettingStr(L"KillProcess", L"TerminateProcess");
	else if (IsDlgButtonChecked(hTabDebug, IDC_KILLPROC_NTP))
		currentSettings->SetSettingStr(L"KillProcess", L"NtTerminateProcess");
	else if (IsDlgButtonChecked(hTabDebug, IDC_KILLPROC_PPTP_APC))
		currentSettings->SetSettingStr(L"KillProcess", L"KernelMode");

	if (!SysHlp::Is64BitOS())
	{
		currentSettings->SetSettingBool(L"DisableDriver", IsDlgButtonChecked(hTabMore, IDC_CHECK_INI_11));
		currentSettings->SetSettingBool(L"SelfProtect", IsDlgButtonChecked(hTabMore, IDC_CHECK_INI_12));
	}

	currentSettings->SetSettingBool(L"AutoIncludeFullWindow", IsDlgButtonChecked(hTabMore, IDC_CHECK_INI_14));
	currentSettings->SetSettingBool(L"AutoForceKill", IsDlgButtonChecked(hTabMore, IDC_CHECK_INI_13));
	currentSettings->SetSettingBool(L"DoNotShowVirusWindow", IsDlgButtonChecked(hTabMore, IDC_CHECK_INI_15));
	currentSettings->SetSettingBool(L"DoNotShowTrayIcon", IsDlgButtonChecked(hTabDebug, IDC_CHECK_INI_17));

	currentSettings->SetSettingBool(L"AlwaysCheckUpdate", IsDlgButtonChecked(hTabDebug, IDC_CHECK_INI_21));
	currentSettings->SetSettingBool(L"ForceInstallInCurrentDir", IsDlgButtonChecked(hTabDebug, IDC_CHECK_INI_24));
	currentSettings->SetSettingBool(L"ForceDisableWatchDog", IsDlgButtonChecked(hTabDebug, IDC_CHECK_INI_25));
	currentSettings->SetSettingBool(L"InjectMasterHelper", IsDlgButtonChecked(hTabDebug, IDC_CHECK_INI_22));
	currentSettings->SetSettingBool(L"InjectProcHelper64", IsDlgButtonChecked(hTabDebug, IDC_CHECK_INI_23));

	currentSettings->SetSettingInt(L"HotKeyFakeFull", SendDlgItemMessage(hTabMore, IDC_HOTKEY_FK, HKM_GETHOTKEY, NULL, NULL));
	currentSettings->SetSettingInt(L"HotKeyShowHide", SendDlgItemMessage(hTabMore, IDC_HOTKEY_SHOWHIDE, HKM_GETHOTKEY, NULL, NULL));

	WCHAR ckIntervalStrBuffer[16];
	GetDlgItemText(hTabDebug, IDC_CKINTERVAL, ckIntervalStrBuffer, 16);
	int ckInterval = _wtoi(ckIntervalStrBuffer);
	if (ckInterval < 1000 || ckInterval>10000) wcscpy_s(ckIntervalStrBuffer, L"3100");
	currentSettings->SetSettingStr(L"CKInterval", ckIntervalStrBuffer);

	int sel = SendMessage(GetDlgItem(hTabDebug, IDC_COMBO_INJECT_MODE), CB_GETCURSEL, 0, 0);
	if (sel == 0)currentSettings->SetSettingStr(L"InjectMode", L"RemoteThread");
	else if (sel == 1)currentSettings->SetSettingStr(L"InjectMode", L"HookDllStub");

	auto worker = currentApp->GetTrainerWorker();
	if(worker) 
		worker->InitSettings();
	else 
		MessageBox(NULL, L"设置保存成功，请重启软件", L"提示", MB_OK);
	EndDialog(hDlg, IDOK);
}
void InitSettings(HWND hDlg) {
	//IDC_COMBO1
	HWND cbInjectMode = GetDlgItem(hTabDebug, IDC_COMBO_INJECT_MODE);
	SendMessage(cbInjectMode, CB_ADDSTRING, NULL, (WPARAM)L"RemoteThread");
	SendMessage(cbInjectMode, CB_ADDSTRING, NULL, (WPARAM)L"HookDllStub");
	SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(currentApp->GetInstance(), MAKEINTRESOURCE(IDI_APP)));
	SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(currentApp->GetInstance(), MAKEINTRESOURCE(IDI_APP)));

	if (SysHlp::Is64BitOS())
	{
		EnableWindow(GetDlgItem(hTabMore, IDC_CHECK_INI_11), FALSE);
		EnableWindow(GetDlgItem(hTabMore, IDC_CHECK_INI_12), FALSE);
		CheckDlgButton(hTabMore, IDC_CHECK_INI_11, BST_CHECKED);
	}
	else
	{
		CheckDlgButton(hTabMore, IDC_CHECK_INI_11, currentSettings->GetSettingBool(L"DisableDriver", false) ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hTabMore, IDC_CHECK_INI_12, currentSettings->GetSettingBool(L"SelfProtect", true) ? BST_CHECKED : BST_UNCHECKED);
	}

	CheckDlgButton(hTabMore, IDC_CHECK_INI_13, currentSettings->GetSettingBool(L"BandAllRunOp", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabMore, IDC_CHECK_INI_14, currentSettings->GetSettingBool(L"AutoIncludeFullWindow", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabMore, IDC_CHECK_INI_15, currentSettings->GetSettingBool(L"DoNotShowVirusWindow", true) ? BST_CHECKED : BST_UNCHECKED);
	
	if(currentApp->GetTrainerWorker())
		CheckDlgButton(hTabMore, IDC_CHECK_INI_16, currentApp->GetTrainerWorker()->Running() ? BST_CHECKED : BST_UNCHECKED);
	else {
		CheckDlgButton(hTabMore, IDC_CHECK_INI_16, BST_CHECKED);
		EnableWindow(GetDlgItem(hTabMore, IDC_CHECK_INI_16), FALSE);
	}

	CheckDlgButton(hTabDebug, IDC_CHECK_INI_17, currentSettings->GetSettingBool(L"DoNotShowTrayIcon", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabDebug, IDC_CHECK_INI_21, currentSettings->GetSettingBool(L"AlwaysCheckUpdate", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabDebug, IDC_CHECK_INI_24, currentSettings->GetSettingBool(L"ForceInstallInCurrentDir", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabDebug, IDC_CHECK_INI_25, currentSettings->GetSettingBool(L"ForceDisableWatchDog", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabDebug, IDC_CHECK_INI_22, currentSettings->GetSettingBool(L"InjectMasterHelper", false) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hTabDebug, IDC_CHECK_INI_23, currentSettings->GetSettingBool(L"InjectProcHelper64", false) ? BST_CHECKED : BST_UNCHECKED);

	int HotKeyFakeFull = currentSettings->GetSettingInt(L"HotKeyFakeFull", 1606);
	int HotKeyShowHide = currentSettings->GetSettingInt(L"HotKeyShowHide", 1604);

	SendDlgItemMessage(hTabMore, IDC_HOTKEY_FK, HKM_SETHOTKEY, HotKeyFakeFull, NULL);
	SendDlgItemMessage(hTabMore, IDC_HOTKEY_SHOWHIDE, HKM_SETHOTKEY, HotKeyShowHide, NULL);

	std::wstring *setKillProcess = currentSettings->GetSettingStrPtr(L"KillProcess", L"NtTerminateProcess");
	if (StrEqual(setKillProcess->c_str(), L"TerminateProcess"))  Button_SetCheck(GetDlgItem(hTabDebug, IDC_KILLPROC_TP), BST_CHECKED);
	else if (StrEqual(setKillProcess->c_str(), L"NtTerminateProcess")) Button_SetCheck(GetDlgItem(hTabDebug, IDC_KILLPROC_NTP), BST_CHECKED);
	else if (StrEqual(setKillProcess->c_str(), L"KernelMode"))  Button_SetCheck(GetDlgItem(hTabDebug, IDC_KILLPROC_PPTP_APC), BST_CHECKED);
	FreeStringPtr(setKillProcess);

	std::wstring *setCKInterval = currentSettings->GetSettingStrPtr(L"CKInterval", L"3100");
	int ckInterval = _wtoi((*setCKInterval).c_str());
	if (ckInterval < 1000 || ckInterval>10000) ckInterval = 3100;

	WCHAR ckIntervalStrBuffer[16];
	swprintf_s(ckIntervalStrBuffer, L"%d", ckInterval);
	SetDlgItemText(hTabDebug, IDC_CKINTERVAL, ckIntervalStrBuffer);
	FreeStringPtr(setCKInterval);

	std::wstring *setInjectMode = currentSettings->GetSettingStrPtr(L"InjectMode", L"RemoteThread");
	SendMessage(cbInjectMode, CB_SELECTSTRING, -1, (LPARAM)(*setInjectMode).c_str());
	FreeStringPtr(setInjectMode);
}
void InitTab(HWND hDlg) {

	hTab = GetDlgItem(hDlg, IDC_TAB_SETTINGS);
	hTabMore = CreateDialog(currentApp->GetInstance(), MAKEINTRESOURCE(IDD_SETTINGS_MORE), hTab, SettingsDlgPageFunc);
	hTabDebug = CreateDialog(currentApp->GetInstance(), MAKEINTRESOURCE(IDD_SETTINGS_DEBUG), hTab, SettingsDlgPageFunc);

	RECT rc;
	GetClientRect(hTab, &rc);

	MoveWindow(hTabMore, rc.left + 1, rc.top + 23, rc.right - rc.left - 6, rc.bottom - rc.top - 2 - 23, FALSE);
	MoveWindow(hTabDebug, rc.left + 1, rc.top + 23, rc.right - rc.left - 6, rc.bottom - rc.top - 2 - 23, FALSE);
	ShowWindow(hTabMore, SW_SHOW);

	TC_ITEM tie;
	tie.mask = TCIF_TEXT;
	tie.pszText = (LPWSTR)L"高级配置";
	TabCtrl_InsertItem(hTab, 0, &tie);
	tie.pszText = (LPWSTR)L"调试配置";
	TabCtrl_InsertItem(hTab, 1, &tie);
}

