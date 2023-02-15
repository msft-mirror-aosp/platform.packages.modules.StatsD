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
            const unordered_map<int, unordered_map<int, int64_t>>& stateGroupMap = {});

    void onMetricRemove() override;

private:
    void onMatchedLogEventInternalLocked(
            const size_t matcherIndex, const MetricDimensionKey& eventKey,
            const ConditionKey& conditionKey, bool condition, const LogEvent& event,
            const std::map<int, HashableDimensionKey>& statePrimaryKeys) override;

    void onDumpReportLocked(const int64_t dumpTimeNs, const bool include_current_partial_bucket,
                            const bool erase_data, const DumpLatency dumpLatency,
                            std::set<string>* str_set,
                            android::util::ProtoOutputStream* protoOutput) override;

    bool mIsMetricTableCreated = false;
};

}  // namespace statsd
}  // namespace os
}  // namespace android
#endif  // RESTRICTED_EVENT_METRIC_PRODUCER_H
