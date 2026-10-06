#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/adc.h>
#include "can_definitions.h"
#include "system_status.h"

/* ======================================================================= */
/*                       PARAMETRY KALIBRACYJNE SPRZĘTU                    */
/* ======================================================================= */
// Środkowa pozycja sprzętowego dzielnika napięcia (R46 6.8k, RV5 2k, R47 3.3k)
#define VOLTAGE_DIVIDER_RATIO    (2.81f) // DO ZMIANY JEZELI JEST USTAWIONY INACZEJ NIZ NA SRODEK

// Offset czujnika prądu wyznaczony na chybił trafił z odczytu ramek w savvyCAN przy braku obciążenia (0A)
#define CURRENT_SENSOR_OFFSET_MV (2036.0f) //POTRZEBNE INFO

// Domyślna czułość czujnika prądu (brak modelu w dokumentacji FCCU v2)
#define CURRENT_SENSOR_SENS_MV_A (40.0f)
/* ======================================================================= */

volatile bool system_is_enabled = false;

const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));

static const struct adc_channel_cfg adc_cfg_voltage = {
        .gain             = ADC_GAIN_1_4,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = 7, // GPIO 8 - FC Voltage
        .differential     = 0
};

static const struct adc_channel_cfg adc_cfg_current = {
        .gain             = ADC_GAIN_1_4,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = 0, // GPIO 1 - FC Current
        .differential     = 0
};

void can_rx_callback(const struct device *dev, struct can_frame *frame, void *user_data) {
    if (frame->id == 0x214) {
        struct hydrogreen_can_definitions_fccu_set_ctrl_t rx_data;
        int unpack_ret = hydrogreen_can_definitions_fccu_set_ctrl_unpack(&rx_data, frame->data, frame->dlc);

        if (unpack_ret == 0) {
            system_is_enabled = rx_data.cmd_system_enable;
        }
    }
}

int main(void) {
    k_sleep(K_MSEC(3000));
    printk("\n\n--- START SYSTEMU FCCU ---\n");

    init_system_status();

    if (!device_is_ready(can_dev)) {
        while (1) { printk("Blad: Urzadzenie CAN nie gotowe!\n"); k_sleep(K_MSEC(1000)); }
    }

    if (!device_is_ready(adc_dev)) {
        while (1) { printk("Blad: Urzadzenie ADC nie gotowe!\n"); k_sleep(K_MSEC(1000)); }
    }

    if (adc_channel_setup(adc_dev, &adc_cfg_voltage) != 0) {
        while (1) { printk("Blad ADC napiecia (CH7)!\n"); k_sleep(K_MSEC(1000)); }
    }

    if (adc_channel_setup(adc_dev, &adc_cfg_current) != 0) {
        while (1) { printk("Blad ADC pradu (CH0)!\n"); k_sleep(K_MSEC(1000)); }
    }

    const struct can_filter rx_filter = {
            .id = 0x214,
            .mask = CAN_STD_ID_MASK
    };
    can_add_rx_filter(can_dev, &can_rx_callback, NULL, &rx_filter);

    can_set_mode(can_dev, CAN_MODE_NORMAL);
    can_start(can_dev);

    int32_t vref = adc_ref_internal(adc_dev);
    printk("Inicjalizacja zakonczona sukcesem! Wchodze w petle glowna...\n");

    while (1) {
        if (system_is_enabled) {
            int16_t sample_v, sample_c;
            float physical_voltage = 0.0f;
            float physical_current = 0.0f;

            // 1. ODCZYT NAPIĘCIA
            struct adc_sequence seq_v = {
                    .channels    = BIT(7),
                    .buffer      = &sample_v,
                    .buffer_size = sizeof(sample_v),
                    .resolution  = 12,
            };
            if (adc_read(adc_dev, &seq_v) == 0) {
                int32_t mv_v = sample_v;
                adc_raw_to_millivolts(vref, ADC_GAIN_1_4, 12, &mv_v);
                physical_voltage = ((float)mv_v / 1000.0f) * VOLTAGE_DIVIDER_RATIO;
            }

            // 2. ODCZYT PRĄDU
            struct adc_sequence seq_c = {
                    .channels    = BIT(0),
                    .buffer      = &sample_c,
                    .buffer_size = sizeof(sample_c),
                    .resolution  = 12,
            };
            if (adc_read(adc_dev, &seq_c) == 0) {
                int32_t mv_c = sample_c;
                adc_raw_to_millivolts(vref, ADC_GAIN_1_4, 12, &mv_c);
                physical_current = ((float)mv_c - CURRENT_SENSOR_OFFSET_MV) / CURRENT_SENSOR_SENS_MV_A;
            }

            // 3. PAKOWANIE I WYSYŁKA
            struct hydrogreen_can_definitions_fccu_power_t power_data;
            power_data.fc_voltage = (int)(CLAMP(physical_voltage, -50.0f, 50.0f) * 100.0f);
            power_data.fc_current = (int)(CLAMP(physical_current, -20.0f, 20.0f) * 100.0f);
            power_data.dcdc_voltage = 0;
            power_data.dcdc_current = 0;
            power_data.load_current = 0;

            uint8_t payload[8];
            hydrogreen_can_definitions_fccu_power_pack(payload, &power_data, 8);

            struct can_frame frame = {
                    .id = 0x201,
                    .dlc = 8,
            };
            memcpy(frame.data, payload, 8);

            if (can_send(can_dev, &frame, K_MSEC(100), NULL, NULL) != 0) {
                set_system_status(STATUS_CAN_ERROR);
            } else {
                set_system_status(STATUS_CAN_OK);
            }
        }
        k_sleep(K_MSEC(1000));
    }
    return 0;
}