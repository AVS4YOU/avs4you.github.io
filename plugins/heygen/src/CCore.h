//---------------------------------------------------------------------------

#ifndef CCoreH
#define CCoreH
//---------------------------------------------------------------------------

#include <vector>
#include "CAvatar.h"
#include "CVoice.h"
#include "CUserSettings.h"

enum class Endpoint {
	AvatarsList = 0,
	VoicesList,
	VideoGenerate,
	VideoStatus,
};

class CCore {
public:
	CCore();
	~CCore();

	CCore(CCore const&) = delete;
	void operator=(const CCore &) = delete;

	static CCore& getInstance();

	std::vector<std::pair<std::wstring, std::wstring>> requestHeaders() const;
    std::wstring urlFor(Endpoint) const;

	std::vector<CAvatar *> avatars() const;
	std::vector<CVoice *> voices() const;
	void keepAvatars(const nlohmann::json&);
	void keepVoices(const nlohmann::json&);
    void setApiKey();
    void resetApiKey();
	bool checkApiKey();
	CUserSettings& userSettings();

private:
	class CCorePrivate;
	CCorePrivate * m_intf = nullptr;
};

#endif
