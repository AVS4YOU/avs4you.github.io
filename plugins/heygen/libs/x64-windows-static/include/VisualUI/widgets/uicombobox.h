#ifndef UICOMBOBOX_H
#define UICOMBOBOX_H

#include "uilineedit.h"
#include <vector>

class UIMenu;
class DECL_VISUALUI UIComboBox : public UILineEdit
{
public:
    explicit UIComboBox(UIWidget *parent = nullptr);
    ~UIComboBox();

    void addItem(const tstring &text, uintptr_t data = 0);
    void clearItems();
    void setCurrentIndex(int index, bool emitSignal = false) noexcept;
    void setItemHeight(int height) noexcept;
    void setMaxVisibleItems(int maxItems) noexcept;
    uintptr_t itemData(int index);
    int currentIndex() const noexcept;
    int count() const noexcept;
    bool containsItem(const tstring &text) const;

    /* Signals */
    Signal<int> indexChangedSignal;

protected:
#ifdef _WIN32
    virtual bool event(UINT, WPARAM, LPARAM, LRESULT*) override;
#else
    virtual bool event(uint ev_type, void *param) override;
#endif

private:
    void onClick();
    int calculateMenuHeight() const;
    void populateMenu();

    struct ItemData {
        tstring text;
        uintptr_t data;
    };

    UIMenu *m_menu;
    std::vector<ItemData> m_items;
    int m_itemHeight,
        m_currentIndex,
        m_maxVisibleItems;
    bool m_skipNextClick,
         m_mousePressed;
};

#endif // UICOMBOBOX_H
