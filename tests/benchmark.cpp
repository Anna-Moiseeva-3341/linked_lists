#include "../src/coarse_lock_list.hpp"
#include "../src/fine_lock_list.hpp"
#include "../src/lock_free_list.hpp"

#include <barrier>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

namespace {

struct Config {
    int writers;
    int readers;
};

const std::vector<Config> CONFIGS = {
    {1, 1}, {2, 2}, {4, 4}, {8, 8},
    {7, 1}, {6, 2}, {2, 6}, {1, 7},
};

template <template <typename> class List>
double run_once(const Config& cfg, int ops_per_thread, int key_range, unsigned seed) {
    List<int> list;

    std::mt19937 prefill_gen(seed);
    std::uniform_int_distribution<int> prefill_key(0, key_range - 1);
    for (int i = 0; i < key_range / 2; ++i) {
        list.insert(prefill_key(prefill_gen));
    }

    const int total_threads = cfg.writers + cfg.readers;
    std::barrier start_line(total_threads + 1);
    std::vector<std::thread> threads;

    auto writer = [&](unsigned thread_seed) {
        std::mt19937 gen(thread_seed);
        std::uniform_int_distribution<int> key(0, key_range - 1);
        start_line.arrive_and_wait();
        for (int i = 0; i < ops_per_thread; ++i) {
            if (i % 2 == 0) {
                list.insert(key(gen));
            } else {
                list.remove(key(gen));
            }
        }
    };

    auto reader = [&](unsigned thread_seed) {
        std::mt19937 gen(thread_seed);
        std::uniform_int_distribution<int> key(0, key_range - 1);
        start_line.arrive_and_wait();
        for (int i = 0; i < ops_per_thread; ++i) {
            list.find(key(gen));
        }
    };

    for (int i = 0; i < cfg.writers; ++i) {
        threads.emplace_back(writer, seed * 100 + i);
    }
    for (int i = 0; i < cfg.readers; ++i) {
        threads.emplace_back(reader, seed * 100 + 50 + i);
    }

    start_line.arrive_and_wait();
    auto start = std::chrono::steady_clock::now();
    for (auto& th : threads) {
        th.join();
    }
    auto end = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double>(end - start).count();
    return static_cast<double>(ops_per_thread) * total_threads / seconds;
}

template <template <typename> class List>
void run_impl(const char* name, int repeats, int ops_per_thread, int key_range) {
    for (const auto& cfg : CONFIGS) {
        std::vector<double> results;
        for (int r = 0; r < repeats; ++r) {
            results.push_back(run_once<List>(cfg, ops_per_thread, key_range, 1 + r));
        }

        double mean = 0;
        for (double x : results) mean += x;
        mean /= results.size();

        double var = 0;
        for (double x : results) var += (x - mean) * (x - mean);
        double stddev = results.size() > 1 ? std::sqrt(var / (results.size() - 1)) : 0.0;

        std::printf("%s,%d,%d,%d,%.0f,%.0f\n", name, cfg.writers, cfg.readers,
                    cfg.writers + cfg.readers, mean, stddev);
        std::fflush(stdout);
    }
}

}

int main(int argc, char** argv) {
    int repeats = argc > 1 ? std::atoi(argv[1]) : 5;
    int ops_per_thread = argc > 2 ? std::atoi(argv[2]) : 20000;
    int key_range = argc > 3 ? std::atoi(argv[3]) : 2000;

    std::fprintf(stderr, "hardware threads: %u, repeats: %d, ops per thread: %d, key range: %d\n",
                 std::thread::hardware_concurrency(), repeats, ops_per_thread, key_range);

    std::printf("impl,writers,readers,threads,mean_ops_per_sec,stddev_ops_per_sec\n");
    run_impl<CoarseLinkedList>("Coarse", repeats, ops_per_thread, key_range);
    run_impl<FineGrainedLinkedList>("FineGrained", repeats, ops_per_thread, key_range);
    run_impl<LockFreeLinkedList>("LockFree", repeats, ops_per_thread, key_range);
    return 0;
}