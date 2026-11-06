#pragma once
#include <cstddef>
#include <memory>
#include <mutex>

template <typename T>
class FineGrainedLinkedList {
    struct Node {
        T value;
        std::unique_ptr<Node> next;
        mutable std::mutex node_mtx;

        explicit Node(T val) : value(std::move(val)), next(nullptr) {}
    };

    std::unique_ptr<Node> head;
    mutable std::mutex head_mtx;

public:
    FineGrainedLinkedList() : head(nullptr) {}

    FineGrainedLinkedList(const FineGrainedLinkedList&) = delete;
    FineGrainedLinkedList& operator=(const FineGrainedLinkedList&) = delete;

    ~FineGrainedLinkedList() {
        while (head) {
            head = std::move(head->next);
        }
    }

    bool insert(const T& value) {
        std::unique_lock<std::mutex> head_guard(head_mtx);
        if (!head || head->value > value) {
            auto new_node = std::make_unique<Node>(value);
            new_node->next = std::move(head);
            head = std::move(new_node);
            return true;
        }
        if (head->value == value) {
            return false;
        }

        Node* prev = head.get();
        std::unique_lock<std::mutex> prev_guard(prev->node_mtx);
        head_guard.unlock();

        while (true) {
            Node* curr = prev->next.get();

            if (!curr || curr->value > value) {
                auto new_node = std::make_unique<Node>(value);
                new_node->next = std::move(prev->next);
                prev->next = std::move(new_node);
                return true;
            }

            if (curr->value == value) {
                return false;
            }

            std::unique_lock<std::mutex> curr_guard(curr->node_mtx);
            prev_guard = std::move(curr_guard);
            prev = curr;
        }
    }

    bool remove(const T& value) {
        std::unique_lock<std::mutex> head_guard(head_mtx);
        if (!head || head->value > value) {
            return false;
        }

        if (head->value == value) {
            std::unique_ptr<Node> victim;
            {
                std::lock_guard<std::mutex> node_guard(head->node_mtx);
                victim = std::move(head);
                head = std::move(victim->next);
            }
            return true;
        }

        Node* prev = head.get();
        std::unique_lock<std::mutex> prev_guard(prev->node_mtx);
        head_guard.unlock();

        while (true) {
            Node* curr = prev->next.get();

            if (!curr || curr->value > value) {
                return false;
            }

            if (curr->value == value) {
                std::unique_ptr<Node> victim;
                {
                    std::lock_guard<std::mutex> curr_guard(curr->node_mtx);
                    victim = std::move(prev->next);
                    prev->next = std::move(victim->next);
                }
                return true;
            }

            std::unique_lock<std::mutex> curr_guard(curr->node_mtx);
            prev_guard = std::move(curr_guard);
            prev = curr;
        }
    }

    bool find(const T& value) const {
        std::unique_lock<std::mutex> head_guard(head_mtx);
        Node* curr = head.get();
        if (!curr || curr->value > value) {
            return false;
        }
        if (curr->value == value) {
            return true;
        }

        std::unique_lock<std::mutex> curr_guard(curr->node_mtx);
        head_guard.unlock();

        while (true) {
            curr = curr->next.get();
            if (!curr || curr->value > value) {
                return false;
            }
            if (curr->value == value) {
                return true;
            }

            std::unique_lock<std::mutex> next_guard(curr->node_mtx);
            curr_guard = std::move(next_guard);
        }
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(head_mtx);
        return head == nullptr;
    }

    size_t size() const {
        std::unique_lock<std::mutex> head_guard(head_mtx);
        Node* curr = head.get();
        if (!curr) {
            return 0;
        }
        size_t count = 1;

        std::unique_lock<std::mutex> curr_guard(curr->node_mtx);
        head_guard.unlock();

        while (true) {
            curr = curr->next.get();
            if (!curr) {
                break;
            }
            std::unique_lock<std::mutex> next_guard(curr->node_mtx);
            curr_guard = std::move(next_guard);
            ++count;
        }
        return count;
    }
};