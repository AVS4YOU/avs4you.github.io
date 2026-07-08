#ifndef UISIGNAL_H
#define UISIGNAL_H

#include <vector>
#include <functional>
#include <algorithm>


template<typename... Args>
class Signal
{
public:
    using Slot = std::function<void(Args...)>;
    using ConnectionId = size_t;
    
    struct Connection
    {
        Slot slot;
        void* receiver;
        ConnectionId id;

        Connection(Slot s, void* recv, ConnectionId conn_id)
            : slot(std::move(s)), receiver(recv), id(conn_id)
        {}
    };

    ConnectionId connect(Slot slot)
    {
        ConnectionId id = next_id_++;
        connections_.emplace_back(std::move(slot), nullptr, id);
        return id;
    }

    void disconnect(ConnectionId id)
    {
        connections_.erase(
            std::remove_if(connections_.begin(), connections_.end(),
                [id](const Connection& conn) {
                    return conn.id == id;
                }),
            connections_.end());
    }
    
    template<typename Receiver>
    void disconnect(Receiver* receiver)
    {
        connections_.erase(
            std::remove_if(connections_.begin(), connections_.end(),
                [receiver](const Connection& conn) {
                    return conn.receiver == receiver;
                }),
            connections_.end());
    }

    void emit(Args... args) const
    {
        for (const auto& conn : connections_) {
            if (conn.slot) conn.slot(args...);
        }
    }

private:
    friend class UIObject;

    template<typename Receiver>
    ConnectionId connect(Slot slot, Receiver* receiver)
    {
        ConnectionId id = next_id_++;
        connections_.emplace_back(std::move(slot), receiver, id);
        return id;
    }

    std::vector<Connection> connections_;
    ConnectionId next_id_ = 1;
};

#endif // UISIGNAL_H
