#ifndef UIABSTRACTBUTTON_H
#define UIABSTRACTBUTTON_H

#include "uiwidget.h"


class UIToolTipHandler;
class DECL_VISUALUI UIAbstractButton : public UIWidget
{
public:
    explicit UIAbstractButton(UIWidget *parent = nullptr, const tstring &text = {});
    virtual ~UIAbstractButton();

    virtual void setText(const tstring &text);
    void setChecked(bool checked);
    void setSelected(bool enabled) noexcept;
    void setSelectable(bool enabled) noexcept;
    void setToolTip(const tstring &text) noexcept;
    tstring text() const noexcept;
    bool isSelected() const noexcept;
    bool isChecked() const noexcept;
    void restrictClickArea(bool restrict) noexcept;
    void adjustSizeBasedOnContent();

    /* Signals */
    Signal<> clickSignal;

protected:
#ifdef _WIN32
    virtual bool event(UINT, WPARAM, LPARAM, LRESULT*) override;
#else
    virtual bool event(uint ev_type, void *param) override;
#endif
    virtual void click();

    tstring  m_text;
    RECT m_check_rc;
    UIToolTipHandler *m_tooltipHandler;
    bool m_checked,
         m_selected,
         m_selectable,
         m_restrictedClickArea;

private:
};

#endif // UIABSTRACTBUTTON_H
