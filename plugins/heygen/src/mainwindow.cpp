#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "uiapplication.h"
#include "uipopupmessage.h"
#include "uimenu.h"
#include "uipalette.h"
#include "uistyle.h"
#include "uifilesystem.h"
#include "uithread.h"
#include "uitimer.h"
#include "utils.h"
#include "CHttpClient.h"
#include "CCore.h"
#include <shlwapi.h>
#include <nlohmann/json.hpp>
#include "defines.h"
#include "DSAudioPlayer.h"

#define ACTION_GENERATE 100
#define ACTION_CHECK_STATUS 101
#define ACTION_DOWNLOAD 102
#define ACTION_GET_AVATARS 103
#define ACTION_GET_VOICES 104
#define ACTION_FAILED 105

#define TIMER_INTERVAL 3000
#define GEN_TIMER_INTERVAL 1000
#define DNL_TIMER_INTERVAL 100


class MainWindow::CGeneratePrivateIntf {
public:
    std::string  video_id;
    std::wstring video_url,
                 video_path;

    UITimer *m_timer = nullptr,
            *m_gen_timer = nullptr,
            *m_dnl_timer = nullptr;

    CHttpClient   *m_http = nullptr;
    DSAudioPlayer *m_player = nullptr;
    MainWindow    *m_owner = nullptr;

    int timerAction = 0,
        error = 0,
        refresh_status = 0;
    std::chrono::steady_clock::time_point gen_start_time;

    CGeneratePrivateIntf(MainWindow *owner)
    {
        m_owner = owner;
        m_timer = new UITimer();
        m_gen_timer = new UITimer();
        m_dnl_timer = new UITimer();
        m_http = new CHttpClient;
    }

    ~CGeneratePrivateIntf()
    {
        RELEASE_PTR(m_timer);
        RELEASE_PTR(m_gen_timer);
        RELEASE_PTR(m_dnl_timer);
        RELEASE_PTR(m_http);
        RELEASE_PTR(m_player);
    }

    auto http() -> CHttpClient&
    {
        return *(m_http);
    }

    auto timer() -> UITimer&
    {
        return *m_timer;
    }

    auto starttimer(int act) -> void
    {
        timerAction = act;
        m_timer->start(TIMER_INTERVAL);
    }

    auto start_gen_timer() -> void
    {
        gen_start_time = std::chrono::steady_clock::now();
        m_gen_timer->start(GEN_TIMER_INTERVAL);
    }

    auto generation_elapsed_time() -> int
    {
        auto gen_time = std::chrono::steady_clock::now();
        return (std::chrono::duration_cast<std::chrono::milliseconds>(gen_time - gen_start_time)).count();
    }

    auto stop_gen_timer() -> void
    {
        m_gen_timer->stop();
    }

    auto fill_combo_languages() -> void
    {
        m_owner->ui->cmbLanguage->clearItems();
        m_owner->ui->cmbLanguage->addItem(L"Any");
        std::vector<CVoice*> voices = CCore::getInstance().voices();
        for (auto vc: voices) {
            std::wstring vc_lang = vc->language();
            if (!vc_lang.empty() && !m_owner->ui->cmbLanguage->containsItem(vc_lang)) {
                m_owner->ui->cmbLanguage->addItem(vc_lang);
            }
        }
        m_owner->ui->cmbLanguage->setCurrentIndex(0);
    }

    auto fill_avatars_list_by_gender() -> void
    {
        m_owner->ui->listAvatars->clearList();
        std::vector<CAvatar*> avatars = CCore::getInstance().avatars();
        int index = m_owner->ui->cmbGender->currentIndex();
        const wchar_t *sel_gender_str = (index > -1) ? (const wchar_t*)m_owner->ui->cmbGender->itemData(index) : L"any";
        CAvatar::Gender sel_gender = (wcscmp(sel_gender_str, L"male") == 0) ? CAvatar::Gender::Male :
                                         (wcscmp(sel_gender_str, L"female") == 0) ? CAvatar::Gender::Female :
                                         CAvatar::Gender::Unknown;
        for (auto av: avatars) {
            CAvatar::Gender av_gender = av->gender();
            if ( av_gender == sel_gender || sel_gender == CAvatar::Gender::Unknown )
                m_owner->ui->listAvatars->addItem(av->name(), (uintptr_t)av);
        }

        if ( m_owner->ui->listAvatars->count() ) {
            m_owner->ui->listAvatars->setCurrentIndex(0);
            m_dnl_timer->start(DNL_TIMER_INTERVAL);
        }
    }

    auto fill_voices_list_by_gender() -> void
    {
        m_owner->ui->listVoices->clearList();
        std::vector<CVoice*> voices = CCore::getInstance().voices();
        int index = m_owner->ui->cmbGender->currentIndex();
        const wchar_t *sel_gender_str = (index > -1) ? (const wchar_t*)m_owner->ui->cmbGender->itemData(index) : L"any";
        CAvatar::Gender sel_gender = (wcscmp(sel_gender_str, L"male") == 0) ? CAvatar::Gender::Male :
                                         (wcscmp(sel_gender_str, L"female") == 0) ? CAvatar::Gender::Female :
                                         CAvatar::Gender::Unknown;
        // int lang_index = m_owner->ui->cmbLanguage->currentIndex();
        std::wstring sel_lang_str = (index > -1) ? utils::toLowerCase(m_owner->ui->cmbLanguage->text()) : L"any";

        for (auto vc: voices) {
            std::wstring vc_gender_str = utils::toLowerCase(vc->gender());
            CAvatar::Gender vc_gender = (vc_gender_str == L"male") ? CAvatar::Gender::Male :
                                            (vc_gender_str == L"female") ? CAvatar::Gender::Female : CAvatar::Gender::Unknown;
            if ( vc_gender == sel_gender || sel_gender == CAvatar::Gender::Unknown ) {
                std::wstring vc_lang_str = utils::toLowerCase(vc->language());
                if ( vc_lang_str == sel_lang_str || sel_lang_str == L"any" ) {
                    m_owner->ui->listVoices->addItem(vc->name() /*+ L" (" + vc_gender_str + L")"*/, (uintptr_t)vc);
                }
            }
        }

        if ( m_owner->ui->listVoices->count() ) {
            m_owner->ui->listVoices->setCurrentIndex(0);
            m_owner->listVoicesIndexChanged();
        }
    }

    void on_query_avatars_complete(int statuscode, std::wstring answer)
    {
        if ( statuscode == 200 ) {
            nlohmann::json jobj = nlohmann::json::parse(utils::wstr_to_utf8(answer));
            if ( jobj.contains("data") && jobj["data"].contains("avatars") && jobj["data"]["avatars"].is_array() ) {
                nlohmann::json fresh_avatars = jobj["data"]["avatars"];
                if (!fresh_avatars.empty()) {
                    CCore::getInstance().keepAvatars(fresh_avatars);
                    fill_avatars_list_by_gender();
                }
            }
        }
    }

    void on_query_voices_complete(int statuscode, std::wstring answer)
    {
        if ( statuscode == 200 ) {
            nlohmann::json jobj = nlohmann::json::parse(utils::wstr_to_utf8(answer));
            if ( jobj.contains("data") && jobj["data"].contains("voices") && jobj["data"]["voices"].is_array() ) {
                nlohmann::json fresh_voices = jobj["data"]["voices"];
                if (!fresh_voices.empty()) {
                    CCore::getInstance().keepVoices(fresh_voices);
                    fill_combo_languages();
                    fill_voices_list_by_gender();
                }
            }
        }
    }

    auto refresh_avatars() -> void
    {
        m_http->setRequestHeaders(CCore::getInstance().requestHeaders());
        m_http->setRequestJsonData(L"");
        m_http->request(CCore::getInstance().urlFor(Endpoint::AvatarsList).c_str(), ACTION_GET_AVATARS);
    }

    auto refresh_voices() -> void
    {
        m_http->setRequestHeaders(CCore::getInstance().requestHeaders());
        m_http->setRequestJsonData(L"");
        m_http->request(CCore::getInstance().urlFor(Endpoint::VoicesList).c_str(), ACTION_GET_VOICES);
    }

    auto download_image(const std::wstring& url, const std::wstring& id) -> void
    {
        m_http->download(url, L".webp", id);
    }

    auto download_audio(const std::wstring& url, const std::wstring& id) -> void
    {
        LPCWSTR ext = PathFindExtensionW(url.c_str());
        m_http->download(url, ext ? ext : L"", id);
    }
};

MainWindow::MainWindow(const Rect &rc, BYTE flags) :
    UIWindow(nullptr, rc, flags),
    ui(new Ui::MainWindow),
    m_intf(new CGeneratePrivateIntf(this)),
    m_image(nullptr),
    m_scale(1.0),
    m_result(Result::Rejected)
{
    setObjectGroupId(_T("MainWindow"));
    UIWidget *cw = new UIWidget(this);
    cw->setObjectGroupId(_T("CentralWidget"));
    setCentralWidget(cw);
    setContentsMargins(0,0,0,0);
    ui->setupUi(cw);

    /* Top Panel */
    ui->btnClose->clickSignal.connect([=]() {
        close();
    });
    ui->btnClose->activationChangedSignal.connect([this](bool active) {
        UIStyle *style = UIApplication::instance()->style();
        ui->btnClose->SetColor(Palette::Primitive, Palette::Normal, style->themeColor(active ? _T("TEXT_NORMAL") : _T("TEXT_DISABLED")));
        ui->btnClose->update();
        ui->btnRestore->SetColor(Palette::Primitive, Palette::Normal, style->themeColor(active ? _T("TEXT_NORMAL") : _T("TEXT_DISABLED")));
        ui->btnRestore->update();
        ui->btnMinimize->SetColor(Palette::Primitive, Palette::Normal, style->themeColor(active ? _T("TEXT_NORMAL") : _T("TEXT_DISABLED")));
        ui->btnMinimize->update();
    });
    ui->btnRestore->clickSignal.connect([=]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    ui->btnMinimize->clickSignal.connect([=]() {
        showMinimized();
    });
    stateChangedSignal.connect([this](int state) {
        if (state == UIWindow::Normal) {
            ui->btnRestore->setStockIcon(UIButton::RestoreIcon);
        } else
        if (state == UIWindow::Maximized) {
            ui->btnRestore->setStockIcon(UIButton::MaximizeIcon);
        }
    });

    /* Avatars */
    ui->listAvatars->indexChangedSignal.connect([=](int){
        listAvatarsIndexChanged();
    });

    /* Voices */
    ui->listVoices->indexChangedSignal.connect([=](int){
        listVoicesIndexChanged();
    });

    /* Options */
    ui->cmbGender->setItemHeight(26);
    ui->cmbGender->addItem(_T("Any"), (uintptr_t)L"any");
    ui->cmbGender->addItem(_T("Female"), (uintptr_t)L"female");
    ui->cmbGender->addItem(_T("Male"), (uintptr_t)L"male");
    ui->cmbGender->setCurrentIndex(0);
    ui->cmbGender->indexChangedSignal.connect([=](int index) {
        cmbGenderIndexChanged();
    });

    ui->cmbLanguage->setItemHeight(26);
    ui->cmbLanguage->setMaxVisibleItems(10);
    ui->cmbLanguage->addItem(_T("Any"), (uintptr_t)L"any");
    ui->cmbLanguage->setCurrentIndex(0);
    ui->cmbLanguage->indexChangedSignal.connect([=](int index) {
        cmbLanguageIndexChanged();
    });

    connect(ui->btnRefresh->clickSignal, this, &MainWindow::btnRefreshClick);

    /* View Panel*/
    ui->viewPanel->resizeSignal.connect([this](int w, int h) {
        if (m_image) {
            Size sz = m_image->imageSize();
            double drawWidth = 0, drawHeight = 0;
            calculateFitSize(sz, w, h, drawWidth, drawHeight);

            ui->viewPanel->SetMetrics(Metrics::IconWidth, drawWidth);
            ui->viewPanel->SetMetrics(Metrics::IconHeight, drawHeight);
        }
    });

    /* Details */
    connect(ui->btnPlayVoice->clickSignal, this, &MainWindow::btnPlayVoiceClick);

    /* Settings */
    ui->cmbWidth->addItem(_T("3840"));
    ui->cmbWidth->addItem(_T("2560"));
    ui->cmbWidth->addItem(_T("1920"));
    ui->cmbWidth->addItem(_T("1280"));
    ui->cmbWidth->addItem(_T("854"));
    ui->cmbWidth->addItem(_T("640"));
    ui->cmbWidth->addItem(_T("426"));
    ui->cmbWidth->setItemHeight(26);
    ui->cmbWidth->setCurrentIndex(3);

    ui->cmbHeight->addItem(_T("2160"));
    ui->cmbHeight->addItem(_T("1440"));
    ui->cmbHeight->addItem(_T("1080"));
    ui->cmbHeight->addItem(_T("720"));
    ui->cmbHeight->addItem(_T("480"));
    ui->cmbHeight->addItem(_T("360"));
    ui->cmbHeight->addItem(_T("240"));
    ui->cmbHeight->setItemHeight(26);
    ui->cmbHeight->setCurrentIndex(3);

    ui->cmbWidth->indexChangedSignal.connect([this](int index) {
        ui->cmbHeight->setCurrentIndex(index);
    });
    ui->cmbHeight->indexChangedSignal.connect([this](int index) {
        ui->cmbWidth->setCurrentIndex(index);
    });

    connect(ui->linkHeygenKey->clickSignal, this, &MainWindow::linkHeygenKeyClick);
    connect(ui->btnRetrieve->clickSignal, this, &MainWindow::btnRetrieveClick);
    connect(ui->btnApiKey->clickSignal, this, &MainWindow::btnSetApiKeyClick);
    connect(ui->btnGenerate->clickSignal, this, &MainWindow::btnGenerateClick);

    /* Setup */
    m_intf->m_timer->onTimeout([=]() {
        onTimer();
    });
    m_intf->m_gen_timer->onTimeout([=]() {
        onGenerateTimer();
    });
    m_intf->m_dnl_timer->onTimeout([=]() {
        onDownloadImageTimer();
    });
    m_intf->m_http->setOnRequestCompleteCallback(
        std::bind(&MainWindow::onRequestComplete, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );
    m_intf->m_http->setOnDownloadCompleteCallback(
        std::bind(&MainWindow::onDownloadComplete, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
    );

    m_intf->fill_avatars_list_by_gender();
    m_intf->fill_combo_languages();
    m_intf->fill_voices_list_by_gender();
}

MainWindow::~MainWindow()
{
    if (m_image) {
        delete m_image, m_image = nullptr;
    }
    delete m_intf, m_intf = nullptr;
    delete ui, ui = nullptr;
}

tstring MainWindow::filePath() const
{
    return m_intf->video_path;
}

int MainWindow::result() const
{
    return m_result;
}

void MainWindow::calculateFitSize(const Size &imgSize, int w, int h, double &drawW, double &drawH) const noexcept
{
    double rectW = w / m_dpi_ratio;
    double rectH = h / m_dpi_ratio;
    double aspect = (double)imgSize.width / imgSize.height;
    if (rectW / rectH > aspect) {
        drawH = rectH;
        drawW = drawH * aspect;
    } else {
        drawW = rectW;
        drawH = drawW / aspect;
    }
    drawW *= m_scale;
    drawH *= m_scale;
}

void MainWindow::setImage(const tstring &path)
{
    if (m_image) {
        delete m_image, m_image = nullptr;
    }

    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (!utils::loadWebP(path, pixels, w, h)) {
        ui->viewPanel->setPixmap(UIPixmap());
        ui->viewPanel->setText(_T("No preview"));
        ui->bottomPanel->setText(_T(""));
        return;
    }

    m_image = new UIPixmap(UIPixmap::fromRawData(pixels.data(), w, h));
    if (!m_image->isValid()) {
        ui->viewPanel->setPixmap(UIPixmap());
        ui->viewPanel->setText(_T("No preview"));
        ui->bottomPanel->setText(_T(""));
        delete m_image; m_image = nullptr;
        return;
    }

    ui->viewPanel->setText(_T(""));
    Size sz = m_image->imageSize();

    w = 0, h = 0;
    ui->viewPanel->size(&w, &h);
    double drawWidth = 0, drawHeight = 0;
    calculateFitSize(sz, w, h, drawWidth, drawHeight);

    ui->viewPanel->setPixmap(*m_image);
    ui->viewPanel->setIconSize(drawWidth, drawHeight);
}

void MainWindow::listAvatarsIndexChanged()
{
    m_intf->m_dnl_timer->stop();
    m_intf->m_dnl_timer->start(DNL_TIMER_INTERVAL);
}

void MainWindow::listVoicesIndexChanged()
{
    ui->bottomPanel->setText(L"");
    CVoice *vc = nullptr;
    int index = ui->listVoices->currentIndex();
    if (index > -1)
        vc = (CVoice*)ui->listVoices->itemData(index);
    if ( vc ) {
        ui->labelGender->setText(vc->gender());
        ui->labelLang->setText(vc->language());
        ui->labelSupportPause->setText(vc->supportPause() ? L"Yes" : L"No");
        ui->labelSupportEmotions->setText(vc->supportEmotion() ? L"Yes" : L"No");
        ui->labelSupportInteractive->setText(vc->supportInteractiveAvatar() ? L"Yes" : L"No");
    }
}

void MainWindow::cmbGenderIndexChanged()
{
    m_intf->fill_avatars_list_by_gender();
    m_intf->fill_voices_list_by_gender();
}

void MainWindow::cmbLanguageIndexChanged()
{
    m_intf->fill_voices_list_by_gender();
}

void MainWindow::btnRefreshClick()
{
    if ( !CCore::getInstance().checkApiKey() )
        return;
    m_intf->refresh_status = 0;
    ui->btnRefresh->setDisabled(true);
    m_intf->refresh_avatars();
    Sleep(300);
    m_intf->refresh_voices();
}

void MainWindow::btnPlayVoiceClick()
{
    CVoice *vc = nullptr;
    int index = ui->listVoices->currentIndex();
    if (index > -1)
        vc = (CVoice*)ui->listVoices->itemData(index);
    if ( !vc ) {
        ui->viewPanel->setText(L"No audio to preview");
        return;
    }
    std::wstring preview_audio_url = vc->previewAudioUrl();
    std::wstring preview_audio_path = vc->previewAudioPath();

    if ( !preview_audio_path.empty() && UIFileSystem::fileExists(preview_audio_path) ) {
        if (!m_intf->m_player)
            m_intf->m_player = new DSAudioPlayer;
        if (m_intf->m_player->LoadFile(preview_audio_path.c_str())) {
            if (!m_intf->m_player->Play()) {
                ui->viewPanel->setText(L"Cannot play this audio!");
            } else {
                ui->viewPanel->setText(L"");
            }
        } else {
            ui->viewPanel->setText(L"Cannot open this audio!");
        }
    } else {
        /* download audio */
        if ( !preview_audio_url.empty() ) {
            ui->viewPanel->setText(L"Loading...");
            vc->setPreviewAudioPath(L"loading");
            std::wstring id = vc->id();
            m_intf->download_audio(preview_audio_url.c_str(), id.c_str());
        } else {
            ui->viewPanel->setText(L"No audio to preview");
        }
    }
}

void MainWindow::btnRetrieveClick()
{
    m_intf->video_id = CCore::getInstance().userSettings().keyValue("last_video_id");
    if (m_intf->video_id.empty()) {
        showMessage(_T("Heygen"), _T("There are no saved videos."), {UIPopupMessage::Ok});
        return;
    }

    if ( !CCore::getInstance().checkApiKey() )
        return;

    //	nlohmann::json j_obj = R"({"code":null, "data":{"callback_id":null},"message": "asd"})"_json;
    //	bool r = j_obj["code"].is_null();

    //	LOGW(_request_body.c_str());

    ui->btnRetrieve->setDisabled(true);
    std::wstring _url = CCore::getInstance().urlFor(Endpoint::VideoStatus) + L"?video_id=" + utils::utf8_to_wstr(m_intf->video_id);
    m_intf->http().setRequestHeaders(CCore::getInstance().requestHeaders());
    m_intf->http().setRequestJsonData(L"");
    m_intf->http().request(_url.c_str(), ACTION_CHECK_STATUS);
}

void MainWindow::btnSetApiKeyClick()
{
    CCore::getInstance().setApiKey();
}

void MainWindow::btnGenerateClick()
{
    if ( !CCore::getInstance().checkApiKey() )
        return;

    int width = 1280, height = 720;
    try {
        width = std::stoi(ui->cmbWidth->text());
    } catch (...) {}
    try {
        height = std::stoi(ui->cmbHeight->text());
    } catch (...) {}

    nlohmann::json _json_config = {};
    _json_config["dimension"] = {{"width",width},{"height",height}};

    CAvatar *av = nullptr;
    {
        int index = ui->listAvatars->currentIndex();
        if (index > -1)
            av = (CAvatar*)ui->listAvatars->itemData(index);
    }
    if ( !av ) {
        return;
    }

    CVoice *vc = nullptr;
    {
        int index = ui->listVoices->currentIndex();
        if (index > -1)
            vc = (CVoice*)ui->listVoices->itemData(index);
    }
    if ( !vc ) {
        return;
    }

    ui->btnGenerate->setDisabled(true);
    nlohmann::json _character = {{"type", "avatar"}, {"avatar_id", utils::wstr_to_utf8(av->id().c_str())}};
    nlohmann::json _voice = {{"voice_id", utils::wstr_to_utf8(vc->id().c_str())},
                             {"input_text",utils::wstr_to_utf8(ui->textEdit->text())},
                             {"type","text"}};
    nlohmann::json _video_input = {{"character", _character}, {"voice", _voice}};

    _json_config["video_inputs"] = nlohmann::json::array({_video_input});

    std::string _request_body{_json_config.dump().c_str()};
    // LOGW(_request_body.c_str());
    m_intf->http().setRequestHeaders(CCore::getInstance().requestHeaders());
    m_intf->http().setRequestJsonData(_request_body.c_str());
    m_intf->http().request(CCore::getInstance().urlFor(Endpoint::VideoGenerate).c_str(), ACTION_GENERATE);

    m_intf->start_gen_timer();
}

void MainWindow::linkHeygenKeyClick()
{
    std::wstring url = L"https://docs.heygen.com/reference/authentication";
    shellOpenUrl(url);
}

void MainWindow::onRequestComplete(int error, std::wstring str, int act)
{
    UIThread::invoke(this, [=]() {
        LOGW(str.c_str());
        if ( act == ACTION_GET_AVATARS ) {
            m_intf->on_query_avatars_complete(error, str);
            if ( m_intf->refresh_status == 1 ) {
                ui->btnRefresh->setDisabled(false);
            } else
                m_intf->refresh_status = 1;

            if (error != 200) {
                std::wstring err = L"onRequestComplete ACTION_GET_AVATARS: error code " + std::to_wstring(error);
                nlohmann::json json_obj = nlohmann::json::parse(utils::wstr_to_utf8(str), nullptr, false);
                if ( !json_obj["error"].is_null() ) {
                    nlohmann::json json_err =  json_obj["error"];
                    if ( !json_err["message"].is_null() ) {
                        err = utils::utf8_to_wstr(json_err["message"]);
                    }
                }
                if (err == L"Unauthorized")
                    CCore::getInstance().resetApiKey();

                showMessage(_T("Heygen"), err, {UIPopupMessage::Ok});
            }

        } else
        if ( act == ACTION_GET_VOICES ) {
            m_intf->on_query_voices_complete(error, str);
            if ( m_intf->refresh_status == 1 ) {
                ui->btnRefresh->setDisabled(false);
            } else
                m_intf->refresh_status = 1;
        } else
        if ( act == ACTION_GENERATE ) {
            ui->btnGenerate->setDisabled(false);
            if ( error == 200 ) {
                nlohmann::json json_obj = nlohmann::json::parse(utils::wstr_to_utf8(str), nullptr, false);

                if ( json_obj["error"].is_null() && json_obj.contains("data") ) {
                    m_intf->video_id = json_obj["data"]["video_id"].get<std::string>();
                    m_intf->starttimer(ACTION_CHECK_STATUS);

                    CCore::getInstance().userSettings().keepKeyValue("last_video_id", m_intf->video_id);
                } else {
                    LOGW(std::wstring(L"onRequestComplete ACTION_GENERATE: error! " + str).c_str());
                    m_intf->stop_gen_timer();
                    /* request error */

                }
            } else {
                m_intf->stop_gen_timer();
                std::wstring err = L"onRequestComplete ACTION_GENERATE: error code " + std::to_wstring(error);
                nlohmann::json json_obj = nlohmann::json::parse(utils::wstr_to_utf8(str), nullptr, false);
                if ( !json_obj["error"].is_null() ) {
                    nlohmann::json json_err =  json_obj["error"];
                    if ( !json_err["message"].is_null() ) {
                        err = utils::utf8_to_wstr(json_err["message"]);
                    }
                }
                if (err == L"Unauthorized")
                    CCore::getInstance().resetApiKey();

                LOGW(err.c_str());
                showMessage(_T("Heygen"), err, {UIPopupMessage::Ok});
            }
        } else
        if ( act == ACTION_CHECK_STATUS ) {
            ui->btnRetrieve->setDisabled(false);
            ui->btnGenerate->setDisabled(false);
            nlohmann::json json_obj = nlohmann::json::parse(utils::wstr_to_utf8(str), nullptr, false);
            if ( json_obj.contains("data") ) {
                nlohmann::json jdata = json_obj["data"];
                std::string id = jdata["id"].get<std::string>();

                if ( m_intf->video_id == id ) {
                    std::string status = jdata["status"].get<std::string>().c_str();
                    if ( status == "completed" ) {
                        m_intf->stop_gen_timer();
                        m_intf->video_url = utils::utf8_to_wstr(jdata["video_url"].get<std::string>());

                        int result = showMessage(_T("Heygen"), _T("Video generation complete! Download?"),
                                                 {UIPopupMessage::Ok, UIPopupMessage::Cancel});
                        if (result == UIPopupMessage::Ok) {
                            m_intf->starttimer(ACTION_DOWNLOAD);
                        }
                    } else
                    if ( status == "failed" ) {
                        m_intf->video_id = "";
                        // m_intf->starttimer(ACTION_FAILED);
                        m_intf->stop_gen_timer();

                        std::string err_message;
                        if ( jdata.contains("error") && jdata["error"].contains("message") ) {
                            std::string msg = jdata["error"]["message"].get<std::string>();
                            err_message = msg;
                        }
                        std::wstring message = _T("Video generation error: ") + utils::utf8_to_wstr(err_message);
                        showMessage(_T("Heygen"), message, {UIPopupMessage::Ok});
                    } else {
                        // processing/pending status
                        m_intf->starttimer(ACTION_CHECK_STATUS);
                    }
                } else {
                    LOGW(std::wstring(L"onRequestComplete ACTION_CHECK_STATUS: video id error").c_str());
                }
            } else {
                LOGW(L"onRequestComplete ACTION_CHECK_STATUS error");
            }
        }
    });
}

void MainWindow::onDownloadComplete(int error, std::wstring answer, std::wstring id)
{
    UIThread::invoke(this, [=]() {
        LOGW(answer.c_str());

        m_intf->error = error;
        if ( error == 0 ) {
            LPCWSTR _ext = PathFindExtensionW(answer.c_str());
            std::wstring ext = _ext ? _ext : L"";
            if (ext == L".webp") {
                CAvatar *av = nullptr;
                int index = ui->listAvatars->currentIndex();
                if ( index > -1 ) {
                    av = (CAvatar*)ui->listAvatars->itemData(index);
                }
                if ( av && av->id().c_str() == id ) {
                    av->setPreviewImagePath(answer.c_str());
                    ui->viewPanel->setText(L"");
                    setImage(answer);
                } else {
                    int count = ui->listAvatars->count();
                    for (int i = 0; i < count; ++i) {
                        CAvatar *av = (CAvatar*)ui->listAvatars->itemData(i);
                        if (av && av->id().c_str() == id) {
                            av->setPreviewImagePath(answer.c_str());
                            break;
                        }
                    }
                }
            } else
            if (ext == L".mp3" || ext == L".wav") {
                CVoice *vc = nullptr;
                int index = ui->listVoices->currentIndex();
                if ( index > -1 ) {
                    vc = (CVoice*)ui->listVoices->itemData(index);
                }
                if ( vc && vc->id().c_str() == id ) {
                    vc->setPreviewAudioPath(answer.c_str());
                    if (!m_intf->m_player)
                        m_intf->m_player = new DSAudioPlayer;
                    if (m_intf->m_player->LoadFile(answer.c_str())) {
                        if (!m_intf->m_player->Play()) {
                            m_intf->m_owner->ui->viewPanel->setText(L"Cannot play this audio!");
                        } else {
                            m_intf->m_owner->ui->viewPanel->setText(L"");
                        }
                    } else {
                        m_intf->m_owner->ui->viewPanel->setText(L"Cannot open this audio!");
                    }
                } else {
                    int count = ui->listVoices->count();
                    for (int i = 0; i < count; ++i) {
                        CVoice *vc = (CVoice*)ui->listVoices->itemData(i);
                        if (vc && vc->id().c_str() == id) {
                            vc->setPreviewAudioPath(answer.c_str());
                            break;
                        }
                    }
                }
            } else
            if (ext == L".mp4") {
                m_intf->video_path = answer;               
                showMessage(_T("Heygen"), _T("Download complete!"), {UIPopupMessage::Ok});
                m_result = Result::Accepted;
                close();
            }
            LOGW(L"Done!");

        } else {
            std::wstring err = L"Error! " + std::to_wstring(error);
            LOGW(err.c_str());
            showMessage(_T("Heygen"), _T("Downloading error!"), {UIPopupMessage::Ok});
        }

        ui->bottomPanel->setText(L"");
    });
}

void MainWindow::shellOpenUrl(const std::wstring &url, const std::wstring &params) const
{
    ShellExecuteW(NULL, L"open", url.c_str(), params.c_str(), NULL, SW_SHOWNORMAL);
}

int MainWindow::showMessage(const std::wstring &caption, const std::wstring &message, const std::vector<int> &buttons)
{
    UIPopupMessage msg(this, utils::calcWindowPlacement(this, Size(336, 189)));
    msg.setObjectGroupId(_T("PopupMsg"));
    msg.setCaptionText(caption);
    msg.setDescriptionText(message);
    for (int btn : buttons) {
        switch (btn) {
        case UIPopupMessage::Ok:
            msg.addButton(_T("Ok"), UIPopupMessage::Ok);
            break;
        case UIPopupMessage::Cancel:
            msg.addButton(_T("Cancel"), UIPopupMessage::Cancel);
            break;
        default:
            break;
        }
    }
    return msg.runDialog();
}

void MainWindow::onTimer()
{
    m_intf->timer().stop();

    if ( m_intf->timerAction == ACTION_DOWNLOAD ) {
        m_intf->http().download(m_intf->video_url, L".mp4");
    } else {
        std::wstring _url = CCore::getInstance().urlFor(Endpoint::VideoStatus) + L"?video_id=" + utils::utf8_to_wstr(m_intf->video_id);
        m_intf->http().setRequestHeaders(CCore::getInstance().requestHeaders());
        m_intf->http().setRequestJsonData(L"");
        m_intf->http().request(_url.c_str(), ACTION_CHECK_STATUS);
    }
}

void MainWindow::onGenerateTimer()
{
    int total_milliseconds = m_intf->generation_elapsed_time();
    //    int hours = totalMilliseconds / 3600000;
    int minutes = (total_milliseconds % 3600000) / 60000;
    int seconds = (total_milliseconds % 60000) / 1000;
    //	int milliseconds = totalMilliseconds % 1000;

    std::wostringstream ss;
    ss << L"Elapsed time: "
       << std::setw(2) << std::setfill(L'0') << minutes
       << L":"
       << std::setw(2) << std::setfill(L'0') << seconds;
    ui->bottomPanel->setText(ss.str());
}

void MainWindow::onDownloadImageTimer()
{
    m_intf->m_dnl_timer->stop();

    CAvatar* av = nullptr;
    int index = ui->listAvatars->currentIndex();
    if (index > -1)
        av = (CAvatar*)ui->listAvatars->itemData(index);
    if (!av) {
        ui->viewPanel->setText(L"No image to preview");
        return;
    }

    std::wstring preview_url = av->previewImageUrl();
    std::wstring preview_path = av->previewImagePath();
    if ( !preview_path.empty() && UIFileSystem::fileExists(preview_path) ) {
        ui->viewPanel->setText(L"");
        setImage(preview_path);
    } else {
        /* download image */
        setImage(L"");
        if ( !preview_url.empty() ) {
            ui->viewPanel->setText(L"Loading...");
            av->setPreviewImagePath(L"loading");
            std::wstring id = av->id();
            m_intf->download_image(preview_url.c_str(), id.c_str());
        } else {
            LOGW(L"no image to avatar preview");
            ui->viewPanel->setText(L"No image to preview");
        }
    }
}


