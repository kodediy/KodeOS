/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_tilt.c
 * @brief The battery liquid's lean - see kgui_tilt.h.
 */
#include "kgui_tilt.h"

#include <math.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kode.h"

#define TILT_STACK      3072U
#define TILT_PRIO       (tskIDLE_PRIORITY + 1U)
#define TILT_SMOOTH     0.45F    /* the share of a new reading in the filter */
#define TILT_STILL_RAD  0.012F   /* under this (0.7 degrees) a reading is the chip's noise */
#define TILT_JOLT_G     0.30F    /* a change of weight under this is a hand, not a knock */
#define TILT_MILLI      1000.0F
#define TILT_PI         3.14159265F

static atomic_bool    s_tilt_on;
static atomic_int     s_angle_m;   /* radians, in thousandths */
static atomic_int     s_jolt_m;    /* g, in thousandths */
static atomic_bool    s_tilt_known;

/**
 * @brief Read the IMU every KGUI_TILT_PERIOD_MS while on, storing the smoothed lean and any jolt.
 *
 * While off it waits on a task notification with no timeout, so it never
 * wakes the P4 for nothing.
 *
 * @param arg Unused.
 */
static void tilt_task(void *arg)
{
    float lean = 0.0F;   /* radians, smoothed */
    bool  had  = false;

    (void)arg;
    for ( ;; )
    {
        if ( !atomic_load(&s_tilt_on) )
        {
            /* Nobody asks: wait for kgui_tilt_run(true), with no timeout. A
             * sleep of a quarter of a second here woke the P4 four times a
             * second for as long as the board lived, asleep included. */
            had = false;
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        /* kode_imu_read() blocks: it is called on this task, never on AO_Gui's. */
        kode_imu_t const r = kode_imu_read();

        if ( !isnan(r.accel_x) && !isnan(r.accel_y) )
        {
            /* Board X and Y are the chip's -X and -Y; f, the reaction to gravity, points up. */
            float const fx = -r.accel_x;
            float const fy = -r.accel_y;

            if ( sqrtf((fx * fx) + (fy * fy)) >= KGUI_TILT_FLAT_G )
            {
                float const want = atan2f(fx, fy);
                float       off  = want - lean;

                /* The short way round. */
                if ( TILT_PI < off )
                {
                    off -= 2.0F * TILT_PI;
                }
                if ( off < -TILT_PI )
                {
                    off += 2.0F * TILT_PI;
                }
                if ( !had )
                {
                    lean = want;
                    had  = true;
                }
                else if ( fabsf(off) > TILT_STILL_RAD )
                {
                    lean += TILT_SMOOTH * off;
                }
                else
                {
                    /* The chip's noise: a Dot held still is a liquid at rest. */
                }
                atomic_store(&s_angle_m, (int)lrintf(lean * TILT_MILLI));
                atomic_store(&s_tilt_known, true);
            }
            if ( !isnan(r.accel_z) )
            {
                float const weight = sqrtf((r.accel_x * r.accel_x) + (r.accel_y * r.accel_y)
                                           + (r.accel_z * r.accel_z));
                float const over   = fabsf(weight - 1.0F);

                if ( TILT_JOLT_G < over )
                {
                    atomic_store(&s_jolt_m, (int)lrintf(over * TILT_MILLI));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(KGUI_TILT_PERIOD_MS));
    }
}

void kgui_tilt_run(bool on)
{
    static TaskHandle_t s_tilt_task;

    if ( on && (NULL == s_tilt_task) )
    {
        (void)xTaskCreate(&tilt_task, "kgui_tilt", TILT_STACK, NULL, TILT_PRIO, &s_tilt_task);
    }
    atomic_store(&s_tilt_on, on);
    if ( !on )
    {
        atomic_store(&s_tilt_known, false);
    }
    else if ( NULL != s_tilt_task )
    {
        (void)xTaskNotifyGive(s_tilt_task);
    }
    else
    {
        /* No task: there is nothing to read the IMU with. */
    }
}

bool kgui_tilt_read(float *angle, float *jolt)
{
    *angle = (float)atomic_load(&s_angle_m) / TILT_MILLI;
    *jolt  = (float)atomic_exchange(&s_jolt_m, 0) / TILT_MILLI;
    return atomic_load(&s_tilt_known);
}
