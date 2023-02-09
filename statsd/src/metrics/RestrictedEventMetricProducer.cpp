#define STATSD_DEBUG true
#include "Log.h"

#include "RestrictedEventMetricProducer.h"

#include "utils/DbUtils.h"

using std::lock_guard;
using std::vector;

namespace android {
namespace os {
namespace statsd {

RestrictedEventMetricProducer::RestrictedEventMetricProducer(
        const ConfigKey& key, const EventMetric& metric, const int conditionIndex,
        const vector<ConditionState>& initialConditionCache, const sp<ConditionWizard>& wizard,
        const uint64_t protoHash, const int64_t startTimeNs,
        const unordered_map<int, shared_ptr<Activation>>& eventActivationMap,
        const unordered_map<int, vector<shared_ptr<Activation>>>& eventDeactivationMap,
        const vector<int>& slicedStateAtoms,
        const unordered_map<int, unordered_map<int, int64_t>>& stateGroupMap)
    : EventMetricProducer(key, metric, conditionIndex, initialConditionCache, wizard, protoHash,
                          startTimeNs, eventActivationMap, eventDeactivationMap, slicedStateAtoms,
                          stateGroupMap) {
}

void RestrictedEventMetricProducer::onMatchedLogEventInternalLocked(
        const size_t matcherIndex, const MetricDimensionKey& eventKey,
        const ConditionKey& conditionKey, bool condition, const LogEvent& event,
        const std::map<int, HashableDimensionKey>& statePrimaryKeys) {
    if (!condition) {
        return;
    }
    if (!mIsMetricTableCreated) {
        if (!dbutils::createTableIfNeeded(mConfigKey, mMetricId, event)) {
            VLOG("Failed to create table for metric %lld", (long long)mMetricId);
            // TODO(b/268150038): report error to statsdstats
            return;
        }
        mIsMetricTableCreated = true;
    }
    vector<LogEvent> logEvents{event};
    if (!dbutils::insert(mConfigKey, mMetricId, logEvents)) {
        // TODO(b/268150038): report error to statsdstats
        VLOG("Failed to insert logEvent to table for metric %lld", (long long)mMetricId);
    }
}

void RestrictedEventMetricProducer::onDumpReportLocked(
        const int64_t dumpTimeNs, const bool include_current_partial_bucket, const bool erase_data,
        const DumpLatency dumpLatency, std::set<string>* str_set,
        android::util::ProtoOutputStream* protoOutput) {
    // TODO(b/268150038): report error to statsdstats
    VLOG("Unexpected call to onDumpReportLocked() in RestrictedEventMetricProducer");
}

}  // namespace statsd
}  // namespace os
}  // namespace android
