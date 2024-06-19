/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/metrics/parsing_utils/histogram_parsing_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <vector>

#include "src/guardrail/StatsdStats.h"
#include "src/statsd_config.pb.h"
#include "tests/metrics/parsing_utils/parsing_test_utils.h"
#include "tests/statsd_test_util.h"

#ifdef __ANDROID__

using namespace std;
using namespace testing;

namespace android {
namespace os {
namespace statsd {
namespace {

using HistogramParsingUtilsTest = InitConfigTest;

TEST_F(HistogramParsingUtilsTest, TestMissingHistogramBinConfigId) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createExplicitBinConfig(/* id */ 1, /* bins */ {5});
    config.mutable_value_metric(0)->mutable_histogram_bin_configs()->Mutable(0)->clear_id();

    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_MISSING_BIN_CONFIG_ID,
                                  config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestMissingHistogramBinConfigBinningStrategy) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    config.mutable_value_metric(0)->add_histogram_bin_configs()->set_id(1);

    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_UNKNOWN_BINNING_STRATEGY,
                                  config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestGeneratedBinsMissingMin) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 1, /* max */ 10, /* count */ 5,
                                     HistogramBinConfig::GeneratedBins::LINEAR);
    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_generated_bins()
            ->clear_min();

    EXPECT_EQ(
            initConfig(config),
            InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_MISSING_GENERATED_BINS_ARGS,
                                config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestGeneratedBinsMissingMax) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 1, /* max */ 10, /* count */ 5,
                                     HistogramBinConfig::GeneratedBins::LINEAR);
    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_generated_bins()
            ->clear_max();

    EXPECT_EQ(
            initConfig(config),
            InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_MISSING_GENERATED_BINS_ARGS,
                                config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestGeneratedBinsMissingCount) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 1, /* max */ 10, /* count */ 5,
                                     HistogramBinConfig::GeneratedBins::LINEAR);
    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_generated_bins()
            ->clear_count();

    EXPECT_EQ(
            initConfig(config),
            InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_MISSING_GENERATED_BINS_ARGS,
                                config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestGeneratedBinsMissingStrategy) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 1, /* max */ 10, /* count */ 5,
                                     HistogramBinConfig::GeneratedBins::UNKNOWN);

    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_generated_bins()
            ->clear_strategy();

    EXPECT_EQ(
            initConfig(config),
            InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_MISSING_GENERATED_BINS_ARGS,
                                config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestGeneratedBinsInvalidMinMax) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 10, /* max */ 10, /* count */ 5,
                                     HistogramBinConfig::GeneratedBins::LINEAR);

    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(
                      INVALID_CONFIG_REASON_VALUE_METRIC_HIST_GENERATED_BINS_INVALID_MIN_MAX,
                      config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestTooFewGeneratedBins) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 10, /* max */ 50, /* count */ 2,
                                     HistogramBinConfig::GeneratedBins::LINEAR);

    EXPECT_EQ(initConfig(config), nullopt);

    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_generated_bins()
            ->set_count(1);

    clearData();
    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_TOO_FEW_BINS,
                                  config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestTooManyGeneratedBins) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createGeneratedBinConfig(/* id */ 1, /* min */ 10, /* max */ 50, /* count */ 100,
                                     HistogramBinConfig::GeneratedBins::LINEAR);

    EXPECT_EQ(initConfig(config), nullopt);

    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_generated_bins()
            ->set_count(101);

    clearData();
    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_TOO_MANY_BINS,
                                  config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestTooFewExplicitBins) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createExplicitBinConfig(/* id */ 1, /* bins */ {1});

    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_TOO_FEW_BINS,
                                  config.value_metric(0).id()));

    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_explicit_bins()
            ->add_bin(2);

    clearData();
    EXPECT_EQ(initConfig(config), nullopt);
}

TEST_F(HistogramParsingUtilsTest, TestTooManyExplicitBins) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);

    vector<float> bins(100);
    // Fill bins with values 1, 2, ..., 100.
    std::iota(std::begin(bins), std::end(bins), 1);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createExplicitBinConfig(/* id */ 1, bins);

    EXPECT_EQ(initConfig(config), nullopt);

    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_explicit_bins()
            ->add_bin(101);

    clearData();
    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(INVALID_CONFIG_REASON_VALUE_METRIC_HIST_TOO_MANY_BINS,
                                  config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestExplicitBinsDuplicateValues) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);

    vector<float> bins(50);
    // Fill bins with values 1, 2, ..., 50.
    std::iota(std::begin(bins), std::end(bins), 1);
    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createExplicitBinConfig(/* id */ 1, bins);

    config.mutable_value_metric(0)
            ->mutable_histogram_bin_configs(0)
            ->mutable_explicit_bins()
            ->add_bin(50);

    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(
                      INVALID_CONFIG_REASON_VALUE_METRIC_HIST_EXPLICIT_BINS_NOT_STRICTLY_ORDERED,
                      config.value_metric(0).id()));
}

TEST_F(HistogramParsingUtilsTest, TestExplicitBinsUnsortedValues) {
    StatsdConfig config;
    *config.add_atom_matcher() = CreateSimpleAtomMatcher("matcher", /* atomId */ 1);
    *config.add_value_metric() =
            createValueMetric("ValueMetric", config.atom_matcher(0), /* valueField */ 1,
                              /* condition */ nullopt, /* states */ {});
    config.mutable_value_metric(0)->set_aggregation_type(ValueMetric::HISTOGRAM);

    vector<float> bins(50);
    // Fill bins with values 1, 2, ..., 50.
    std::iota(std::begin(bins), std::end(bins), 1);

    // Swap values at indices 10 and 40.
    std::swap(bins[10], bins[40]);

    *config.mutable_value_metric(0)->add_histogram_bin_configs() =
            createExplicitBinConfig(/* id */ 1, bins);

    EXPECT_EQ(initConfig(config),
              InvalidConfigReason(
                      INVALID_CONFIG_REASON_VALUE_METRIC_HIST_EXPLICIT_BINS_NOT_STRICTLY_ORDERED,
                      config.value_metric(0).id()));
}

}  // anonymous namespace

}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif
