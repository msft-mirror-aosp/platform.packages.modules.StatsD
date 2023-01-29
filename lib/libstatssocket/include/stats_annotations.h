/*
 * Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

enum AnnotationIds : uint8_t {
    /**
     * Annotation ID constant for logging UID field.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_IS_UID = 1,

    /**
     * Annotation ID constant to indicate logged atom event's timestamp should be truncated.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_TRUNCATE_TIMESTAMP = 2,

    /**
     * Annotation ID constant for a state atom's primary field.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_PRIMARY_FIELD = 3,

    /**
     * Annotation ID constant for state atom's state field.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_EXCLUSIVE_STATE = 4,

    /**
     * Annotation ID constant to indicate the first UID in the attribution chain
     * is a primary field.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_PRIMARY_FIELD_FIRST_UID = 5,

    /**
     * Annotation ID constant to indicate which state is default for the state atom.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_DEFAULT_STATE = 6,

    /**
     * Annotation ID constant to signal that all states should be reset to the default state.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_TRIGGER_STATE_RESET = 7,

    /**
     * Annotation ID constant to indicate state changes need to account for nesting.
     * This should only be used with binary state atoms.
     *
     * Introduced in API 31.
     */
    ASTATSLOG_ANNOTATION_ID_STATE_NESTED = 8,

    /**
     * Annotation ID constant to indicate the restriction category of an atom.
     * This annotation must only be attached to the atom id. This is an int annotation.
     *
     * Introduced in API 34.
     */
    ASTATSLOG_ANNOTATION_ID_RESTRICTION_CATEGORY = 9,

    /**
     * Annotation ID to indicate that a field of an atom contains peripheral device info.
     * This is a bool annotation.
     *
     * Introduced in API 34.
     */
    ASTATSLOG_ANNOTATION_ID_FIELD_RESTRICTION_PERIPHERAL_DEVICE_INFO = 10,

    /**
     * Annotation ID to indicate that a field of an atom contains coarse app usage personalization.
     * This is a bool annotation.
     *
     * Introduced in API 34.
     */
    ASTATSLOG_ANNOTATION_ID_FIELD_RESTRICTION_COARSE_APP_USAGE_PERSONALIZATION = 11,
};

enum AStatsLogRestrictionCategories : uint32_t {
    /**
     * Restriction category for atoms about diagnostics.
     *
     * Introduced in API 34.
     */
    ASTATSLOG_RESTRICTION_CATEGORY_DIAGNOSTIC = 1,

};
__END_DECLS
