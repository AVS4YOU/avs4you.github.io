#include "apikeydialog.h"
#include "ui_apikeydialog.h"
#include "isltranslator.h"

ApiKeyDialog::ApiKeyDialog(UIWidget *parent, const Rect &rc) :
    UIDialog(parent, rc),
    ui(new Ui::ApiKeyDialog)
{
    setObjectGroupId(_T("PanelWidget"));
    setWindowTitle(_TR(ADD_KEY_CAPTION));
    ui->setupUi(this);

    ui->editKey->setPlaceholderText(_T("Enter the API key"));
    // ui->editKey->setText(_T("YjgxNjAxNmI0Yjg0NDEwYjkzMjUzYmM0N2U2MjY0N2MtMTc1MzI5NjQyNQ=="));

    ui->btnOk->clickSignal.connect([this]() {
        /* TODO: check key is valid */
        accept();
    });

    ui->linkKey->clickSignal.connect([]() {
        std::wstring url = L"https://docs.heygen.com/reference/authentication";
        ShellExecuteW(NULL, L"open", url.c_str(), L"", NULL, SW_SHOWNORMAL);
    });
}

ApiKeyDialog::~ApiKeyDialog()
{
    delete ui, ui = nullptr;
}

std::wstring ApiKeyDialog::key() const
{
    return ui->editKey->text();
}

void ApiKeyDialog::setKey(const std::wstring& key)
{
    ui->editKey->setText(key);
}
