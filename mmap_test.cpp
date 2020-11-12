#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
// C++
#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <fstream>

const uint64_t PACKET_SIZE = 1024 * 512;

typedef std::chrono::duration<double, std::ratio<1> > second_;

std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> prev_time;

void ResetTimer() {
    prev_time = std::chrono::high_resolution_clock::now();
}

double GetTime() {
    auto cur_time = std::chrono::high_resolution_clock::now();
    auto delta = std::chrono::duration_cast<second_>(cur_time - prev_time).count();
    prev_time = cur_time;
    return delta;
}

struct MmapTester {
    MmapTester(uint8_t *ptr, int sender) : data_ptr(ptr + sizeof(int) * 2), sender(sender) {
        auto int_ptr = reinterpret_cast<int *>(ptr);
        other_ptr = int_ptr;
        size_ptr = int_ptr + 1;
    }

    std::vector<uint8_t> ReadData() {
        if (closed) {
            return std::vector<uint8_t>();
        }
        int size, other;
        do {
            other = *other_ptr;
            size = *size_ptr;
        } while (!size || other == sender);
        if (size == -1) {
            Close();
            return std::vector<uint8_t>();
        }
        std::vector<uint8_t> data(size);
        std::copy(data_ptr, data_ptr + size, data.data());
        *size_ptr = 0;
        *other_ptr = sender;
        return data;
    }

    void Close() {
        closed = true;
        *size_ptr = -1;
    }

    void WriteData(const std::vector<uint8_t> &bytes) {
        if (closed) {
            return;
        }
        while (*size_ptr) {}
        if (*size_ptr == -1) {
            Close();
            return;
        }
        std::copy(bytes.begin(), bytes.end(), data_ptr);
        *size_ptr = bytes.size();
        *other_ptr = sender;
    }

    int sender;
    int *other_ptr;
    int *size_ptr;
    uint8_t *data_ptr;
    bool closed = false;

    void PrintStats() {
        std::vector<uint8_t> data(PACKET_SIZE);
        for (unsigned char & a : data) a = rand();

        {
            uint64_t repeats = 10;
            double total_latency = 0;
            std::cout << "Computing latency..." << std::endl;
            for (uint64_t k = 0; k < repeats; k++) {
                ResetTimer();
                WriteData(data);
                auto response = ReadData();
                total_latency += GetTime() / 2;
            }
            std::cout << total_latency / repeats << " s" << std::endl;
        }
        {
            ResetTimer();
            std::cout << "Computing throughput..." << std::endl;
            uint64_t blocks = 128;
            for (uint64_t k = 0; k < blocks * (1ull << 20u) / PACKET_SIZE; k++) {
                WriteData(data);
                auto response = ReadData();
            }
            double time = GetTime();
            double throughput = blocks / time * 2;
            std::cout << throughput << " MB/s" << std::endl;
        }
        {
            std::cout << "Computing capacity..." << std::endl;
            double max_throughput = 0;
            for (int kk = 0; kk < 16; kk++) {
                ResetTimer();
                uint64_t blocks = 16;
                for (uint64_t k = 0; k < 16 * (1ull << 20u) / PACKET_SIZE; k++) {
                    WriteData(data);
                    auto response = ReadData();
                }
                double throughput = blocks / GetTime() * 2;
                max_throughput = std::max(max_throughput, throughput);
            }
            std::cout << max_throughput << " MB/s" << std::endl;
        }
    }
};

int main() {
    uint8_t *ptr = reinterpret_cast<uint8_t *>(mmap(NULL, (PACKET_SIZE + 16) * sizeof(uint8_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0));

    auto process_first = new MmapTester(ptr, 0);
    auto process_second = new MmapTester(ptr, 1);

    std::cout << "Benchmark for [MMAP]" << std::endl;
    if (fork() == 0) {
        // child
        std::vector<uint8_t> data;
        do {
            data = process_second->ReadData();
            if (!data.empty()) {
                process_second->WriteData(data);
            }
        } while (!data.empty());
        exit(0);
    }

    process_first->PrintStats();
    process_first->Close();
    return 0;
}
