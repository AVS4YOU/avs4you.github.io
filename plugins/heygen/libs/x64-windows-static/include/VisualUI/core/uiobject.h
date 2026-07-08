#ifndef UIOBJECT_H
#define UIOBJECT_H

#include "uidefines.h"
#include "uisignal.h"


class DECL_VISUALUI UIObject
{
public:
    enum ObjectType : unsigned char {
        ApplicationType,
        WindowType,
        DialogType,
        WidgetType,
        PopupType
    };

    UIObject(const UIObject&) = delete;
    explicit UIObject(ObjectType type, UIObject *parent = nullptr);
    virtual ~UIObject();

    UIObject& operator=(const UIObject&) = delete;

    void setParent(UIObject*) noexcept;
    void setObjectName(const tstring&);
    virtual void setObjectGroupId(const tstring &id);
    UIObject *parent() const noexcept;
    ObjectType objectType() const noexcept;
    tstring objectName() const noexcept;
    tstring objectGroupId() const noexcept;

    template<typename... Args, typename Receiver>
    static typename Signal<Args...>::ConnectionId connect(Signal<Args...>& signal, Receiver* receiver,
                                                          void (Receiver::* method)(Args...) )
    {
        auto slot = [receiver, method](Args... args) {
            (receiver->*method)(args...);
        };

        return signal.connect(std::move(slot), receiver);
    }

    template<typename... Args, typename Receiver, typename Callable>
    static typename std::enable_if<
            !std::is_member_function_pointer<Callable>::value,
            typename Signal<Args...>::ConnectionId
        >::type connect(Signal<Args...>& signal, Receiver* receiver, Callable&& callable)
    {
        return signal.connect(std::forward<Callable>(callable), receiver);
    }

    template<typename... Args>
    static void disconnect(Signal<Args...>& signal, typename Signal<Args...>::ConnectionId id)
    {
        signal.disconnect(id);
    }

    template<typename... Args, typename Receiver>
    static void disconnect(Signal<Args...>& signal, Receiver* receiver)
    {
        signal.disconnect(receiver);
    }

private:
    UIObject();

    UIObject  *m_parent;
    ObjectType m_objectType;
    tstring    m_objectName;
    tstring    m_groupId;
};

#endif // UIOBJECT_H
