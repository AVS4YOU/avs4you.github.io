#ifndef UISCROLLAREA_H
#define UISCROLLAREA_H

#include "uiabstractscrollarea.h"

class DECL_VISUALUI UIScrollArea : public UIAbstractScrollArea
{
public:
    explicit UIScrollArea(UIWidget *parent = nullptr);
    ~UIScrollArea();

protected:
    virtual void onScrollOffsetChanged() override;
};

#endif // UISCROLLAREA_H
