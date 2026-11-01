#pragma once
#include "stdafx.h"
#include <winsock2.h>
#include <windef.h>   
#include <Nb30.h>
#include <string>

class NetUtils
{
public:
    static std::wstring GetIP(int family = AF_UNSPEC);
    static bool GetMacByIP(LPCWSTR ipAddr, ULONG srcIp, unsigned char* mac);
};

