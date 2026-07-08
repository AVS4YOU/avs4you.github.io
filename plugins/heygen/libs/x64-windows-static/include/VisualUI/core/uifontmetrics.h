#ifndef UIFONTMETRICS_H
#define UIFONTMETRICS_H

#include "uidefines.h"


class UIWidget;
class DECL_VISUALUI UIFontMetrics
{
public:
    UIFontMetrics(UIWidget *widget);
    ~UIFontMetrics();

    void textSize(const tstring &text, int &width, int &height) const;

private:
    UIWidget *m_widget;
};

#endif // UIFONTMETRICS_H
