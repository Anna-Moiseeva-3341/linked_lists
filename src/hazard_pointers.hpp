#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <stdexcept>

namespace concurrent::hazard {

constexpr int HP_PER_THREAD = 3;   
constexpr int MAX_THREADS = 256;   

struct HPRecord {
    std::atomic<bool> active{false};
    std::atomic<void*> pointers[HP_PER_THREAD] = {};
};

class HazardPointerHolder {
public:
    HazardPointerHolder();
    ~HazardPointerHolder();

    HazardPointerHolder(const HazardPointerHolder&) = delete;
    HazardPointerHolder& operator=(const HazardPointerHolder&) = delete;

    void set(int index, void* p);
    void clear();

private:
    HPRecord* record_;
};

void reclaim_later(void* p, std::function<void(void*)> deleter);
void drain_retired();

}