#include "src/metrics/RestrictedEventMetricProducer.h"

#include <gtest/gtest.h>

#include "flags/FlagProvider.h"
#include "metrics_test_helper.h"
#include "stats_annotations.h"
#include "tests/statsd_test_util.h"
#include "utils/DbUtils.h"

using namespace testing;
using namespace std;

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {

const ConfigKey configKey(/*uid=*/0, /*id=*/12345);
const int64_t metricId1 = 123;
const int64_t metricId2 = 456;

class RestrictedEventMetricProducerTest : public Test {
protected:
    void SetUp() override {
        FlagProvider::getInstance().overrideFlag(RESTRICTED_METRICS_FLAG, FLAG_TRUE,
                                                 /*isBootFlag=*/true);
    }
    void TearDown() override {
        dbutils::deleteDb(configKey);
        FlagProvider::getInstance().resetOverrides();
    }
};

TEST_F(RestrictedEventMetricProducerTest, TestOnMatchedLogEventMultipleEvents) {
    EventMetric metric;
    metric.set_id(metricId1);
    RestrictedEventMetricProducer producer(configKey, metric,
                                           /*conditionIndex=*/-1,
                                           /*initialConditionCache=*/{}, new ConditionWizard(),
                                           /*protoHash=*/0x1234567890,
                                           /*startTimeNs=*/0);
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(/*timestampNs=*/1);
    std::unique_ptr<LogEvent> event2 = CreateRestrictedLogEvent(/*timestampNs=*/3);

    producer.onMatchedLogEvent(/*matcherIndex=*/1, *event1);
    producer.onMatchedLogEvent(/*matcherIndex=*/1, *event2);

    stringstream query;
    query << "SELECT * FROM metric_" << metricId1;
    vector<int32_t> columnTypes;
    vector<vector<string>> rows;
    dbutils::query(configKey, query.str(), rows, columnTypes);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(columnTypes.size(),
              3 + event1->getValues().size());  // col 0:2 are reserved for metadata.
    EXPECT_EQ(/*tagId=*/rows[0][0], to_string(event1->GetTagId()));
    EXPECT_EQ(/*elapsedTimestampNs=*/rows[0][1], to_string(event1->GetElapsedTimestampNs()));
    EXPECT_EQ(/*elapsedTimestampNs=*/rows[1][1], to_string(event2->GetElapsedTimestampNs()));
}

TEST_F(RestrictedEventMetricProducerTest, TestOnMatchedLogEventMultipleFields) {
    EventMetric metric;
    metric.set_id(metricId2);
    RestrictedEventMetricProducer producer(configKey, metric,
                                           /*conditionIndex=*/-1,
                                           /*initialConditionCache=*/{}, new ConditionWizard(),
                                           /*protoHash=*/0x1234567890,
                                           /*startTimeNs=*/0);
    AStatsEvent* statsEvent = AStatsEvent_obtain();
    AStatsEvent_setAtomId(statsEvent, 1);
    AStatsEvent_addInt32Annotation(statsEvent, ASTATSLOG_ANNOTATION_ID_RESTRICTION_CATEGORY,
                                   ASTATSLOG_RESTRICTION_CATEGORY_DIAGNOSTIC);
    AStatsEvent_overwriteTimestamp(statsEvent, 1);

    AStatsEvent_writeString(statsEvent, "111");
    AStatsEvent_writeInt32(statsEvent, 11);
    AStatsEvent_writeFloat(statsEvent, 11.0);
    LogEvent logEvent(/*uid=*/0, /*pid=*/0);
    parseStatsEventToLogEvent(statsEvent, &logEvent);

    producer.onMatchedLogEvent(/*matcherIndex=1*/ 1, logEvent);

    stringstream query;
    query << "SELECT * FROM metric_" << metricId2;
    vector<int32_t> columnTypes;
    vector<vector<string>> rows;
    EXPECT_TRUE(dbutils::query(configKey, query.str(), rows, columnTypes));
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(columnTypes.size(),
              3 + logEvent.getValues().size());  // col 0:2 are reserved for metadata.
    EXPECT_EQ(/*field1=*/rows[0][3], "111");
    EXPECT_EQ(/*field2=*/rows[0][4], "11");
    EXPECT_FLOAT_EQ(/*field3=*/std::stof(rows[0][5]), 11.0);
}

TEST_F(RestrictedEventMetricProducerTest, TestOnMatchedLogEventWithCondition) {
    EventMetric metric;
    metric.set_id(metricId1);
    metric.set_condition(StringToId("SCREEN_ON"));
    RestrictedEventMetricProducer producer(configKey, metric,
                                           /*conditionIndex=*/0,
                                           /*initialConditionCache=*/{ConditionState::kUnknown},
                                           new ConditionWizard(),
                                           /*protoHash=*/0x1234567890,
                                           /*startTimeNs=*/0);
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(/*timestampNs=*/1);
    std::unique_ptr<LogEvent> event2 = CreateRestrictedLogEvent(/*timestampNs=*/3);

    producer.onConditionChanged(true, 0);
    producer.onMatchedLogEvent(/*matcherIndex=*/1, *event1);
    producer.onConditionChanged(false, 1);
    producer.onMatchedLogEvent(/*matcherIndex=*/1, *event2);

    std::stringstream query;
    query << "SELECT * FROM metric_" << metricId1;
    std::vector<int32_t> columnTypes;
    std::vector<std::vector<std::string>> rows;
    dbutils::query(configKey, query.str(), rows, columnTypes);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(columnTypes.size(), 3 + event1->getValues().size());
    EXPECT_EQ(/*elapsedTimestampNs=*/rows[0][1], to_string(event1->GetElapsedTimestampNs()));
}

TEST_F(RestrictedEventMetricProducerTest, TestOnDumpReportNoOp) {
    EventMetric metric;
    metric.set_id(metricId1);
    RestrictedEventMetricProducer producer(configKey, metric,
                                           /*conditionIndex=*/-1,
                                           /*initialConditionCache=*/{}, new ConditionWizard(),
                                           /*protoHash=*/0x1234567890,
                                           /*startTimeNs=*/0);
    std::unique_ptr<LogEvent> event1 = CreateRestrictedLogEvent(/*timestampNs=*/1);
    producer.onMatchedLogEvent(/*matcherIndex=*/1, *event1);
    ProtoOutputStream output;
    std::set<string> strSet;
    producer.onDumpReport(/*dumpTimeNs=*/10,
                          /*include_current_partial_bucket=*/true,
                          /*erase_data=*/true, FAST, &strSet, &output);

    ASSERT_EQ(output.size(), 0);
    ASSERT_EQ(strSet.size(), 0);
}

}  // namespace statsd
}  // namespace os
}  // namespace android

#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif