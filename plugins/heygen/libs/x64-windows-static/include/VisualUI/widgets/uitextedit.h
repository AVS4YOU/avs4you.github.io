#ifndef UITEXTEDIT_H
#define UITEXTEDIT_H

#include "uiabstractscrollarea.h"


class DECL_VISUALUI UITextEdit : public UIAbstractScrollArea
{
public:
    explicit UITextEdit(UIWidget *parent = nullptr, const tstring &text = {});
    ~UITextEdit();

    void setText(const tstring &text) noexcept;
    void setPlaceholderText(const tstring &text) noexcept;
    
    tstring text() const noexcept;

protected:
    virtual void onScrollOffsetChanged() override;

private:
    friend class UITextEditContent;
    
    void calculateContentHeight();
    int getLineHeight() const;

    int  m_lineHeight,
         m_contentHeight;
};

#endif // UITEXTEDIT_H
