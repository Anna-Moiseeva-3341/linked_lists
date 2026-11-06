#include "../src/coarse_lock_list.hpp"
#include "../src/fine_lock_list.hpp"
#include "../src/lock_free_list.hpp"

#include <atomic>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<int> failures{0};

void check(bool condition, const std::string& what) {
    if (!condition) {
        if (failures.fetch_add(1) < 20) {
            std::printf("    FAIL: %s\n", what.c_str());
        }
    }
}

template <template <typename> class List>
void test_basic() {
    List<int> list;
    check(list.empty() && list.size() == 0, "новый список пуст");
    check(list.insert(10) && list.insert(20), "вставка новых значений");
    check(!list.insert(10), "повторная вставка возвращает false");
    check(list.size() == 2, "размер после вставок");
    check(list.find(10) && list.find(20) && !list.find(30), "поиск");
    check(list.remove(10) && !list.find(10) && list.find(20), "удаление");
    check(!list.remove(100), "удаление отсутствующего значения возвращает false");
    check(list.remove(20) && list.empty(), "список пуст после удаления всего");
}

template <template <typename> class List>
void test_against_model() {
    List<int> list;
    std::set<int> model;
    std::mt19937 gen(12345);
    std::uniform_int_distribution<int> key(0, 99);
    std::uniform_int_distribution<int> op(0, 2);

    for (int i = 0; i < 20000; ++i) {
        int k = key(gen);
        switch (op(gen)) {
        case 0:
            check(list.insert(k) == model.insert(k).second, "insert совпадает с эталоном");
            break;
        case 1:
            check(list.remove(k) == (model.erase(k) == 1), "remove совпадает с эталоном");
            break;
        default:
            check(list.find(k) == (model.count(k) == 1), "find совпадает с эталоном");
            break;
        }
    }
    check(list.size() == model.size(), "размер совпадает с эталоном");
    for (int k = 0; k < 100; ++k) {
        check(list.find(k) == (model.count(k) == 1), "итоговое содержимое совпадает с эталоном");
    }
}

template <template <typename> class List>
void test_disjoint_ranges() {
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 1000;
    List<int> list;
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&list, t] {
            int base = t * PER_THREAD;
            for (int i = 0; i < PER_THREAD; ++i) {
                check(list.insert(base + i), "вставка своего ключа");
            }
            for (int i = 0; i < PER_THREAD; i += 2) {
                check(list.find(base + i), "свой ключ находится");
                check(list.remove(base + i), "удаление своего ключа");
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    check(list.size() == THREADS * PER_THREAD / 2, "итоговый размер");
}

template <template <typename> class List>
void test_shared_keys_stress() {
    constexpr int THREADS = 4;
    constexpr int KEYS = 32;
    constexpr int OPS_PER_THREAD = 40000;

    for (int round = 0; round < 5; ++round) {
        List<int> list;
        std::vector<std::vector<long>> inserted(THREADS, std::vector<long>(KEYS, 0));
        std::vector<std::vector<long>> removed(THREADS, std::vector<long>(KEYS, 0));
        std::vector<std::thread> threads;

        for (int t = 0; t < THREADS; ++t) {
            threads.emplace_back([&, t] {
                std::mt19937 gen(1000 * round + t);
                std::uniform_int_distribution<int> key(0, KEYS - 1);
                std::uniform_int_distribution<int> op(0, 2);
                for (int i = 0; i < OPS_PER_THREAD; ++i) {
                    int k = key(gen);
                    switch (op(gen)) {
                    case 0:
                        if (list.insert(k)) ++inserted[t][k];
                        break;
                    case 1:
                        if (list.remove(k)) ++removed[t][k];
                        break;
                    default:
                        list.find(k);
                        break;
                    }
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }

        size_t expected_size = 0;
        for (int k = 0; k < KEYS; ++k) {
            long balance = 0;
            for (int t = 0; t < THREADS; ++t) {
                balance += inserted[t][k] - removed[t][k];
            }
            check(balance == 0 || balance == 1,
                  "ключ " + std::to_string(k) + ": вставок минус удалений = " + std::to_string(balance));
            check(list.find(k) == (balance == 1),
                  "ключ " + std::to_string(k) + ": наличие в списке совпадает с балансом");
            expected_size += (balance == 1);
        }
        check(list.size() == expected_size, "размер совпадает с балансом вставок и удалений");
    }
}

template <template <typename> class List>
void test_many_threads() {
    List<int> list;
    for (int batch = 0; batch < 10; ++batch) {
        std::vector<std::thread> threads;
        for (int i = 0; i < 100; ++i) {
            threads.emplace_back([&list, i] {
                list.insert(i);
                list.find(i);
                list.remove(i);
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
    check(list.empty(), "список пуст после 1000 потоков");
}

template <template <typename> class List>
void run_all(const char* name) {
    std::printf("%s\n", name);
    struct Test {
        const char* name;
        void (*fn)();
    };
    const Test tests[] = {
        {"basic", test_basic<List>},
        {"against_model", test_against_model<List>},
        {"disjoint_ranges", test_disjoint_ranges<List>},
        {"shared_keys_stress", test_shared_keys_stress<List>},
        {"many_threads", test_many_threads<List>},
    };
    for (const auto& test : tests) {
        int before = failures.load();
        test.fn();
        std::printf("  %-20s %s\n", test.name, failures.load() == before ? "OK" : "FAIL");
    }
}

}

int main() {
    run_all<CoarseLinkedList>("CoarseLinkedList");
    run_all<FineGrainedLinkedList>("FineGrainedLinkedList");
    run_all<LockFreeLinkedList>("LockFreeLinkedList");

    if (failures.load() == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\nFAILED: %d checks\n", failures.load());
    return 1;
}