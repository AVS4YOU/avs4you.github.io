#ifndef UILINEEDIT_H
#define UILINEEDIT_H

#include "uiabstracteditcontrol.h"


class DECL_VISUALUI UILineEdit : public UIAbstractEditControl
{
public:
    explicit UILineEdit(UIWidget *parent = nullptr, const tstring &text = {});
    ~UILineEdit();

protected:
    virtual void onPaint(const RECT &rc) override;

private:
    virtual void calculateCaretPositions() override;
    virtual void updateViewportAndCaret() override;
    virtual void updateCaretOnMouseClick(int x, int y) override;
};

#endif // UILINEEDIT_H
