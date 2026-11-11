// PX4Signal.cpp

// BonDriver_PX4 の BER と PER を TVTest のステータスバーへ表示するプラグイン。
// BonDriver の GetSignalLevel() は C/N のまま使い、BER と PER は BonDriver_PX4.dll が
// 公開する PX4GetSignalStats() から取得する。

#include <cstdio>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <windows.h>

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"
#include "px4_signal_stats.h"

namespace {

const int STATUS_ITEM_ID = 1;
// 表示の更新に足りる間隔とし、DriverHost_PX4 経由の I2C 読み出しを増やしすぎないようにする。
const DWORD POLL_INTERVAL_MS = 1000;

enum class Layer : int {
	AUTO = 0,	// ISDB-T は B 階層、ISDB-S は高階層
	A,
	B,
	C,
};

class CPX4Signal final : public TVTest::CTVTestPlugin {
public:
	bool GetPluginInfo(TVTest::PluginInfo *pInfo) override;
	bool Initialize() override;
	bool Finalize() override;

private:
	struct Snapshot {
		bool available = false;	// PX4GetSignalStats() が呼び出せた
		Px4SignalStats stats = {};
	};

	void Start();
	void Stop();
	void Worker();
	void UpdateDriverPath();
	void LoadSettings();
	void SaveSettings();
	std::wstring FormatText(const Snapshot &snapshot) const;
	void Draw(const TVTest::StatusItemDrawInfo *pInfo);

	static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);

	std::wstring ini_path_;
	Layer layer_ = Layer::AUTO;

	std::mutex mtx_;
	std::condition_variable cond_;
	std::thread worker_;
	bool quit_ = false;
	std::wstring driver_path_;	// mtx_ で保護
	Snapshot snapshot_;		// mtx_ で保護
};

bool CPX4Signal::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
	pInfo->Type = TVTest::PLUGIN_TYPE_NORMAL;
	// BonDriver_PX4 以外では取得関数が見つからず何も読み出さないため、既定で有効にする。
	pInfo->Flags = TVTest::PLUGIN_FLAG_ENABLEDEFAULT;
	pInfo->pszPluginName = L"PX4Signal";
	pInfo->pszCopyright = L"px4_drv / GPLv2";
	pInfo->pszDescription = L"BonDriver_PX4 の BER と PER をステータスバーに表示します。";
	return true;
}

bool CPX4Signal::Initialize()
{
	WCHAR path[MAX_PATH];
	DWORD len = ::GetModuleFileNameW(g_hinstDLL, path, MAX_PATH);

	ini_path_.assign(path, len);
	std::wstring::size_type ext = ini_path_.find_last_of(L'.');
	if (ext != std::wstring::npos)
		ini_path_.erase(ext);
	ini_path_ += L".ini";
	LoadSettings();

	TVTest::StatusItemInfo item = {};

	item.Size = sizeof(item);
	// 描画要求の発行は UI スレッドから行う必要があるため、TVTest の更新タイマーで再描画する。
	item.Flags = TVTest::STATUS_ITEM_FLAG_TIMERUPDATE;
	item.Style = 0;
	item.ID = STATUS_ITEM_ID;
	item.pszIDText = L"PX4Signal";
	item.pszName = L"BER/PER (PX4)";
	item.MinWidth = 0;
	item.MaxWidth = -1;
	item.DefaultWidth = TVTest::StatusItemWidthByFontSize(24);
	item.MinHeight = 0;

	if (!m_pApp->RegisterStatusItem(&item))
		m_pApp->AddLog(L"ステータス項目を登録できません。", TVTest::LOG_TYPE_ERROR);

	m_pApp->SetEventCallback(EventCallback, this);

	if (m_pApp->IsPluginEnabled())
		Start();

	return true;
}

bool CPX4Signal::Finalize()
{
	Stop();
	return true;
}

void CPX4Signal::Start()
{
	if (worker_.joinable())
		return;

	UpdateDriverPath();

	{
		std::lock_guard<std::mutex> lock(mtx_);
		quit_ = false;
		snapshot_ = Snapshot();
	}

	worker_ = std::thread(&CPX4Signal::Worker, this);
}

void CPX4Signal::Stop()
{
	if (!worker_.joinable())
		return;

	{
		std::lock_guard<std::mutex> lock(mtx_);
		quit_ = true;
	}
	cond_.notify_all();
	worker_.join();

	std::lock_guard<std::mutex> lock(mtx_);
	snapshot_ = Snapshot();
}

void CPX4Signal::UpdateDriverPath()
{
	WCHAR path[MAX_PATH] = {};

	// TVTest へのメッセージは UI スレッドから送り、取得したパスだけを取得スレッドへ渡す。
	if (m_pApp->GetDriverFullPathName(path, MAX_PATH) <= 0)
		path[0] = L'\0';

	std::lock_guard<std::mutex> lock(mtx_);
	driver_path_ = path;
	snapshot_ = Snapshot();
}

void CPX4Signal::Worker()
{
	std::unique_lock<std::mutex> lock(mtx_);

	while (!quit_) {
		const std::wstring path = driver_path_;
		Snapshot snapshot;

		lock.unlock();

		// PX4GetSignalStats() は名前付きパイプ通信を伴うため、UI スレッドを止めないよう
		// このスレッドで呼び出す。
		HMODULE module = nullptr;

		// ドライバの切り替えで BonDriver が解放されても、呼び出し中は DLL が残るよう参照を加える。
		if (!path.empty() && ::GetModuleHandleExW(0, path.c_str(), &module)) {
			auto func = reinterpret_cast<Px4GetSignalStatsFunc>(::GetProcAddress(module, PX4_SIGNAL_STATS_PROC_NAME));

			if (func) {
				snapshot.stats.size = sizeof(snapshot.stats);
				snapshot.available = (func(&snapshot.stats) != FALSE);
			}

			::FreeLibrary(module);
		}

		lock.lock();

		// 取得中にドライバが切り替わった場合、古いドライバの値を表示しない。
		if (path == driver_path_)
			snapshot_ = snapshot;

		cond_.wait_for(lock, std::chrono::milliseconds(POLL_INTERVAL_MS), [this] { return quit_; });
	}
}

void CPX4Signal::LoadSettings()
{
	int layer = ::GetPrivateProfileIntW(L"Settings", L"Layer", 0, ini_path_.c_str());

	layer_ = (layer >= 0 && layer <= 3) ? static_cast<Layer>(layer) : Layer::AUTO;
}

void CPX4Signal::SaveSettings()
{
	WCHAR value[16];

	swprintf_s(value, L"%d", static_cast<int>(layer_));
	::WritePrivateProfileStringW(L"Settings", L"Layer", value, ini_path_.c_str());
}

std::wstring CPX4Signal::FormatText(const Snapshot &snapshot) const
{
	if (!snapshot.available)
		return L"BER --";

	const Px4SignalStats &stats = snapshot.stats;
	int index;

	switch (layer_) {
	case Layer::A:
		index = 0;
		break;
	case Layer::B:
		index = 1;
		break;
	case Layer::C:
		index = 2;
		break;
	default:
		// 地上デジタルのフルセグは通常 B 階層、BS/CS の主な放送は高階層で送られる。
		index = (stats.system == PX4_SIGNAL_SYSTEM_ISDB_T) ? 1 : 0;
		break;
	}

	WCHAR layer_name[8];

	if (stats.system == PX4_SIGNAL_SYSTEM_ISDB_S)
		wcscpy_s(layer_name, (index == 0) ? L"高" : (index == 1) ? L"低" : L"-");
	else
		swprintf_s(layer_name, L"%c", L'A' + index);

	WCHAR ber[32], per[32];

	if (stats.valid & (PX4_SIGNAL_STATS_VALID_BER_A << index)) {
		if (stats.ber[index] == 0)
			wcscpy_s(ber, L"0");
		else
			swprintf_s(ber, L"%.1E", stats.ber[index] / 1e7);
	} else {
		wcscpy_s(ber, L"--");
	}

	if (stats.valid & (PX4_SIGNAL_STATS_VALID_PER_A << index)) {
		if (stats.per[index] == 0)
			wcscpy_s(per, L"0");
		else
			swprintf_s(per, L"%.1E", stats.per[index] / 1e6);
	} else {
		wcscpy_s(per, L"--");
	}

	WCHAR text[128];

	// ビタビ前 BER は階層の区別がない参考値のため、階層の表示より前に置く。
	if (stats.valid & PX4_SIGNAL_STATS_VALID_VBER) {
		WCHAR vber[32];

		if (stats.vber == 0)
			wcscpy_s(vber, L"0");
		else
			swprintf_s(vber, L"%.1E", stats.vber / 1e7);

		swprintf_s(text, L"VBER %s  %s BER %s PER %s", vber, layer_name, ber, per);
	} else {
		swprintf_s(text, L"%s BER %s PER %s", layer_name, ber, per);
	}
	return text;
}

void CPX4Signal::Draw(const TVTest::StatusItemDrawInfo *pInfo)
{
	std::wstring text;

	if (pInfo->Flags & TVTest::STATUS_ITEM_DRAW_FLAG_PREVIEW) {
		text = L"VBER 4.0E-02  B BER 1.2E-05 PER 0";
	} else {
		Snapshot snapshot;
		{
			std::lock_guard<std::mutex> lock(mtx_);
			snapshot = snapshot_;
		}
		text = FormatText(snapshot);
	}

	m_pApp->ThemeDrawText(pInfo->pszStyle, pInfo->hdc, text.c_str(), pInfo->DrawRect,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, pInfo->Color);
}

LRESULT CALLBACK CPX4Signal::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
	CPX4Signal *pThis = static_cast<CPX4Signal *>(pClientData);

	switch (Event) {
	case TVTest::EVENT_PLUGINENABLE:
		if (lParam1 != 0)
			pThis->Start();
		else
			pThis->Stop();
		pThis->m_pApp->StatusItemNotify(STATUS_ITEM_ID, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
		return TRUE;

	case TVTest::EVENT_DRIVERCHANGE:
		pThis->UpdateDriverPath();
		return TRUE;

	case TVTest::EVENT_STATUSITEM_DRAW:
		pThis->Draw(reinterpret_cast<const TVTest::StatusItemDrawInfo *>(lParam1));
		return TRUE;

	case TVTest::EVENT_STATUSITEM_NOTIFY:
	{
		const auto *pInfo = reinterpret_cast<const TVTest::StatusItemEventInfo *>(lParam1);

		if (pInfo->Event == TVTest::STATUS_ITEM_EVENT_UPDATETIMER) {
			pThis->m_pApp->StatusItemNotify(STATUS_ITEM_ID, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
			return TRUE;
		}

		return FALSE;
	}

	case TVTest::EVENT_STATUSITEM_MOUSE:
	{
		const auto *pInfo = reinterpret_cast<const TVTest::StatusItemMouseEventInfo *>(lParam1);

		// クリックで表示する階層を 自動 → A → B → C の順に切り替える。
		if (pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_LDOWN) {
			pThis->layer_ = static_cast<Layer>((static_cast<int>(pThis->layer_) + 1) % 4);
			pThis->SaveSettings();
			pThis->m_pApp->StatusItemNotify(STATUS_ITEM_ID, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
			return TRUE;
		}

		return FALSE;
	}
	}

	return 0;
}

} // namespace

TVTest::CTVTestPlugin *CreatePluginClass()
{
	return new CPX4Signal;
}
