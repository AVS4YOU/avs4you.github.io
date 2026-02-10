#ifndef APIKEYDIALOG_H
#define APIKEYDIALOG_H

#include "uidialog.h"

namespace Ui {
    class ApiKeyDialog;
}

class ApiKeyDialog : public UIDialog
{
public:
    ApiKeyDialog(UIWidget *parent = nullptr, const Rect &rc = DEFAULT_DLG_RECT);
    ~ApiKeyDialog();

    std::wstring key() const;
    void setKey(const std::wstring&);

private:
    Ui::ApiKeyDialog *ui;
};

#endif //APIKEYDIALOG_H
