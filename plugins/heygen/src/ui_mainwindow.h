#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include "uicaption.h"
#include "uibutton.h"
#include "uilabel.h"
#include "uilistview.h"
#include "uitextedit.h"
#include "uicombobox.h"
#include "uiboxlayout.h"
#include "uispacer.h"
#include "uimetrics.h"
#include "isltranslator.h"
#include "resource.h"

#define SetMetrics(role, value) metrics()->setMetrics(role, value)
#define SetColor(role, state, value) palette()->setColor(role, state, value)

class Ui_MainWindow
{
public:
    UIButton *btnMinimize;
    UIButton *btnRestore;
    UIButton *btnClose;
    UIListView *listAvatars;
    UIListView *listVoices;
    UIComboBox *cmbGender;
    UIComboBox *cmbLanguage;
    UIButton *btnRefresh;
    UIButton *btnApiKey;
    UITextEdit *textEdit;
    UILabel *viewPanel;
    UILabel *bottomPanel;
    UILabel *labelGender;
    UILabel *labelLang;
    UILabel *labelSupportPause;
    UILabel *labelSupportEmotions;
    UILabel *labelSupportInteractive;
    UIButton *btnPlayVoice;
    UIComboBox *cmbWidth;
    UIComboBox *cmbHeight;
    UIButton *linkHeygenKey;
    UIButton *btnRetrieve;
    UIButton *btnGenerate;

    void setupUi(UIWidget *w)
    {
        UIBoxLayout *cenVlut = new UIBoxLayout(UIBoxLayout::Vertical);
        cenVlut->setContentMargins(0, 0, 0, 0);
        cenVlut->setSpacing(0);
        w->setLayout(cenVlut);

        /* Top Panel */
        caption = new UICaption(w);
        caption->setObjectGroupId(_T("HeaderBar"));
        caption->setIcon(IDI_MAINICON, 20, 20);
        cenVlut->addWidget(caption);

        UIBoxLayout *topHlut = new UIBoxLayout(UIBoxLayout::Horizontal);
        topHlut->setContentMargins(0, 0, 0, 0);
        topHlut->setSpacing(0);
        caption->setLayout(topHlut);

        UISpacer *captionStretch = new UISpacer(5,5, SizePolicy::Expanding, SizePolicy::Fixed);
        topHlut->addSpacer(captionStretch);

        btnMinimize = new UIButton(caption);
        btnMinimize->setObjectGroupId(_T("HeaderButton"));
        btnMinimize->setStockIcon(UIButton::MinimizeIcon);
        btnMinimize->setIconSize(10, 10);
        topHlut->addWidget(btnMinimize);

        btnRestore = new UIButton(caption);
        btnRestore->setObjectGroupId(_T("HeaderButton"));
        btnRestore->setStockIcon(UIButton::RestoreIcon);
        btnRestore->setIconSize(10, 10);
        btnRestore->setSupportSnapLayouts();
        topHlut->addWidget(btnRestore);

        btnClose = new UIButton(caption);
        btnClose->setObjectGroupId(_T("HeaderCloseButton"));
        btnClose->setStockIcon(UIButton::CloseIcon);
        btnClose->setIconSize(10, 10);
        topHlut->addWidget(btnClose);

        /* App Panel*/
        UIWidget *appPanel = new UIWidget(w);
        appPanel->setObjectGroupId(_T("CentralWidget"));
        appPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        appPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        cenVlut->addWidget(appPanel);

        UIBoxLayout *appHlut = new UIBoxLayout(UIBoxLayout::Horizontal, UIBoxLayout::AlignHLeft | UIBoxLayout::AlignVCenter);
        appHlut->setContentMargins(0, 1, 0, 1);
        appHlut->setSpacing(1);
        appPanel->setLayout(appHlut);

        /* Left Panel*/
        UIWidget *leftPanel = new UIWidget(appPanel);
        leftPanel->setObjectGroupId(_T("PanelWidget"));
        leftPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Fixed);
        leftPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        leftPanel->setBaseSize(220, 50);
        appHlut->addWidget(leftPanel);

        UIBoxLayout *leftVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignHCenter | UIBoxLayout::AlignVCenter);
        leftVlut->setContentMargins(3, 3, 3, 3);
        leftVlut->setSpacing(6);
        leftPanel->setLayout(leftVlut);

        /* Avatars */
        labelAvatars = new UILabel(leftPanel);
        labelAvatars->setObjectGroupId(_T("PanelLabel"));
        labelAvatars->setVectorIcon(IDI_AVATARDARK, 14, 14);
        leftVlut->addWidget(labelAvatars);

        UIWidget *avatarsListContainer = new UIWidget(leftPanel);
        avatarsListContainer->setObjectGroupId(_T("ListView"));
        avatarsListContainer->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        leftVlut->addWidget(avatarsListContainer);

        UIBoxLayout *avatarsVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignCenter);
        avatarsVlut->setContentMargins(1, 4, 1, 4);
        avatarsVlut->setSpacing(0);
        avatarsListContainer->setLayout(avatarsVlut);

        listAvatars = new UIListView(avatarsListContainer);
        listAvatars->setObjectGroupId(_T("ListViewViewport"));
        listAvatars->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        listAvatars->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        listAvatars->setRowBaseHeight(36);
        avatarsVlut->addWidget(listAvatars);

        /* Voices */
        labelVoices = new UILabel(leftPanel);
        labelVoices->setObjectGroupId(_T("PanelLabel"));
        labelVoices->setVectorIcon(IDI_VOICEDARK, 14, 14);
        leftVlut->addWidget(labelVoices);

        UIWidget *voicesListContainer = new UIWidget(leftPanel);
        voicesListContainer->setObjectGroupId(_T("ListView"));
        voicesListContainer->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        leftVlut->addWidget(voicesListContainer);

        UIBoxLayout *voicesVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignCenter);
        voicesVlut->setContentMargins(1, 4, 1, 4);
        voicesVlut->setSpacing(0);
        voicesListContainer->setLayout(voicesVlut);

        listVoices = new UIListView(voicesListContainer);
        listVoices->setObjectGroupId(_T("ListViewViewport"));
        listVoices->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        listVoices->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        listVoices->setRowBaseHeight(36);
        voicesVlut->addWidget(listVoices);

        /* Options */
        labelOptions = new UILabel(leftPanel);
        labelOptions->setObjectGroupId(_T("PanelLabel"));
        labelOptions->setVectorIcon(IDI_OPTIONSDARK, 14, 14);
        leftVlut->addWidget(labelOptions);

        UIWidget *optPanel = new UIWidget(leftPanel);
        optPanel->setObjectGroupId(_T("PanelWidget"));
        optPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        optPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        leftVlut->addWidget(optPanel);

        UIBoxLayout *optVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignHCenter | UIBoxLayout::AlignVCenter);
        optVlut->setContentMargins(0, 3, 0, 12);
        optVlut->setSpacing(6);
        optPanel->setLayout(optVlut);

        /* Gender */
        labelGenderOpt = new UILabel(optPanel);
        labelGenderOpt->setObjectGroupId(_T("Label"));
        optVlut->addWidget(labelGenderOpt);

        cmbGender = new UIComboBox(optPanel);
        cmbGender->setObjectGroupId(_T("LineEdit"));
        cmbGender->setVectorIcon(/*style->theme() == _T("Light") ?*/ IDI_CHEVRONDARK /*: IDI_CHEVRON*/, 14, 14);
        cmbGender->metrics()->setMetrics(Metrics::IconAlignment, Metrics::AlignHRight | Metrics::AlignVCenter);
        cmbGender->setEditable(false);
        cmbGender->setItemHeight(26);
        optVlut->addWidget(cmbGender);

        /* Language */
        labelLanguageOpt = new UILabel(optPanel);
        labelLanguageOpt->setObjectGroupId(_T("Label"));
        optVlut->addWidget(labelLanguageOpt);

        cmbLanguage = new UIComboBox(optPanel);
        cmbLanguage->setObjectGroupId(_T("LineEdit"));
        cmbLanguage->setVectorIcon(/*style->theme() == _T("Light") ?*/ IDI_CHEVRONDARK /*: IDI_CHEVRON*/, 14, 14);
        cmbLanguage->metrics()->setMetrics(Metrics::IconAlignment, Metrics::AlignHRight | Metrics::AlignVCenter);
        cmbLanguage->setEditable(false);
        cmbLanguage->setItemHeight(26);
        optVlut->addWidget(cmbLanguage);

        UISpacer *spacerOpts = new UISpacer(5, 5, SizePolicy::Fixed, SizePolicy::Expanding);
        optVlut->addSpacer(spacerOpts);

        /* Refresh */
        btnRefresh = new UIButton(optPanel);
        btnRefresh->setObjectGroupId(_T("PushButtonSecondary"));
        btnRefresh->SetMetrics(Metrics::TextAlignment, Metrics::AlignHCenter | Metrics::AlignVCenter);
        optVlut->addWidget(btnRefresh);

        /* Middle Panel*/
        UIWidget *middlePanel = new UIWidget(appPanel);
        middlePanel->setObjectGroupId(_T("PanelWidget"));
        middlePanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        middlePanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        appHlut->addWidget(middlePanel);

        UIBoxLayout *middleVlut = new UIBoxLayout(UIBoxLayout::Vertical);
        middleVlut->setContentMargins(3, 3, 3, 3);
        middleVlut->setSpacing(6);
        middlePanel->setLayout(middleVlut);

        /* Speech */
        labelSpeech = new UILabel(middlePanel);
        labelSpeech->setObjectGroupId(_T("PanelLabel"));
        labelSpeech->setVectorIcon(IDI_SPEECHDARK, 14, 14);
        middleVlut->addWidget(labelSpeech);

        UIWidget *textEditContainer = new UIWidget(middlePanel);
        textEditContainer->setObjectGroupId(_T("ListView"));
        textEditContainer->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        middleVlut->addWidget(textEditContainer);

        UIBoxLayout *textVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignCenter);
        textVlut->setContentMargins(4, 4, 4, 4);
        textVlut->setSpacing(0);
        textEditContainer->setLayout(textVlut);

        textEdit = new UITextEdit(textEditContainer);
        textEdit->setObjectGroupId(_T("ListViewViewport"));
        textEdit->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        textEdit->metrics()->setMetrics(Metrics::TextAlignment, Metrics::AlignHLeft | Metrics::AlignVTop);
        textEdit->metrics()->setMetrics(Metrics::TextMarginLeft, 6);
        textEdit->metrics()->setMetrics(Metrics::TextMarginTop, 4);
        textVlut->addWidget(textEdit);

        /* Preview */
        labelPreview = new UILabel(middlePanel);
        labelPreview->setObjectGroupId(_T("PanelLabel"));
        labelPreview->setVectorIcon(IDI_PREVIEWDARK, 14, 14);
        middleVlut->addWidget(labelPreview);

        viewPanel = new UILabel(middlePanel);
        viewPanel->setObjectGroupId(_T("ViewWidget"));
        viewPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        viewPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        viewPanel->setFont({DEFAULT_FONT_NAME, 14});
        middleVlut->addWidget(viewPanel);

        /* Right Panel*/
        UIWidget *rightPanel = new UIWidget(appPanel);
        rightPanel->setObjectGroupId(_T("PanelWidget"));
        rightPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Fixed);
        rightPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        rightPanel->setBaseSize(220, 50);
        appHlut->addWidget(rightPanel);

        UIBoxLayout *rightVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignHCenter | UIBoxLayout::AlignVCenter);
        leftVlut->setContentMargins(3, 3, 3, 3);
        leftVlut->setSpacing(6);
        rightPanel->setLayout(rightVlut);

        /* Details */
        labelDetails = new UILabel(rightPanel);
        labelDetails->setObjectGroupId(_T("PanelLabel"));
        labelDetails->setVectorIcon(IDI_ABOUTDARK, 14, 14);
        rightVlut->addWidget(labelDetails);

        UIWidget *detailsPanel = new UIWidget(rightPanel);
        detailsPanel->setObjectGroupId(_T("PanelWidget"));
        detailsPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        detailsPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        rightVlut->addWidget(detailsPanel);

        UIBoxLayout *detailsVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignHCenter | UIBoxLayout::AlignVCenter);
        detailsVlut->setContentMargins(0, 3, 0, 12);
        detailsVlut->setSpacing(6);
        detailsPanel->setLayout(detailsVlut);

        /* Gender */
        labelGenderDtls = new UILabel(detailsPanel);
        labelGenderDtls->setObjectGroupId(_T("Label"));
        detailsVlut->addWidget(labelGenderDtls);

        labelGender = new UILabel(detailsPanel);
        labelGender->setObjectGroupId(_T("LineEdit"));
        detailsVlut->addWidget(labelGender);

        /* Language */
        labelLanguageDtls = new UILabel(detailsPanel);
        labelLanguageDtls->setObjectGroupId(_T("Label"));
        detailsVlut->addWidget(labelLanguageDtls);

        labelLang = new UILabel(detailsPanel);
        labelLang->setObjectGroupId(_T("LineEdit"));
        detailsVlut->addWidget(labelLang);

        /* Support pause */
        labelSupportPauseDtls = new UILabel(detailsPanel);
        labelSupportPauseDtls->setObjectGroupId(_T("Label"));
        detailsVlut->addWidget(labelSupportPauseDtls);

        labelSupportPause = new UILabel(detailsPanel);
        labelSupportPause->setObjectGroupId(_T("LineEdit"));
        detailsVlut->addWidget(labelSupportPause);

        /* Support emotion */
        labelSupportEmotDtls = new UILabel(detailsPanel);
        labelSupportEmotDtls->setObjectGroupId(_T("Label"));
        detailsVlut->addWidget(labelSupportEmotDtls);

        labelSupportEmotions = new UILabel(detailsPanel);
        labelSupportEmotions->setObjectGroupId(_T("LineEdit"));
        detailsVlut->addWidget(labelSupportEmotions);

        /* Support interactive avatar */
        labelSupportInteractDtls = new UILabel(detailsPanel);
        labelSupportInteractDtls->setObjectGroupId(_T("Label"));
        detailsVlut->addWidget(labelSupportInteractDtls);

        labelSupportInteractive = new UILabel(detailsPanel);
        labelSupportInteractive->setObjectGroupId(_T("LineEdit"));
        detailsVlut->addWidget(labelSupportInteractive);

        UISpacer *spacerDetails = new UISpacer(5, 5, SizePolicy::Fixed, SizePolicy::Expanding);
        detailsVlut->addSpacer(spacerDetails);

        btnPlayVoice = new UIButton(detailsPanel);
        btnPlayVoice->setObjectGroupId(_T("PushButtonSecondary"));
        btnPlayVoice->SetMetrics(Metrics::TextAlignment, Metrics::AlignHCenter | Metrics::AlignVCenter);
        detailsVlut->addWidget(btnPlayVoice);

        /* Config */
        labelSettings = new UILabel(rightPanel);
        labelSettings->setObjectGroupId(_T("PanelLabel"));
        labelSettings->setVectorIcon(IDI_CONFIGDARK, 14, 14);
        rightVlut->addWidget(labelSettings);

        UIWidget *settingsPanel = new UIWidget(rightPanel);
        settingsPanel->setObjectGroupId(_T("PanelWidget"));
        settingsPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        settingsPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Expanding);
        rightVlut->addWidget(settingsPanel);

        UIBoxLayout *settingsVlut = new UIBoxLayout(UIBoxLayout::Vertical, UIBoxLayout::AlignHCenter | UIBoxLayout::AlignVCenter);
        settingsVlut->setContentMargins(0, 3, 0, 12);
        settingsVlut->setSpacing(6);
        settingsPanel->setLayout(settingsVlut);

        /* Resolution */
        labelWidth = new UILabel(settingsPanel);
        labelWidth->setObjectGroupId(_T("Label"));
        settingsVlut->addWidget(labelWidth);

        cmbWidth = new UIComboBox(settingsPanel);
        cmbWidth->setObjectGroupId(_T("LineEdit"));
        cmbWidth->setVectorIcon(/*style->theme() == _T("Light") ?*/ IDI_CHEVRONDARK /*: IDI_CHEVRON*/, 14, 14);
        cmbWidth->metrics()->setMetrics(Metrics::IconAlignment, Metrics::AlignHRight | Metrics::AlignVCenter);
        cmbWidth->setEditable(false);
        cmbWidth->setItemHeight(26);
        settingsVlut->addWidget(cmbWidth);

        labelHeight = new UILabel(settingsPanel);
        labelHeight->setObjectGroupId(_T("Label"));
        settingsVlut->addWidget(labelHeight);

        cmbHeight = new UIComboBox(settingsPanel);
        cmbHeight->setObjectGroupId(_T("LineEdit"));
        cmbHeight->setVectorIcon(/*style->theme() == _T("Light") ?*/ IDI_CHEVRONDARK /*: IDI_CHEVRON*/, 14, 14);
        cmbHeight->metrics()->setMetrics(Metrics::IconAlignment, Metrics::AlignHRight | Metrics::AlignVCenter);
        cmbHeight->setEditable(false);
        cmbHeight->setItemHeight(26);
        settingsVlut->addWidget(cmbHeight);

        UISpacer *spacerConf = new UISpacer(5, 5, SizePolicy::Fixed, SizePolicy::Expanding);
        settingsVlut->addSpacer(spacerConf);

        linkHeygenKey = new UIButton(settingsPanel);
        linkHeygenKey->setObjectGroupId(_T("HyperLink"));
        FontInfo fi;
        fi.name = DEFAULT_FONT_NAME;
        fi.underline = true;
        linkHeygenKey->setFont(fi);
        linkHeygenKey->setHandCursorOnHover();
        settingsVlut->addWidget(linkHeygenKey);

        UISpacer *spacerConf1 = new UISpacer(5, 4, SizePolicy::Fixed, SizePolicy::Fixed);
        settingsVlut->addSpacer(spacerConf1);

        btnRetrieve = new UIButton(settingsPanel);
        btnRetrieve->setObjectGroupId(_T("PushButtonSecondary"));
        btnRetrieve->SetMetrics(Metrics::TextAlignment, Metrics::AlignHCenter | Metrics::AlignVCenter);
        settingsVlut->addWidget(btnRetrieve);

        UISpacer *spacerConf2 = new UISpacer(5, 4, SizePolicy::Fixed, SizePolicy::Fixed);
        settingsVlut->addSpacer(spacerConf2);

        btnApiKey = new UIButton(settingsPanel);
        btnApiKey->setObjectGroupId(_T("PushButtonSecondary"));
        btnApiKey->SetMetrics(Metrics::TextAlignment, Metrics::AlignHCenter | Metrics::AlignVCenter);
        settingsVlut->addWidget(btnApiKey);

        UISpacer *spacerConf3 = new UISpacer(5, 4, SizePolicy::Fixed, SizePolicy::Fixed);
        settingsVlut->addSpacer(spacerConf3);

        btnGenerate = new UIButton(settingsPanel);
        btnGenerate->setObjectGroupId(_T("PushButton"));
        btnGenerate->SetMetrics(Metrics::TextAlignment, Metrics::AlignHCenter | Metrics::AlignVCenter);
        settingsVlut->addWidget(btnGenerate);

        /* Bottom Panel*/
        bottomPanel = new UILabel(w);
        bottomPanel->setObjectGroupId(_T("ViewWidget"));
        bottomPanel->setSizePolicy(SizePolicy::HSizeBehavior, SizePolicy::Expanding);
        bottomPanel->setSizePolicy(SizePolicy::VSizeBehavior, SizePolicy::Fixed);
        bottomPanel->setBaseSize(40, 28);
        cenVlut->addWidget(bottomPanel);

        retranslateUi(w);
    }

    void retranslateUi(UIWidget *w)
    {
        caption->setText(_T("Heygen"));
        labelAvatars->setText(_TR(LABEL_AVATARS));
        labelVoices->setText(_TR(LABEL_VOICES));
        labelOptions->setText(_TR(LABEL_OPTIONS));
        labelGenderOpt->setText(_TR(LABEL_GENDER));
        cmbGender->setText(_T("Any"));
        labelLanguageOpt->setText(_TR(LABEL_LANGUAGE));
        cmbLanguage->setText(_T("Any"));
        btnRefresh->setText(_TR(BUTTON_REFRESH));
        labelSpeech->setText(_TR(LABEL_SPEECH));
        textEdit->setText(_T("Hi there! How many cats on the Earth? what do you think?"));
        labelPreview->setText(_TR(LABEL_PREVIEW));
        viewPanel->setText(_T("No preview"));
        labelDetails->setText(_TR(LABEL_VOICE_DETAILS));
        labelGenderDtls->setText(_TR(LABEL_GENDER));
        labelLanguageDtls->setText(_TR(LABEL_LANGUAGE));
        labelSupportPauseDtls->setText(_TR(LABEL_SUPPORT_PAUSE));
        labelSupportEmotDtls->setText(_TR(LABEL_SUPPORT_EMOTION));
        labelSupportInteractDtls->setText(_TR(LABEL_SUPPORT_INTERACT));
        btnPlayVoice->setText(_TR(BUTTON_PLAY_VOICE));
        labelSettings->setText(_TR(LABEL_SETTINGS));
        labelWidth->setText(_TR(LABEL_WIDTH));
        labelHeight->setText(_TR(LABEL_HEIGHT));
        linkHeygenKey->setText(_T("Heygen website"));
        linkHeygenKey->adjustSizeBasedOnContent();
        btnRetrieve->setText(_TR(BUTTON_RETRIEVE_STATUS));
        btnApiKey->setText(_TR(BUTTON_API_KEY));
        btnGenerate->setText(_TR(BUTTON_GENERATE));
    }

private:
    UICaption *caption;
    UILabel *labelAvatars;
    UILabel *labelVoices;
    UILabel *labelOptions;
    UILabel *labelGenderOpt;
    UILabel *labelLanguageOpt;
    UILabel *labelSpeech;
    UILabel *labelPreview;
    UILabel *labelDetails;
    UILabel *labelGenderDtls;
    UILabel *labelLanguageDtls;
    UILabel *labelSupportPauseDtls;
    UILabel *labelSupportEmotDtls;
    UILabel *labelSupportInteractDtls;
    UILabel *labelSettings;
    UILabel *labelWidth;
    UILabel *labelHeight;
};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
}

#endif // UI_MAINWINDOW_H
