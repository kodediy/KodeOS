/*
 * SPDX-FileCopyrightText: Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 AND GPL-3.0-or-later
 */
/**
 * @file bootloader_start.c
 * @brief The stock bootloader plus one step: boot the app kodeOS asked for.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_image_format.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "bootloader_flash_priv.h"
#include "esp_flash_partitions.h"
#include "esp_rom_crc.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc_caps.h"
#include "soc/reset_reasons.h"
#include "soc/hp_sys_clkrst_struct.h"
#include "kapps_fmt.h"
#include "kbsp_pins.h"

#if defined(CONFIG_BOOTLOADER_FACTORY_RESET) || defined(CONFIG_BOOTLOADER_APP_TEST)
#error "this override does not implement factory-reset/app-test; port the stock handling first"
#endif

static const char *TAG = "boot.kode";

/**
 * @brief Find a data partition by subtype in the raw partition table.
 *
 * @param subtype Partition subtype to look for (type is always data).
 * @param out     Receives the position of the first match.
 * @return true when a partition with that subtype exists.
 */
static bool find_data_part(uint8_t subtype, esp_partition_pos_t *out)
{
    esp_partition_info_t entries[ESP_PARTITION_TABLE_MAX_ENTRIES];
    if ( bootloader_flash_read(ESP_PARTITION_TABLE_OFFSET, entries,
                              sizeof(entries), true) != ESP_OK )
    {
        return false;
    }
    for ( unsigned i = 0; ESP_PARTITION_TABLE_MAX_ENTRIES > i; ++i )
    {
        if ( ESP_PARTITION_MAGIC != entries[i].magic )
        {
            break;                     /* end of table (or MD5 sentinel) */
        }
        if ( (PART_TYPE_DATA == entries[i].type)
            && (entries[i].subtype == subtype) )
        {
            *out = entries[i].pos;
            return true;
        }
    }
    return false;
}

/**
 * @brief Is there an image (bare or .kfw) waiting in the app_pending mailbox?
 *
 * @return true when one is waiting, or when the mailbox cannot be read.
 */
static bool mailbox_has_image(void)
{
    esp_partition_pos_t pending;
    uint32_t            head = 0U;
    if ( !find_data_part(KAPPS_PENDING_SUBTYPE, &pending) )
    {
        return false;
    }
    if ( bootloader_flash_read(pending.offset, &head, sizeof(head), true)
        != ESP_OK )
    {
        return true;
    }
    if ( (head & 0xFFU) == (uint32_t)ESP_IMAGE_HEADER_MAGIC )
    {
        return true;
    }
    return KAPPS_KFW_MAGIC == head;
}

/* The closed list of resets that keep an armed request is decided in
 * kapps_fmt.h, where the host suite tests it, in the ROM's numbers. These pin
 * each number to the name ESP-IDF gives it, so an IDF whose numbering moved
 * stops the build instead of changing which resets keep a sketch armed. */
_Static_assert(KAPPS_RESET_CORE_SW == (uint32_t)RESET_REASON_CORE_SW, "reset numbering moved");
_Static_assert(KAPPS_RESET_DEEP_SLEEP == (uint32_t)RESET_REASON_CORE_DEEP_SLEEP, "reset numbering moved");
_Static_assert(KAPPS_RESET_CPU_SW == (uint32_t)RESET_REASON_CPU_SW, "reset numbering moved");
_Static_assert(KAPPS_RESET_USB_JTAG == (uint32_t)RESET_REASON_CORE_USB_JTAG, "reset numbering moved");
_Static_assert(KAPPS_RESET_USB_UART == (uint32_t)RESET_REASON_CORE_USB_UART, "reset numbering moved");
_Static_assert(KAPPS_RESET_CPU_JTAG == (uint32_t)RESET_REASON_CPU_JTAG, "reset numbering moved");

/**
 * @brief Boot the requested app; returns only to let the stock path boot kodeOS.
 *
 * @param bs The bootloader state the stock loader was given.
 */
static void try_boot_requested_app(bootloader_state_t *bs)
{
    esp_partition_pos_t apps;
    if ( !find_data_part(KAPPS_PART_SUBTYPE, &apps) )
    {
        return;
    }

    kapps_req_t req;
    if ( bootloader_flash_read(apps.offset + KAPPS_REQ_OFF, &req,
                              sizeof(req), true) != ESP_OK )
    {
        return;
    }
    uint32_t const crc = esp_rom_crc32_le(UINT32_MAX, (const uint8_t *)&req,
                                          offsetof(kapps_req_t, crc));
    if ( !kapps_req_live(&req, crc) )
    {
        return;
    }
    /* The same bounds kodeOS checks before writing the request. */
    if ( !kapps_req_bounds_ok(req.offset, req.size, apps.size) )
    {
        ESP_LOGE(TAG, "boot request out of bounds; ignoring it");
        return;
    }

    uint32_t const state_off = apps.offset + KAPPS_REQ_OFF
                               + offsetof(kapps_req_t, state);

    /* An armed request that no longer stands is retired before the costly
     * verification. */
    if ( KAPPS_REQ_STATE_ARMED == req.state )
    {
        uint32_t const cause = (uint32_t)esp_rom_get_reset_reason(0);
        bool keep = false;
        if ( kapps_req_cause_keeps(cause) )
        {
            /* Read only when it can matter: the mailbox costs a flash read. */
            keep = kapps_req_armed_stands(cause, mailbox_has_image());
        }
        if ( !keep )
        {
            uint32_t tried = KAPPS_REQ_STATE_TRIED;
            bootloader_flash_write(state_off, &tried, sizeof(tried), false);
            ESP_LOGI(TAG, "sticky boot request retired (reset cause 0x%x); booting kodeOS",
                     (unsigned)cause);
            return;
        }
    }

    /* Verify the requested image before committing to boot it. */
    esp_partition_pos_t const req_pos = { .offset = apps.offset + req.offset, .size = req.size };
    esp_image_metadata_t meta;
    if ( esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &req_pos, &meta) != ESP_OK )
    {
        ESP_LOGE(TAG, "requested app does not verify; ignoring the request");
        /* TRIED from any state, so a bad image cannot loop. */
        uint32_t tried = KAPPS_REQ_STATE_TRIED;
        bootloader_flash_write(state_off, &tried, sizeof(tried), false);
        return;
    }

    /* Guard against overflow: ensure there is room in the OTA array. */
    if ( MAX_OTA_SLOTS <= bs->app_count )
    {
        ESP_LOGE(TAG, "OTA table full; cannot boot requested app");
        return;
    }

    /* NEW -> TRIED, ALWAYS -> ARMED before the jump; ARMED writes nothing.
     * A write that fails aborts the boot, or it would jump for ever. */
    uint32_t next = kapps_req_next_state(req.state);
    if ( next != req.state )
    {
        if ( bootloader_flash_write(state_off, &next, sizeof(next), false)
            != ESP_OK )
        {
            ESP_LOGE(TAG, "failed to advance the boot request; aborting boot of requested app");
            return;
        }
    }

    int const idx = (int)bs->app_count;
    bs->ota[idx].offset = apps.offset + req.offset;
    bs->ota[idx].size   = req.size;
    bs->app_count       = (uint32_t)idx + 1U;
    ESP_LOGI(TAG, "booting requested app @0x%08x (%u bytes)",
             (unsigned)bs->ota[idx].offset, (unsigned)req.size);

    /* The stock loader maps and jumps; on a bad image it falls back to kodeOS. */
    bootloader_utility_load_boot_image(bs, idx);
}

/** @brief Panel reset pulse; the display driver does the full one later. */
#define PANEL_RESET_LOW_US 2000U

/**
 * @brief Blank the panel and touch left on by the last firmware.
 *
 * Low, then released to the pull-up rather than driven high, so an
 * unpowered panel is never fed through its reset pin.
 */
static void panel_reset_pulse(void)
{
    esp_rom_gpio_pad_select_gpio(KBSP_PIN_DSP_TP_RST);
    gpio_ll_set_level(&GPIO, KBSP_PIN_DSP_TP_RST, 0U);
    esp_rom_gpio_connect_out_signal(KBSP_PIN_DSP_TP_RST, SIG_GPIO_OUT_IDX,
                                    false, false);
    esp_rom_delay_us(PANEL_RESET_LOW_US);
    gpio_ll_output_disable(&GPIO, KBSP_PIN_DSP_TP_RST);
    esp_rom_gpio_pad_pullup_only(KBSP_PIN_DSP_TP_RST);
}

/**
 * @brief Disarm every GPIO interrupt the last firmware left armed.
 *
 * A software reset does not reset the GPIO block, so a pin's interrupt
 * enable survives into the next firmware. Once that firmware installs IDF's
 * GPIO interrupt service, a level interrupt still asserted on a pin with no
 * handler re-fires forever and the interrupt watchdog resets the board, boot
 * after boot. Here it covers every way into the next firmware - kodeOS to an
 * app, an app home, a panic, a watchdog - whatever SDK that firmware carries.
 */
static void gpio_irq_quiesce(void)
{
    for ( uint32_t pin = 0U; (uint32_t)SOC_GPIO_PIN_COUNT > pin; ++pin )
    {
        gpio_ll_intr_disable(&GPIO, pin);
    }
    gpio_ll_clear_intr_status(&GPIO, UINT32_MAX);
    gpio_ll_clear_intr_status_high(&GPIO, UINT32_MAX);
}

/**
 * @brief Disarm every USB-Serial/JTAG interrupt the last firmware left armed.
 *
 * The USB-Serial/JTAG block keeps its state across every reset but a power-on,
 * so the host stays attached. An Arduino sketch's HWCDC enables BUS_RESET and
 * TOKEN_REC_IN_EP1 as well as the two IDF's driver uses; the driver adds its
 * enables to the ones it finds, and its handler clears only its own two. With
 * the cable in, TOKEN_REC_IN_EP1 then fires on every IN token, nothing clears
 * it, and the interrupt watchdog resets the board, boot after boot. The status
 * bits are left alone: the driver reads them to find data still in the FIFOs.
 */
static void usj_irq_quiesce(void)
{
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
}

void __attribute__((noreturn)) call_start_cpu0(void)
{
    bootloader_state_t bs = {0};

    /* IO_MUX clock back to XTAL before any pin access: a selector left on
     * the PLL survives resets and faults the next boot. */
    HP_SYS_CLKRST.peri_clk_ctrl26.reg_iomux_clk_src_sel = 0U;

    if ( bootloader_init() != ESP_OK )
    {
        bootloader_reset();
    }

    /* After the IO_MUX fix and after bootloader_init() sets the CPU clock. */
    gpio_irq_quiesce();
    usj_irq_quiesce();
    panel_reset_pulse();

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    bootloader_utility_load_boot_image_from_deep_sleep();
#endif
    if ( !bootloader_utility_load_partition_table(&bs) )
    {
        ESP_LOGE(TAG, "load partition table error!");
        bootloader_reset();
    }

    try_boot_requested_app(&bs);

    /* The stock path: otadata (kodeOS's slots) or factory. */
    int const index = bootloader_utility_get_selected_boot_partition(&bs);
    if ( INVALID_INDEX == index )
    {
        bootloader_reset();
    }
    bootloader_utility_load_boot_image(&bs, index);
}
