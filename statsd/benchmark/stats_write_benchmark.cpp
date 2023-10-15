/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <statslog.h>

#include "benchmark/benchmark.h"

namespace android {
namespace os {
namespace statsd {

static void BM_StatsEventObtain(benchmark::State& state) {
    while (state.KeepRunning()) {
        AStatsEvent* event = AStatsEvent_obtain();
        benchmark::DoNotOptimize(event);
        AStatsEvent_release(event);
    }
}
BENCHMARK(BM_StatsEventObtain);

static void BM_StatsWrite(benchmark::State& state) {
    const char* reason = "test";
    int64_t boot_end_time = 1234567;
    int64_t total_duration = 100;
    int64_t bootloader_duration = 10;
    int64_t time_since_last_boot = 99999999;
    while (state.KeepRunning()) {
        android::util::stats_write(
                android::util::BOOT_SEQUENCE_REPORTED, reason, reason,
                boot_end_time, total_duration, bootloader_duration, time_since_last_boot);
        total_duration++;
    }
}
BENCHMARK(BM_StatsWrite);

static void BM_StatsWriteViaQueue(benchmark::State& state) {
    // writes dedicated atom which known to be put into the queue for the test purpose
    int32_t uid = 0;
    int32_t label = 100;
    int32_t a_state = 1;
    // TODO: choose atom with a same structure as used in BM_StatsWrite
    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(android::util::stats_write(android::util::APP_BREADCRUMB_REPORTED,
                                                            uid, label, a_state++));
    }
}
BENCHMARK(BM_StatsWriteViaQueue);

}  //  namespace statsd
}  //  namespace os
}  //  namespace android
