#ifndef BMS_SAMPLE_H
#define BMS_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "bms_ntc.h"
#include "bms_types.h"
#include "bq76940.h"

/* Configuration revision advance shared with production-C wrap tests.
 * Natural uint32_t wrap is intentional: an immediate UINT32_MAX-to-zero
 * change still differs from the captured revision. Equality does not claim
 * to detect alias after a complete 2^32 configuration-change cycle; that
 * boundary depends on the product lifecycle/watchdog constraint. */
#define BMS_SAMPLE_CONFIGURATION_REVISION_NEXT(revision_) \
    ((uint32_t)((uint32_t)(revision_) + 1UL))

/*
 * Phase 8 measurement owner. A RunOnce call stages a complete 13-cell/BAT
 * core locally and publishes it atomically through bms_data only after all
 * mandatory transactions succeed. Current is sourced exclusively from the
 * ProtectTask latest-CC mailbox. TS1 is sampled every eighth cycle.
 */
typedef struct
{
    uint32_t success_count;
    uint32_t failure_count;
    uint32_t frame_reject_count;
    uint32_t i2c_timeout_count;
    uint32_t transport_failure_count;
    uint32_t calibration_invalid_count;
    uint32_t configuration_not_ready_count;
    uint32_t cell_group_failure_count;
    uint32_t pack_group_failure_count;
    uint32_t current_group_failure_count;
    uint32_t temperature_group_failure_count;
    uint32_t temperature_conversion_unavailable_count;
    uint32_t ntc_curve_unavailable_count;
    uint32_t data_publish_failure_count;
    uint32_t cc_mailbox_unavailable_count;
    /* A stale sample means at least one valid voltage/current/temperature
     * group exceeded its freshness limit. Invalid groups are not stale. */
    uint32_t stale_sample_count;
    uint32_t stale_transition_count;
    uint32_t stale_check_failure_count;
    /* XREADY/calibration generation guard rejected before any AFE read. */
    uint32_t xready_precheck_reject_count;
    /* XREADY/calibration generation changed before atomic publication. */
    uint32_t xready_postcheck_reject_count;
    uint32_t consecutive_failure_count;
    uint32_t max_consecutive_failure_count;
} BMS_SampleDiagnostics_t;

typedef struct
{
    uint32_t xready_generation;
    uint32_t recovery_revision;
    bool post_clear_verified;
    BQ76940_Calibration_t calibration;
} BMS_SampleCalibrationEvidence_t;

/* Startup initialization. It leaves the module fail-closed until a device
 * and a validated AFE calibration are supplied. A valid calibration is bound
 * to the current inactive XREADY generation and must be rebound after every
 * observed XREADY transition. */
void BMS_Sample_Init(void);

/* These configuration APIs are task/startup context only, never ISR APIs.
 * Before the scheduler starts they assign directly in the single-threaded
 * startup context. With a running scheduler they protect multi-field updates;
 * an already-suspended scheduler is not resumed by this module.
 *
 * The device and pointed-to NTC table must outlive all sampling calls; the
 * installed table must also remain immutable until cleared or replaced.
 * SetNtcTable(NULL, 0) deliberately clears the temperature curve. An
 * invalid nonempty table is rejected transactionally. No curve is embedded
 * in production code because the board NTC curve is not yet validated.
 * SetCalibration rejects and clears its binding while XREADY is active, and
 * is permanently denied after runtime XREADY invalidation. From that point
 * only SetRecoveryCalibration can install provenance-bound calibration. */
void BMS_Sample_SetDevice(BQ76940_t *device);
bool BMS_Sample_SetCalibration(
    const BQ76940_Calibration_t *calibration);
bool BMS_Sample_SetRecoveryCalibration(
    const BMS_SampleCalibrationEvidence_t *evidence,
    uint32_t current_recovery_revision,
    bool handoff_permitted);
void BMS_Sample_InvalidateCalibrationForXready(
    uint32_t xready_generation);
bool BMS_Sample_SetNtcTable(const BMS_NtcPoint_t *points,
                             uint16_t point_count);

#if defined(TEST_PHASE8_SAMPLE_IMAGE)
/* Test-image-only seam for exercising the production revision guard across
 * its immediate natural wrap. This symbol is absent from production images. */
void BMS_Sample_TestSeedConfigurationRevision(uint32_t revision);
#endif

/* One bounded 250 ms-cycle body. Returns true only when bms_data accepted a
 * fully staged core frame. No I2C mutex is held while data is published. The
 * first successful core publication after a device/XREADY epoch change also
 * invalidates previous-epoch current unless same-epoch CC is available. */
bool BMS_Sample_RunOnce(BMS_TimestampMs_t now_ms);

/* Same-generation task-context snapshot; not callable from an ISR. */
BMS_SampleDiagnostics_t BMS_Sample_GetDiagnostics(void);

/* Production FreeRTOS entry. app_rtos.c's Phase 6 placeholder must be
 * removed when this module is added to the target. */
void Task_Sample(void *argument);

#endif /* BMS_SAMPLE_H */
