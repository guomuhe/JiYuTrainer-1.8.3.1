// OpChain.h - 统一降级链框架（L0 最优 → L3 最后手段）
// 用法：
//   opchain::OpResult r = opchain::RunChain(L"解除键鼠锁定", {
//       { 0, L"结束锁键鼠进程", [] { return NTDExt::ClearKeyboardAndMouseLock(); } },
//       { 1, L"低级钩子转发",   [] { mythware_ext::StartInputUnlock(); return mythware_ext::IsInputUnlockRunning(); } },
//   });
//   r.ok / r.level / r.detail
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace opchain {

struct OpResult {
	bool ok = false;
	int level = -1;          // 成功时的层级；失败为 -1
	std::wstring detail;     // 可读描述
};

struct Attempt {
	int level = 0;
	const wchar_t* name = L"";
	std::function<bool()> run;
};

inline OpResult RunChain(const std::wstring& opName, const std::vector<Attempt>& chain)
{
	for (size_t i = 0; i < chain.size(); ++i) {
		const Attempt& a = chain[i];
		bool ok = false;
		try {
			ok = a.run ? a.run() : false;
		} catch (...) {
			ok = false;
		}
		if (ok) {
			OpResult r;
			r.ok = true;
			r.level = a.level;
			r.detail = L"L" + std::to_wstring(a.level) + L" " + a.name;
			return r;
		}
	}
	OpResult fail;
	fail.ok = false;
	fail.level = -1;
	fail.detail = opName + L"：全部降级层级均失败";
	return fail;
}

} // namespace opchain
