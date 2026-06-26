#ifndef UITOOLBUTTON_H
#define UITOOLBUTTON_H

#include "uiabstractbutton.h"
#include "uiconhandler.h"

class DECL_VISUALUI UIToolButton : public UIAbstractButton, public UIconHandler
{
public:
    explicit UIToolButton(UIWidget *parent = nullptr, const tstring &text = {});
    ~UIToolButton();

protected:
#ifdef _WIN32
    virtual bool event(UINT, WPARAM, LPARAM, LRESULT*) override;
#else
    virtual bool event(uint ev_type, void *param) override;
#endif
    virtual void onPaint(const RECT &rc) override;
};

#endif // UITOOLBUTTON_H
