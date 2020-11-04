#include <sys/mman.h>
#include <unistd.h>
// C++
#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <fstream>

const uint64_t PACKET_SIZE = 1024 * 512;

typedef std::chrono::duration<double, std::ratio<1> > second_;

class Timer {
private:
    std::chrono::time_point <std::chrono::system_clock, std::chrono::nanoseconds> prev_time;
public:
    Timer() {
        prev_time = std::chrono::high_resolution_clock::now();
    }

    double GetElapsedSeconds() {
        auto cur_time = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<second_>(cur_time - prev_time).count();
        prev_time = cur_time;
        return delta;
    }
};

struct IO {
    virtual void write_bytes(const std::vector <uint8_t> &bytes) = 0;

    virtual std::vector <uint8_t> read_bytes() = 0;

    virtual void close() = 0;

    void ComputeLatency() {
        uint64_t repeats = 10;
        double total_latency = 0;
        for (uint64_t k = 0; k < repeats; k++) {
            Timer timer;
            std::vector <uint8_t> data(128);
            for (uint64_t i = 0; i < data.size(); i++) {
                data[i] = i * 3 + 23;
            }
            write_bytes(data);
            auto response = read_bytes();
            for (uint64_t i = 0; i < data.size(); i++) {
                //std::cout << (int)data[i] << " " << (int)response[i] << std::endl;
                assert(data[i] == response[i]);
            }
            total_latency += timer.GetElapsedSeconds() / 2;
        }

        std::cout << "Latency: " << total_latency / repeats << " s" << std::endl;
    }

    void ComputeThroughput() {
        Timer timer;
        std::vector <uint8_t> data(PACKET_SIZE);
        for (uint64_t i = 0; i < data.size(); i++) {
            data[i] = i * 3 + 23;
        }
        uint64_t mega_bytes = 128;
        for (uint64_t k = 0; k < mega_bytes * 1024 * 1024 / PACKET_SIZE; k++) {
            write_bytes(data);
            auto response = read_bytes();
            for (uint64_t i = 0; i < data.size(); i++) {
                assert(data[i] == response[i]);
            }
        }
        double time = timer.GetElapsedSeconds();
        double throughput = mega_bytes / time * 2;
        std::cout << "Throughput: " << throughput << " MB/s" << std::endl;
    }
};

struct FileIO : IO {
    FileIO(const std::string &filename, int sender) : sender(sender) {
        file.open(filename, std::fstream::in | std::fstream::out);
        int temp = 0;
        file.seekp(0);
        file.write(reinterpret_cast<const char *>(&sender), sizeof(int));
        file.write(reinterpret_cast<const char *>(&temp), sizeof(int));
        file.flush();
    }

    void write_bytes(const std::vector <uint8_t> &bytes) override {
        if (closed) {
            return;
        }
        int other = 0;
        int prev = 0;
        do {
            file.seekg(0);
            file.read(reinterpret_cast<char *>(&other), sizeof(int));
            file.read(reinterpret_cast<char *>(&prev), sizeof(int));
            if (prev == -1) {
                closed = true;
                return;
            }
        } while (prev != 0);

        file.seekp(2 * sizeof(int));
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        int size = bytes.size();
        file.seekp(0);
        file.write(reinterpret_cast<const char *>(&sender), sizeof(int));
        file.write(reinterpret_cast<const char *>(&size), sizeof(int));
        file.flush();
        //std::cout << "[" << sender << "] Data sent" << std::endl;
    }

    void close() override {
        closed = true;
        int temp = -1;
        file.seekp(0);
        file.write(reinterpret_cast<const char *>(&sender), sizeof(int));
        file.write(reinterpret_cast<const char *>(&temp), sizeof(int));
        file.flush();
    }

    std::vector <uint8_t> read_bytes() override {
        if (closed) {
            return std::vector<uint8_t>();;
        }
        int size = 0;
        int other = 0;
        while (!size || other == sender) {
            file.flush();
            file.seekg(0);
            file.read(reinterpret_cast<char *>(&other), sizeof(int));
            file.read(reinterpret_cast<char *>(&size), sizeof(int));
            //std::cout << "["<<sender << "] From " <<other << " Size:" << size << std::endl;
        }
        //std::cout << "[" <<sender << "] Got some from: " << other << std::endl;
        if (size == -1) {
            closed = true;
            return std::vector<uint8_t>();
        }
        std::vector <uint8_t> data(size);
        file.seekg(sizeof(int) * 2);
        file.read(reinterpret_cast<char *>(data.data()), size);

        int temp = 0;
        file.seekp(0);
        file.write(reinterpret_cast<const char *>(&sender), sizeof(int));
        file.write(reinterpret_cast<const char *>(&temp), sizeof(int));
        file.flush();

        //std::cout << "Data received" << std::endl;
        return data;
    }

    std::fstream file;
    bool closed = false;
    int sender = 0;
};

struct MmapIO : IO {
    MmapIO(uint8_t *ptr, int sender) : data_ptr(ptr + sizeof(int) * 2), sender(sender) {
        auto int_ptr = reinterpret_cast<int *>(ptr);
        other_ptr = int_ptr;
        size_ptr = int_ptr + 1;
    }

    void close() override {
        closed = true;
        *size_ptr = -1;
    }

    void write_bytes(const std::vector <uint8_t> &bytes) override {
        if (closed) {
            return;
        }
        //std::cout << "[" << sender << "] Sending message of length: " << bytes.size() << std::endl;
        while (*size_ptr) {}
        if (*size_ptr == -1) {
            close();
            return;
        }
        std::copy(bytes.begin(), bytes.end(), data_ptr);
        //std::cout << "[" << sender << "] Sent message of length: " << bytes.size() << std::endl;
        *size_ptr = bytes.size();
        *other_ptr = sender;
    }

    std::vector <uint8_t> read_bytes() override {
        if (closed) {
            return std::vector<uint8_t>();
        }
        int size,other;
        do {
            other = *other_ptr;
            size = *size_ptr;
        } while(!size || other == sender);
        if (size == -1) {
            close();
            return std::vector<uint8_t>();
        }
        std::vector<uint8_t> data(size);
        //std::cout << "[" << sender << "] Got message of length: " << size << " from " << other << std::endl;
        std::copy(data_ptr, data_ptr + size, data.data());
        *size_ptr = 0;
        *other_ptr = sender;
        return data;
    }

    int sender;
    int *other_ptr;
    int *size_ptr;
    uint8_t *data_ptr;
    bool closed = false;
};

void RunBenchmark(const std::string & name, IO *io, IO *io_other) {
    std::cout << "Benchmark for method \""<< name << "\" started" << std::endl;
    int p = fork();
    if (p == 0) {
        // child
        std::vector <uint8_t> data;
        do {
            data = io_other->read_bytes();
            //std::cout << "Repeater: got data, repeating " << data.size() << std::endl;
            if (!data.empty()) {
                io_other->write_bytes(data);
            }
        } while (!data.empty());
        exit(0);
    } else {
        // parent
        io->ComputeLatency();
        io->ComputeThroughput();
        io->close();
    }
}

int main() {
    // preparing
    std::ofstream f("file.data");
    f.close();
    uint8_t *ptr = reinterpret_cast<uint8_t *>(mmap(NULL, (PACKET_SIZE + 8) * sizeof(uint8_t),
                                                    PROT_READ | PROT_WRITE,
                                                    MAP_SHARED | MAP_ANONYMOUS,
                                                    0, 0));

    RunBenchmark("Files", new FileIO("file.data", 0), new FileIO("file.data", 1));
    RunBenchmark("Mmap", new MmapIO(ptr, 0), new MmapIO(ptr, 1));
    return 0;
}
