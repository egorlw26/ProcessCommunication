#include "common.h"

struct shm_t{
    int id;
    size_t size;
};

shm_t *shm_new(size_t size) {
    shm_t *shm = new shm_t();
    shm->size = size;

    if ((shm->id = shmget(IPC_PRIVATE, size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR)) < 0) {
        perror("shmget");
        free(shm);
        return NULL;
    }

    return shm;
}

void shm_write(shm_t *shm, char *data, int offset, int size) {
    void *shm_data;

    if ((shm_data = shmat(shm->id, NULL, 0)) == (void *) -1) {
        perror("write");
        return;
    }

    memcpy((char *) shm_data + offset, data, size);
    shmdt(shm_data);
}

void shm_read(char *data, shm_t *shm, int offset, int size) {
    void *shm_data;

    if ((shm_data = shmat(shm->id, NULL, 0)) == (void *) -1) {
        perror("read");
        return;
    }
    memcpy(data, (char *) shm_data + offset, size);
    shmdt(shm_data);
}

void shm_del(shm_t *shm) {
    shmctl(shm->id, IPC_RMID, 0);
    free(shm);
}

struct SharedTester {
    SharedTester(shm_t *shm, int sender) : shm(shm), sender(sender) {}

    shm_t *shm;

    void Close() {
        closed = true;
        int temp = -1;
        shm_write(shm, (char *) &temp, 4, 4);
    }

    void WriteData(const std::vector<uint8_t> &bytes) {
        if (closed) {
            return;
        }
        int size = 0;
        while (size) {
            shm_read((char *) &size, shm, 4, 4);
            if (size == -1) {
                Close();
                return;
            }
        }
        shm_write(shm, (char *) bytes.data(), 8, bytes.size());
        int temp = bytes.size();
        shm_write(shm, (char *) &temp, 4, 4);
        shm_write(shm, (char *) &sender, 0, 4);
    }

    std::vector<uint8_t> ReadData() {
        if (closed) {
            return std::vector<uint8_t>();
        }
        int size, other;
        do {
            shm_read((char *) &other, shm, 0, 4);
            shm_read((char *) &size, shm, 4, 4);
        } while (!size || other == sender);
        if (size == -1) {
            Close();
            return std::vector<uint8_t>();
        }
        std::vector<uint8_t> data(size);
        shm_read((char *) data.data(), shm, 8, data.size());

        int temp = 0;
        shm_write(shm, (char *) &temp, 4, 4);
        shm_write(shm, (char *) &sender, 0, 4);
        return data;
    }

    int sender;
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
    shm_t * ptr = shm_new((PACKET_SIZE + 8) * sizeof(uint8_t));

    auto process_first = new SharedTester(ptr, 0);
    auto process_second = new SharedTester(ptr, 1);

    std::cout << "Benchmark for [SHARED]" << std::endl;
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
