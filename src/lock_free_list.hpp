#pragma once
#include "hazard_pointers.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

template <typename T>
class LockFreeLinkedList {
private:
    struct Node;

    struct MarkedPtr {
        std::uintptr_t bits = 0;

        MarkedPtr() = default;
        MarkedPtr(Node* p, bool marked = false)
            : bits(reinterpret_cast<std::uintptr_t>(p) | (marked ? 1u : 0u)) {}

        Node* get() const { return reinterpret_cast<Node*>(bits & ~std::uintptr_t{1}); }
        bool is_marked() const { return (bits & 1u) != 0; }

        bool operator==(const MarkedPtr& other) const { return bits == other.bits; }
        bool operator!=(const MarkedPtr& other) const { return bits != other.bits; }
    };

    struct Node {
        T value;
        std::atomic<MarkedPtr> next;
        explicit Node(T val) : value(std::move(val)), next(MarkedPtr(nullptr)) {}
    };

    static constexpr int HP_NEXT = 0;
    static constexpr int HP_CURR = 1;
    static constexpr int HP_PREV = 2;

    Node* head;
    Node* tail;

    static void delete_node(void* p) {
        delete static_cast<Node*>(p);
    }

    bool search(const T& value, Node*& prev_out, Node*& curr_out,
                concurrent::hazard::HazardPointerHolder& hp) {
    retry:
        Node* prev = head;
        hp.set(HP_PREV, prev);
        Node* curr = prev->next.load().get();
        hp.set(HP_CURR, curr);
        if (prev->next.load() != MarkedPtr(curr)) {
            goto retry;
        }

        while (true) {
            if (curr == tail) {
                prev_out = prev;
                curr_out = curr;
                return false;
            }

            MarkedPtr next = curr->next.load();
            hp.set(HP_NEXT, next.get());
            if (curr->next.load() != next || prev->next.load() != MarkedPtr(curr)) {
                goto retry;
            }

            if (next.is_marked()) {
                MarkedPtr expected(curr);
                if (!prev->next.compare_exchange_strong(expected, MarkedPtr(next.get()))) {
                    goto retry;
                }
                concurrent::hazard::reclaim_later(curr, delete_node);
                curr = next.get();
                hp.set(HP_CURR, curr);
            } else {
                if (!(curr->value < value)) {
                    prev_out = prev;
                    curr_out = curr;
                    return curr->value == value;
                }
                prev = curr;
                hp.set(HP_PREV, prev);
                curr = next.get();
                hp.set(HP_CURR, curr);
            }
        }
    }

public:
    LockFreeLinkedList() {
        head = new Node(std::numeric_limits<T>::min());
        tail = new Node(std::numeric_limits<T>::max());
        head->next.store(MarkedPtr(tail));
    }

    ~LockFreeLinkedList() {
        Node* current = head->next.load().get();
        while (current != tail) {
            Node* next = current->next.load().get();
            delete current;
            current = next;
        }
        delete head;
        delete tail;
        concurrent::hazard::drain_retired();
    }

    LockFreeLinkedList(const LockFreeLinkedList&) = delete;
    LockFreeLinkedList& operator=(const LockFreeLinkedList&) = delete;

    bool insert(const T& value) {
        concurrent::hazard::HazardPointerHolder hp;
        Node* new_node = new Node(value);
        Node* prev;
        Node* curr;

        while (true) {
            if (search(value, prev, curr, hp)) {
                delete new_node;
                return false;
            }
            new_node->next.store(MarkedPtr(curr));
            MarkedPtr expected(curr);
            if (prev->next.compare_exchange_strong(expected, MarkedPtr(new_node))) {
                return true;
            }
        }
    }

    bool remove(const T& value) {
        concurrent::hazard::HazardPointerHolder hp;
        Node* prev;
        Node* curr;

        while (true) {
            if (!search(value, prev, curr, hp)) {
                return false;
            }

            MarkedPtr next = curr->next.load();
            if (next.is_marked()) {
                continue;
            }
            if (!curr->next.compare_exchange_strong(next, MarkedPtr(next.get(), true))) {
                continue;
            }

            MarkedPtr expected(curr);
            if (prev->next.compare_exchange_strong(expected, MarkedPtr(next.get()))) {
                concurrent::hazard::reclaim_later(curr, delete_node);
            } else {
                search(value, prev, curr, hp);
            }
            return true;
        }
    }

    bool find(const T& value) {
        concurrent::hazard::HazardPointerHolder hp;
        Node* prev;
        Node* curr;
        return search(value, prev, curr, hp);
    }

    bool empty() const {
        return size() == 0;
    }

    size_t size() const {
        size_t count = 0;
        Node* curr = head->next.load().get();
        while (curr != tail) {
            MarkedPtr next = curr->next.load();
            if (!next.is_marked()) {
                ++count;
            }
            curr = next.get();
        }
        return count;
    }
};