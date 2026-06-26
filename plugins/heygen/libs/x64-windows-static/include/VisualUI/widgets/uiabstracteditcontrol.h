#ifndef UIABSTRACTEDITCONTROL_H
#define UIABSTRACTEDITCONTROL_H

#include "uiabstractbutton.h"
#include "uiconhandler.h"
#include <vector>

class UIMenu;
class DECL_VISUALUI UIAbstractEditControl : public UIAbstractButton, public UIconHandler
{
public:
    explicit UIAbstractEditControl(UIWidget *parent = nullptr, const tstring &text = {});
    virtual ~UIAbstractEditControl();

    virtual void setText(const tstring &text) override;
    void setPlaceholderText(const tstring &text);
    void setEditable(bool editable);

protected:
#ifdef _WIN32
    virtual bool event(UINT, WPARAM, LPARAM, LRESULT*) override;
#else
    virtual bool event(uint ev_type, void *param) override;
#endif

protected:
    const tstring& displayedText() const;
    virtual void calculateCaretPositions() = 0;
    virtual void updateViewportAndCaret() = 0;
    virtual void updateCaretOnMouseClick(int x, int y) = 0;
    void updateCaretPosition();
    void defaultCaretRect(Rect &rc);

    Rect m_viewportRc;
    bool m_editable;
    size_t m_pos;
    int  m_caretPosX,
         m_caretPosY,
         m_textOffsetX,
         m_textOffsetY;
    std::vector<Point> m_caretPositions;

private:
    void showContextMenu();
    void populateMenu();
    int calculateMenuHeight() const;

#ifdef _WIN32
    bool m_caretCreated;
#else
    GtkWidget *m_caret;
#endif
    UIMenu *m_menu;
    tstring m_sourceText,
            m_placeholderText;
};

#endif // UIABSTRACTEDITCONTROL_H
