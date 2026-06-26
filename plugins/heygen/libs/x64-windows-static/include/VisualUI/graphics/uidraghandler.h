#ifndef UIDRAGHANDLER_H
#define UIDRAGHANDLER_H

#include "uidefines.h"
#include "uiplatformtypes.h"
#ifdef __linux__
# include "uicommon.h"
#endif


class UIWidget;
class DECL_VISUALUI UIDragHandler
{
public:
    using DragValidationCallback = std::function<bool(int x, int y)>;

    explicit UIDragHandler(UIWidget *target);
    ~UIDragHandler();

    void handleButtonDownEvent(int x, int y);
    void handleButtonUpEvent();
    void handleMouseMoveEvent(int x, int y);
    void restrictMovementX(bool restrict) noexcept;
    void restrictMovementY(bool restrict) noexcept;
    void onMoveValidation(DragValidationCallback callback);

private:
    bool validateMove(int x, int y) const;

    UIWidget *m_target;
    PlatformWindow m_hWindow;
    DragValidationCallback m_validationCallback;
#ifdef __linux__
    GtkWidget *m_parent;
#endif
    POINT m_dragStart;
    POINT m_winStart;
    bool m_dragging;
    bool m_restrictX;
    bool m_restrictY;
};

#endif // UIDRAGHANDLER_H
