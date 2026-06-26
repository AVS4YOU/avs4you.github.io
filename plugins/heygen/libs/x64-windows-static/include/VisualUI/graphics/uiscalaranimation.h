#ifndef UISCALARANIMATION_H
#define UISCALARANIMATION_H

#include "uidefines.h"

class UIScalarAnimationPrivate;
class DECL_VISUALUI UIScalarAnimation
{
public:
    explicit UIScalarAnimation();
    ~UIScalarAnimation();

    void setCurrentValue(double value);
    void setTargetValue(double value);
    void setDuration(int durationMs);
    void setLoop(bool loop);
    void startAnimation();
    void stopAnimation();

    double currentValue() const;
    bool isRunning() const;

    /* callback */
    void onValueChanged(const std::function<void(double)> &callback);

private:
    UIScalarAnimationPrivate *pimpl;
};

#endif // UISCALARANIMATION_H
