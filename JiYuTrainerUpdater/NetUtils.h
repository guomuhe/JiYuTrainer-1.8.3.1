#pragma once
#include "stdafx.h"
#include <string>
#include <curl/curl.h>

using namespace std;

// NOTE: These functions use C++ types (std::string), so they must NOT be
// declared inside extern "C" (UPEXPORT_CFUNC). Use plain UPEXPORT instead.
UPEXPORT CURLcode curl_get_req(const string &url, string &response);
UPEXPORT CURLcode curl_post_req(const string &url, const string &postParams, string &response);
