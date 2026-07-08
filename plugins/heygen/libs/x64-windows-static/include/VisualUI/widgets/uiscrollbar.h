#ifndef UISCROLLBAR_H
#define UISCROLLBAR_H

#include "uiwidget.h"

class DECL_VISUALUI UIScrollBar : public UIWidget
{
public:
    enum Orientation { Vertical, Horizontal };

    explicit UIScrollBar(Orientation orientation, UIWidget *parent = nullptr);
    ~UIScrollBar();

    void setRange(int min, int max) noexcept;
    void setPageStep(int step) noexcept;
    void setValue(int value) noexcept;
    void setTrackLength(int length) noexcept;
    void updateThumbGeometry(int visibleSize, int totalSize);
    int value() const noexcept;

    /* Signals */
    Signal<int> valueChanged;

private:
    int maxThumbPos() const noexcept;
    void onThumbMoved(int pos);

    UIWidget *m_parent;
    Orientation m_orientation;
    int m_minimum;
    int m_maximum;
    int m_value;
    int m_pageStep;
    int m_trackLength;
};

#endif // UISCROLLBAR_H
