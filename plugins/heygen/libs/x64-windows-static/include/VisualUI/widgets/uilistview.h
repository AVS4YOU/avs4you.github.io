#ifndef UILISTVIEW_H
#define UILISTVIEW_H

#include "uiabstractscrollarea.h"
#include <vector>

class UIAbstractButton;
class DECL_VISUALUI UIListView : public UIAbstractScrollArea
{
public:
    enum ScrollMode { ScrollPerPixel, ScrollPerItem };

    explicit UIListView(UIWidget *parent = nullptr);
    ~UIListView();

    void addItem(const tstring &text, uintptr_t data = 0);
    void removeItem(int index);
    void clearList();
    void setCurrentIndex(int index);
    void setRowBaseHeight(int height) noexcept;
    void setScrollMode(ScrollMode mode) noexcept;
    void setActivateOnMouseUp(bool enable) noexcept;
    uintptr_t itemData(int index) const;
    int currentIndex() const noexcept;
    int rowBaseHeight() const noexcept;
    ScrollMode scrollMode() const noexcept;
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
    virtual void onScrollOffsetChanged() override;

private:
    void createVisibleWidgets();
    void updateVisibleItems();
    void updateItemWidget(UIAbstractButton* button, int dataIndex);
    int getFirstVisibleIndex() noexcept;
    int getLastVisibleIndex() noexcept;
    void onKeyUp();
    void onKeyDown();
    
    struct Item {
        tstring text;
        uintptr_t data;
    };

    std::vector<UIAbstractButton*> m_visibleWidgets;
    std::vector<Item> m_items;
    int m_firstVisibleIndex;
    int m_lastVisibleIndex;
    int m_rowBaseHeight;
    int m_currentIndex;
    int m_itemCount;
    bool m_activateOnMouseUp;
    ScrollMode m_scrollMode;
    bool m_keyAlignActive;
};

#endif // UILISTVIEW_H
