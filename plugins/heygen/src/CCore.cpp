//---------------------------------------------------------------------------

#include "CCore.h"
#include <Windows.h>
#include <Shlobj.h>
#include <nlohmann/json.hpp>
#include "CAvatar.h"
#include "CVoice.h"
#include "CUserSettings.h"
#include "apikeydialog.h"
#include "uiapplication.h"
#include "utils.h"
#include "resource.h"
#include "defines.h"
//---------------------------------------------------------------------------

using namespace nlohmann::literals;

class CCore::CCorePrivate
{
public:
    std::wstring m_api_key;
	std::vector<CAvatar *> vec_avatars;
	std::vector<CVoice *> vec_voices;
	std::unique_ptr<CUserSettings> user_settings;

public:
	CCorePrivate()
		: user_settings(std::make_unique<CUserSettings>())
	{
//		m_api_key = "Nzk4YTFkNzNlY2RiNDZlN2FjNTA2NWNmMDAzYjc5NGMtMTc0MzAwNDIwOA==";

        std::wstring config_file_path;
		wchar_t app_data_path[MAX_PATH];
		if ( SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, app_data_path) == S_OK ) {
            config_file_path = std::wstring(app_data_path) + L"/AVS4YOU/heygen/settings.json";
		}

        if ( !config_file_path.empty() ) {
			user_settings->setConfigPath(config_file_path.c_str());
			user_settings->load();
		}

		m_api_key = user_settings->apiKey();

		if ( user_settings->contains("avatars") ) {
			nlohmann::json jarr = user_settings->keyJsonObject("avatars");

			for (auto& ai : jarr) {
				vec_avatars.push_back(new CAvatar(ai));
			}
		}

		std::string jstr;
		nlohmann::json json;
		if (vec_avatars.empty()) {
            jstr = load_text_from_resource(IDT_JSON_SETTINGS, "TEXT");
			if ( !jstr.empty() ) {
				json = nlohmann::json::parse(jstr);

				if ( json.contains("avatars") && json["avatars"].is_array() ) {
					CAvatar * a;
					for (auto& ai : json["avatars"]) {
						a = new CAvatar(ai);
						vec_avatars.push_back(a);
					}
				}
			}
		}

		if ( user_settings->contains("voices") ) {
			nlohmann::json jarr = user_settings->keyJsonObject("voices");

			for (auto& v : jarr) {
				vec_voices.push_back(new CVoice(v));
			}
		}

		if ( vec_voices.empty() ) {
			if (json.empty()) {
				if ( jstr.empty() )
                    jstr = load_text_from_resource(IDT_JSON_SETTINGS, "TEXT");

				if ( !jstr.empty() )
					json = nlohmann::json::parse(jstr);
			}

			if ( !json.empty() ) {
				if ( json.contains("voices") && json["voices"].is_array() ) {
					CVoice * v;
					for (auto& vi : json["voices"]) {
						vec_voices.push_back(new CVoice(vi));
					}
				}
			}
		}
	}

	~CCorePrivate()
	{
		for (auto a: vec_avatars) {
			if (a) {
				delete a, a = nullptr;
			}
		}

		for (auto v: vec_voices) {
			if (v) {
				delete v, v = nullptr;
			}
		}
	}

    auto load_text_from_resource(int id, const std::string& type) -> std::string
	{
        HMODULE HInstance = utils::getCurrentModule();
        HRSRC hres = FindResourceA(HInstance, MAKEINTRESOURCEA(id), type.c_str());
		if ( hres ) {
			HGLOBAL hgRes = LoadResource(HInstance, hres);
			if ( hgRes ) {
				LPVOID pdata = LockResource(hgRes);
				if ( pdata ) {
					DWORD res_size = SizeofResource(HInstance, hres);
					if ( res_size != 0 )
						return std::string(static_cast<const char*>(pdata), res_size);
				}
			}
		}

		return "";
	}

	auto keep_avatars(const nlohmann::json& avts)
	{
		for (auto a: vec_avatars) {
			if (a) {
				delete a, a = nullptr;
			}
		}
		vec_avatars.clear();

		for (auto& ai: avts) {
			vec_avatars.push_back(new CAvatar(ai));
		}

		user_settings->keepKeyValue("avatars", avts);
	}
	
	auto keep_voices(const nlohmann::json& vcs)
	{
		for (auto v: vec_voices) {
			if (v) {
				delete v, v = nullptr;
			}
		}
		vec_voices.clear();

		for (auto& ai: vcs) {
			vec_voices.push_back(new CVoice(ai));
		}

		user_settings->keepKeyValue("voices", vcs);
	}
};

CCore& CCore::getInstance()
{
	static CCore _core;
	return _core;
}

CCore::CCore()
	: m_intf(new CCore::CCorePrivate)
{
}

CCore::~CCore()
{
	if ( m_intf ) {
		m_intf->user_settings->save();

		delete m_intf, m_intf = nullptr;
	}
}

std::vector<std::pair<std::wstring, std::wstring>> CCore::requestHeaders() const
{
	std::vector<std::pair<std::wstring, std::wstring>> _hdrs{{L"Content-Type", L"application/json"},
																{L"accept",L"application/json'"}};
    if ( !m_intf->m_api_key.empty() )
		_hdrs.push_back(std::make_pair(L"x-api-key", m_intf->m_api_key.c_str()));

	return _hdrs;
}

std::wstring CCore::urlFor(Endpoint t) const
{
	switch (t) {
    case Endpoint::AvatarsList: return L"https://api.heygen.com/v2/avatars";
    case Endpoint::VoicesList: return L"https://api.heygen.com/v2/voices";
    case Endpoint::VideoGenerate: return L"https://api.heygen.com/v2/video/generate";
    case Endpoint::VideoStatus: return L"https://api.heygen.com/v1/video_status.get";

    default: return L"";
	}
}

std::vector<CAvatar *> CCore::avatars() const
{
	return m_intf->vec_avatars;
}

std::vector<CVoice *> CCore::voices() const
{
	return m_intf->vec_voices;
}

void CCore::keepAvatars(const nlohmann::json& avatars)
{
	m_intf->keep_avatars(avatars);
}

void CCore::keepVoices(const nlohmann::json& jv)
{
	m_intf->keep_voices(jv);
}

void CCore::setApiKey()
{
    auto windows = UIApplication::instance()->windows();
    UIWidget *parent = windows.empty() ? nullptr : windows[0];

    ApiKeyDialog dlg(parent, utils::calcWindowPlacement(parent, Size(350, 220)));
    if ( !m_intf->m_api_key.empty() )
        dlg.setKey(m_intf->m_api_key);
    if ( dlg.runDialog() == UIDialog::Accepted ) {
        m_intf->m_api_key = dlg.key();
        m_intf->user_settings->keepApiKey(m_intf->m_api_key);
    }
}

void CCore::resetApiKey()
{
    m_intf->m_api_key = L"";
    m_intf->user_settings->keepApiKey(L"");
}

bool CCore::checkApiKey()
{
    if ( m_intf->m_api_key.empty() ) {
        setApiKey();
    }

    return !m_intf->m_api_key.empty();
}

CUserSettings& CCore::userSettings()
{
	return *(m_intf->user_settings);
}

