#ifndef RESTRICTED_EVENT_METRIC_PRODUCER_H
#define RESTRICTED_EVENT_METRIC_PRODUCER_H

#include <gtest/gtest_prod.h>

#include "EventMetricProducer.h"

namespace android {
namespace os {
namespace statsd {

class RestrictedEventMetricProducer : public EventMetricProducer {
public:
    RestrictedEventMetricProducer(
            const ConfigKey& key, const EventMetric& eventMetric, const int conditionIndex,
            const vector<ConditionState>& initialConditionCache, const sp<ConditionWizard>& wizard,
            const uint64_t protoHash, const int64_t startTimeNs,
            const std::unordered_map<int, std::shared_ptr<Activation>>& eventActivationMap = {},
            const std::unordered_map<int, std::vector<std::shared_ptr<Activation>>>&
                    eventDeactivationMap = {},
            const vector<int>& slicedStateAtoms = {},
            const unordered_map<int, unordered_map<int, int64_t>>& stateGroupMap = {},
            const int restrictedDataTtlInDays = 7);

    void onMetricRemove() override;

    void enforceRestrictedDataTtl(sqlite3* db, const int64_t wallClockNs);

    void flushRestrictedData() override;

private:
    void onMatchedLogEventInternalLocked(
            const size_t matcherIndex, const MetricDimensionKey& eventKey,
            const ConditionKey& conditionKey, bool condition, const LogEvent& event,
            const std::map<int, HashableDimensionKey>& statePrimaryKeys) override;

    void onDumpReportLocked(const int64_t dumpTimeNs, const bool include_current_partial_bucket,
                            const bool erase_data, const DumpLatency dumpLatency,
                            std::set<string>* str_set,
                            android::util::ProtoOutputStream* protoOutput) override;

    void clearPastBucketsLocked(const int64_t dumpTimeNs) override;

    void dropDataLocked(const int64_t dropTimeNs) override;

    bool mIsMetricTableCreated = false;

    const int32_t mRestrictedDataTtlInDays;

    vector<LogEvent> mLogEvents;
};

}  // namespace statsd
}  // namespace os
}  // namespace android
#endif  // RESTRICTED_EVENT_METRIC_PRODUCER_H
