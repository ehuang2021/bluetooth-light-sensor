#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/sys/util.h>
#include <dk_buttons_and_leds.h>

#define UUID 0xFCD2
#define BTHome_flag 0x020106
#define DEVICE_INFO 0x40
#define BATTERY_ID 0x01 /*Battery */
#define ILLUMENCE_ID 0x05

LOG_MODULE_REGISTER(main);  /*log module register*/

static char device_name[] = "light_sensor_proki"; /*name of device*/
#define DEVICE_NAME_LEN (sizeof(device_name) - 1) /*no null terminator*/


__packed typedef struct bthome_data{     /*This creates the struct for the payload. Using a struct allows for dynamic allocation of lux and battery data*/                   
        uint16_t uuid;          /*packs the data together so no weird padding from structs happens*/
        uint8_t device_info;
        uint8_t lux_id;
        uint8_t lux[3]; /*gotta build it myself since spec wants 3 bytes but no def for uint24 :(*/
        uint8_t battery_id;
        uint8_t battery;
} BTHOME_DATA ;




static const struct bt_le_adv_param *adv_param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_NONE, /* No options specified */
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
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR), /*contains flag marker, as well as LE General Discoverable Mode and traditional BT not supported*/
        BT_DATA(BT_DATA_NAME_COMPLETE, device_name, DEVICE_NAME_LEN), /*This is the name segment of packet*/
        BT_DATA(BT_DATA_SVC_DATA16, &bthome_data, sizeof(BTHOME_DATA)) /*Includes the complete data part of packet*/
};


static void update_lux() {

}

static void update_battery() {

}

static void update_ad() {
        bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0); /*updates the advertisment*/
}

int main(void)
{
        int err;

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

        while (1) {
                update_lux(); /*updates the lux value*/
                update_battery(); /*updates the battery value*/
                update_ad(); /*pushes the new info to the bluetooth broadcast*/
        }

        return 0
}
