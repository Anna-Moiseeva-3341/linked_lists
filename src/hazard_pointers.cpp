#include "hazard_pointers.hpp"

#include <algorithm>
#include <vector>

namespace concurrent::hazard {

namespace {

HPRecord hazard_pointer_records[MAX_THREADS];

struct RetiredNode {
    void* ptr;
    std::function<void(void*)> deleter;
    RetiredNode* next;
};

std::atomic<RetiredNode*> retired_list_head{nullptr};
std::atomic<int> retired_count{0};

constexpr int RECLAIM_THRESHOLD = 2 * MAX_THREADS * HP_PER_THREAD;

struct ThreadRecord {
    HPRecord* record = nullptr;

    ~ThreadRecord() {
        if (record) {
            for (auto& p : record->pointers) {
                p.store(nullptr);
            }
            record->active.store(false);
        }
    }
};

thread_local ThreadRecord thread_record;

HPRecord* acquire_record() {
    if (thread_record.record) {
        return thread_record.record;
    }
    for (auto& rec : hazard_pointer_records) {
        bool expected = false;
        if (rec.active.compare_exchange_strong(expected, true)) {
            thread_record.record = &rec;
            return &rec;
        }
    }
    throw std::runtime_error("No more hazard pointer records available");
}

void push_retired(RetiredNode* node) {
    node->next = retired_list_head.load();
    while (!retired_list_head.compare_exchange_weak(node->next, node)) {
    }
}

}

HazardPointerHolder::HazardPointerHolder() : record_(acquire_record()) {}

HazardPointerHolder::~HazardPointerHolder() {
    clear();
}

void HazardPointerHolder::set(int index, void* p) {
    if (index >= 0 && index < HP_PER_THREAD) {
        record_->pointers[index].store(p);
    }
}

void HazardPointerHolder::clear() {
    for (auto& p : record_->pointers) {
        p.store(nullptr);
    }
}

void reclaim_later(void* p, std::function<void(void*)> deleter) {
    push_retired(new RetiredNode{p, std::move(deleter), nullptr});
    if (retired_count.fetch_add(1) + 1 >= RECLAIM_THRESHOLD) {
        drain_retired();
    }
}

void drain_retired() {
    RetiredNode* current = retired_list_head.exchange(nullptr);
    if (!current) {
        return;
    }

    std::vector<void*> hazards;
    for (auto& rec : hazard_pointer_records) {
        if (rec.active.load()) {
            for (auto& p : rec.pointers) {
                if (void* h = p.load()) {
                    hazards.push_back(h);
                }
            }
        }
    }
    std::sort(hazards.begin(), hazards.end());

    int freed = 0;
    while (current) {
        RetiredNode* next = current->next;
        if (std::binary_search(hazards.begin(), hazards.end(), current->ptr)) {
            push_retired(current);
        } else {
            current->deleter(current->ptr);
            delete current;
            ++freed;
        }
        current = next;
    }
    retired_count.fetch_sub(freed);
}

}