# Потокобезопасные связные списки

Три реализации упорядоченного односвязного списка с операциями `insert`, `remove`, `find`, безопасные для многопоточного выполнения

```
src/
    coarse_lock_list.hpp - один мьютекс на список
    fine_lock_list.hpp - мьютекс в каждом узле
    lock_free_list.hpp - без блокировок, освобождение памяти через hazard pointers
```

## Сборка и запуск
g++ 11 или новее

```
mkdir -p build results

g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    tests/test_lists.cpp src/hazard_pointers.cpp -o build/test_lists
./build/test_lists

g++ -std=c++20 -O2 -DNDEBUG -pthread \
    tests/benchmark.cpp src/hazard_pointers.cpp -o build/benchmark
./build/benchmark 5 20000 2000 > results/bench.csv
```

## Производительность
Список заполняется ключами, писатели поровну выполняют `insert` и `remove` случайных ключей, читатели `find`. Каждый поток выполняет одинаковое число операций.

Замеры: процессор, количество ядер, ОС, компилятор, 5 повторов по 20 000 операций на поток.