#include "stdafx.h"
#include "NetUtils.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <Mstcpip.h>
// MinGW doesn't have <Ip2string.h> (Windows 10 SDK); <ws2tcpip.h> provides the same
//#include <Ip2string.h>

WCHAR canonname[32];

std::wstring NetUtils::GetIP(int family)
{
	WSADATA wsaData;
	std::wstring strResult;

	if (WSAStartup(MAKEWORD(2, 0), &wsaData) == 0)
	{

		ADDRINFOW* result = nullptr;
		ADDRINFOW* ptr = nullptr;
		ADDRINFOW hints = { 0 };
		hints.ai_flags = AI_CANONNAME;
		hints.ai_family = family;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		if (GetAddrInfoW(L"", NULL, &hints, &result) == 0)
		{
			for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

				switch (ptr->ai_family) {
				case AF_INET:
				case AF_INET6:
					DWORD ipbufferlength = 128;
					WCHAR ipstringbuffer[128];
					if (ptr->ai_canonname != nullptr)
						wcscpy_s(canonname, ptr->ai_canonname);
					if (WSAAddressToStringW(ptr->ai_addr, (DWORD)ptr->ai_addrlen, NULL,
						ipstringbuffer, &ipbufferlength) == 0) {
						strResult.append(ipstringbuffer);
						strResult.append(L"\n");
					}
					break;
				}
			}
		}
		WSACleanup();
	}
	return strResult;
}

bool NetUtils::GetMacByIP(LPCWSTR ipAddr, ULONG srcIp, unsigned char* mac)
{
	ULONG mac_address[2];
	ULONG mac_address_len = 6;

	struct in_addr s;
	InetPton(AF_INET, ipAddr, &s);

	SendARP((IPAddr)s.s_addr, (IPAddr)srcIp, mac_address, &mac_address_len);

	if (mac_address_len)
	{
		BYTE* mac_address_buffer = (BYTE*)&mac_address;
		for (int i = 0; i < (int)mac_address_len; i++)
			mac[i] = (unsigned char)mac_address_buffer[i];
	}
	else
		return false;
	return true;
}

