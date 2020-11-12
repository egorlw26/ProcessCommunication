#pragma once
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <fstream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define PACKET_SIZE 1024 * 512

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