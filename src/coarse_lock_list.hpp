#pragma once
#include <cstddef>
#include <memory>
#include <mutex>

template <typename T>
class CoarseLinkedList {
    struct Node {
        T value;
        std::unique_ptr<Node> next;

        explicit Node(const T& val) : value(val), next(nullptr) {}
    };

    std::unique_ptr<Node> head;
    mutable std::mutex mtx;

public:
    CoarseLinkedList() : head(nullptr) {}

    CoarseLinkedList(const CoarseLinkedList&) = delete;
    CoarseLinkedList& operator=(const CoarseLinkedList&) = delete;

    ~CoarseLinkedList() {
        while (head) {
            head = std::move(head->next);
        }
    }

    bool insert(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);

        std::unique_ptr<Node>* current = &head;
        while (*current && (*current)->value < value) {
            current = &(*current)->next;
        }

        if (*current && (*current)->value == value) {
            return false;
        }

        auto new_node = std::make_unique<Node>(value);
        new_node->next = std::move(*current);
        *current = std::move(new_node);
        return true;
    }

    bool remove(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);

        std::unique_ptr<Node>* current = &head;
        while (*current && (*current)->value < value) {
            current = &(*current)->next;
        }

        if (*current && (*current)->value == value) {
            *current = std::move((*current)->next);
            return true;
        }
        return false;
    }

    bool find(const T& value) const {
        std::lock_guard<std::mutex> lock(mtx);

        Node* current = head.get();
        while (current && current->value < value) {
            current = current->next.get();
        }
        return current && current->value == value;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx);
        return head == nullptr;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        size_t count = 0;
        for (Node* current = head.get(); current; current = current->next.get()) {
            ++count;
        }
        return count;
    }
};