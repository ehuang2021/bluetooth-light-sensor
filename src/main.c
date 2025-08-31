#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/veml7700.h>
#include <zephyr/drivers/adc.h>
#include <hal/nrf_saadc.h>


/*Device Parameters*/
#define LOOP_TIME 5     /*in seconds*/
#define ITTIME 100      /*in millisecond*/

/*BLE Flags*/
#define UUID 0xFCD2
#define BTHOME_FLAG 0x020106
#define DEVICE_INFO 0x40
#define BATTERY_ID 0x01
#define ILLUMENCE_ID 0x05

LOG_MODULE_REGISTER(main);  /*log module register*/


__packed typedef struct bthome_data{     /*This creates the struct for the payload. Using a struct allows for dynamic allocation of lux and battery data*/                   
        uint16_t uuid;          /*packs the data together so no weird padding from structs happens*/
        uint8_t device_info;
        uint8_t lux_id;
        uint8_t lux[3]; /*gotta build it myself since spec wants 3 bytes but no def for uint24 :(*/
        uint8_t battery_id;
        uint8_t battery;
} BTHOME_DATA ;




static const struct bt_le_adv_param *adv_param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_USE_IDENTITY, /* No options specified */
			8000, /* Min Advertising Interval 5s (n*0.625ms) */
			16000, /* Max Advertising Interval 10s (n*0.625ms) */
			NULL); /* Set to NULL for undirected advertising */


static BTHOME_DATA bthome_data = { /*creates bthome payload*/
        .uuid = UUID,
        .device_info = DEVICE_INFO,
        .lux_id = ILLUMENCE_ID,
        .lux = {0,0,0}, /*starts with the default initalizer*/
        .battery_id = BATTERY_ID,
        .battery = 0x00
 };


static const struct bt_data ad[] = { 
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR), /*contains flag marker, as well as LE General Discoverable Mode and traditional BT not supported. BTHOME_FLAG*/
        BT_DATA(BT_DATA_SVC_DATA16, &bthome_data, sizeof(BTHOME_DATA)) /*Includes the complete data part of packet*/
};




/*Hardware Section*/
#define LIGHT_SENSOR_NODE DT_NODELABEL(light_sensor)

/*Zephyr API device object*/
const struct device *const lux_sensor = DEVICE_DT_GET(LIGHT_SENSOR_NODE);

/*Manual I2C bit change object*/
static const struct i2c_dt_spec lux_sensor_i2c = I2C_DT_SPEC_GET(LIGHT_SENSOR_NODE);


#define CONFIG_REGISTER 0x00
/*Configeration number*/
static uint16_t config_reg;

/*Writes to I2C. To use, send in the 16 bit modified reg as val*/
static int write_data16(uint8_t reg, uint16_t val) {

        uint8_t tx[3] = { reg, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8) };
        return i2c_write_dt(&lux_sensor_i2c, tx, sizeof tx);
}

/*Read from I2C to out*/
static int read_data16(uint8_t reg, uint16_t *out) {
    uint8_t rx[2];
    int err = i2c_write_read_dt(&lux_sensor_i2c, &reg, 1, rx, 2);
    if (err) return err;

    *out = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);  // LSB first
    return 0;
}

/*shuts down the sensor*/
static void shutdown_sensor() {
        int config = config_reg | 0x0001; /*Changes last bit (the shutdown bit) to 1, which shuts down the sensor*/
        if (!(write_data16(CONFIG_REGISTER, config))) {
                /*if successful, it will sync global conf.*/
                config_reg = config;
        }
        else {
                LOG_ERR("Write data error / shutdown_sensor\n");
        }
}

/*starts the sensor*/
static void start_sensor() {
        int config = config_reg & ~0x0001; /*Changes last bit (the shutdown bit) to 1, which shuts down the sensor*/
        if (!(write_data16(CONFIG_REGISTER, config))) {
                /*if successful, it will sync global conf.*/
                config_reg = config;
                k_sleep(K_MSEC(3)); /*Waits >2.5ms after start per spec*/
                k_sleep(K_MSEC(ITTIME + 50));
        }
        else {
                LOG_ERR("Write data error / start_sensor\n");
        }
}

/*Initalizes light sensor*/
static int init_light_sensor() {
/*Currently, the ALS_GAIN is set to x2 and ALS_IT is at 100ms. This yields*/
        
        if (!device_is_ready(lux_sensor)) {
                LOG_ERR("lux_sensor is not ready\n");
                return -1;
        }

        struct sensor_value params;

        /*ALS_IT init*/
        params.val1 = VEML7700_ALS_IT_100; /*Sets ALS_IT (scanning duration) to 200ms*/
        params.val2 = 0;

        int err = sensor_attr_set(lux_sensor, SENSOR_CHAN_LIGHT, SENSOR_ATTR_VEML7700_ITIME, &params);
        
        if (err) {
                LOG_ERR("ALS_IT set error\n");
                return -1;
        }
        else {

        }

        /*ALS_GAIN init*/
        params.val1 = VEML7700_ALS_GAIN_2;
        params.val2 = 0;

        err = sensor_attr_set(lux_sensor, SENSOR_CHAN_LIGHT, SENSOR_ATTR_VEML7700_GAIN, &params);

        if (err) {
                LOG_ERR("GAIN set error\n");
                return -1;
        }
        
        if (read_data16(CONFIG_REGISTER, &config_reg)) {
                LOG_ERR("Config init fail, i2c read error\n");
                return -1;
        }
        LOG_INF("VEML7700 config set, Gain = x2, IT = 200ms");
        return 0;
}




static void update_lux() {
        struct sensor_value lux;

        /*prepares sensor for data retrival*/
        int err = sensor_sample_fetch(lux_sensor);
        if (err) {
                LOG_ERR("Sensor fetch error\n");
                return;
        }

        /*saves light to lux*/
        if (!sensor_channel_get(lux_sensor, SENSOR_CHAN_LIGHT, &lux)) {
                LOG_ERR("Sensor read error\n");
                return;
        }

        /*Converts outputted lux value to microlux*/
        int64_t ulux = (int64_t)lux.val1 * 1000000LL + lux.val2;
        if (ulux < 0) {
                /*Prevents any negative numbers*/
                ulux = 0;
        }
        /*Convert to centilux which is what the BTHome protocol calls for*/
        uint32_t clux = (uint32_t)((ulux + 5000) / 10000);
        /*Clamps to 24 bit*/
        if (clux > 0xFFFFFF) {clux = 0xFFFFFF;}


        /*Time to pack into bluetooth packet*/
        bthome_data.lux[0] = (uint8_t) clux; /*Last nibble. With the typecast, it only saves the last 8 bits*/
        bthome_data.lux[1] = (uint8_t) (clux >> 8); /*Middle nibble*/
        bthome_data.lux[2] = (uint8_t) (clux >> 16); /*Final nibble*/
}


/*Battery Implementation*/
#define ADC_NODE DT_NODELABEL(adc) 
#define VBAT_ADC_CHAN_ID   0
#define VBAT_SAMPLES       8                      /* average N reads */
#define ADC_RESOLUTION     12                     /* 12‑bit */
#define ADC_MAX            ((1 << ADC_RESOLUTION) - 1)
static const struct device *adc_dev;

static int battery_adc_init() {
        adc_dev = DEVICE_DT_GET(ADC_NODE);
        if (!device_is_ready(adc_dev)) {
                LOG_ERR("ADC Battery device not ready\n");
                return -1;
        }

    struct adc_channel_cfg ch_cfg = {
        .gain             = ADC_GAIN_1_6,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = VBAT_ADC_CHAN_ID,
        /* nRF-specific: read internal supply via VDD/4 */
        .input_positive   = NRF_SAADC_INPUT_VDD,
    };

    return adc_channel_setup(adc_dev, &ch_cfg);
}


static int read_battery_mv(int *mv_out) {
        int16_t sample = 0;

        const struct adc_sequence seq = {
        .channels    = BIT(VBAT_ADC_CHAN_ID),
        .buffer      = &sample,
        .buffer_size = sizeof(sample),
        .resolution  = ADC_RESOLUTION,
    };

    int err = adc_read(adc_dev, &seq);
    if (err) {
        LOG_ERR("Battery read error code: %d", err);
        return -1;
    }
    int32_t mv = ((int32_t)sample * 3600 * 4) / ADC_MAX;
    *mv_out = mv;
    return 0;

}

static uint8_t vbat_percent_from_mv(int mv)
{
    if (mv >= 3000) return 100;
    if (mv <= 2300) return 0;
    /* quick linear segments; refine later */
    if (mv >= 2850) return 80 + (mv - 2850) * 20 / 150;   // 2850–3000 → 80–100
    if (mv >= 2700) return 50 + (mv - 2700) * 30 / 150;   // 2700–2850 → 50–80
    if (mv >= 2550) return 20 + (mv - 2550) * 30 / 150;   // 2550–2700 → 20–50
    if (mv >= 2400) return 5  + (mv - 2400) * 15 / 150;   // 2400–2550 → 5–20
    return (uint8_t)((mv - 2300) * 5 / 100);              // 2300–2400 → 0–5
}

static void update_battery() {
        int mv = 0;
        if (read_battery_mv(&mv) == 0) {
                bthome_data.battery = vbat_percent_from_mv(mv);
        }
        else {
                bthome_data.battery = 0xff; /*unknown or couldn't read*/
        }
}

static void update_ad() {
        bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0); /*updates the advertisment*/
}

int main(void)
{
        int err;

        /*initalizes the light sensor.*/
        if (init_light_sensor()) {
                return -1;
        }
        /*initalizes battery reader*/
        if (battery_adc_init()) {
                return -1;
        }
        err = bt_enable(NULL); /*initalized bluetooth*/
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return -1;
	}

	LOG_INF("Bluetooth initialized\n");

        err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0); /*starts the advertising. the NULL and 0 repersent the scan packets, which we don't have since this is purely advertising*/
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)\n", err);
		return -1;
	}
        LOG_INF("Starting up broadcasting loop.\n");
        while (1) {
                start_sensor(); /*starts sensor*/
                update_lux(); /*updates the lux value*/
                update_battery(); /*updates the battery value*/
                update_ad(); /*pushes the new info to the bluetooth broadcast*/
                shutdown_sensor(); /**/
                k_sleep(K_SECONDS(LOOP_TIME)); /*Sleeps for 5 seconds*/
        }

        return 0;
}