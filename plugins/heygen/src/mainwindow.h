#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "uiwindow.h"
#include "uipixmap.h"

namespace Ui {
    class MainWindow;
}

class MainWindow : public UIWindow
{
public:
    MainWindow(const Rect &rc, BYTE flags);
    ~MainWindow();

    enum Result : BYTE {
        Accepted,
        Rejected
    };

    tstring filePath() const;
    int result() const;

private:
    inline void calculateFitSize(const Size& imgSize, int w, int h, double &drawW, double &drawH) const noexcept;
    void setImage(const tstring &path);

    void listAvatarsIndexChanged();
    void listVoicesIndexChanged();
    void cmbGenderIndexChanged();
    void cmbLanguageIndexChanged();
    void btnRefreshClick();
    void btnPlayVoiceClick();
    void btnRetrieveClick();
    void btnSetApiKeyClick();
    void btnGenerateClick();
    void linkHeygenKeyClick();

    void onRequestComplete(int, std::wstring, int act);
    void onDownloadComplete(int, std::wstring, std::wstring);
    void shellOpenUrl(const std::wstring& url, const std::wstring &params = L"") const;
    int showMessage(const std::wstring& caption, const std::wstring &msg, const std::vector<int> &buttons);

    void onTimer();
    void onGenerateTimer();
    void onDownloadImageTimer();

    Ui::MainWindow *ui;
    class CGeneratePrivateIntf;
    CGeneratePrivateIntf * m_intf;
    UIPixmap *m_image;
    double m_scale;
    int m_result;
};

#endif // MAINWINDOW_H
