/**
 * @file keypad.c
 * Keypad module -- this handles the keypress logic for MCE
 * <p>
 * Copyright © 2004-2011 Nokia Corporation and/or its subsidiary(-ies).
 * <p>
 * @author David Weinehall <david.weinehall@nokia.com>
 *
 * mce is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * mce is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mce.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <glib.h>
#include <gmodule.h>

#include <stdlib.h>			/* exit(), EXIT_FAILURE */

#include "mce.h"
#include "keypad.h"

#include "mce-io.h"			/* mce_close_file(),
					 * mce_write_string_to_file(),
					 * mce_write_number_string_to_file()
					 */
#include "mce-hal.h"			/* get_product_id() */
#include "mce-lib.h"			/* bin_to_string() */
#include "mce-log.h"			/* mce_log(), LL_* */
#include "mce-dbus.h"			/* Direct:
					 * ---
					 * mce_dbus_handler_add(),
					 * dbus_send_message(),
					 * dbus_new_method_reply(),
					 * dbus_message_append_args(),
					 * dbus_message_unref(),
					 * DBusMessage,
					 * DBUS_MESSAGE_TYPE_METHOD_CALL,
					 * DBUS_TYPE_BOOLEAN,
					 * DBUS_TYPE_INVALID,
					 * dbus_bool_t
					 *
					 * Indirect:
					 * ---
					 * MCE_REQUEST_IF,
					 * MCE_KEY_BACKLIGHT_STATE_GET
					 */
#include "datapipe.h"			/* execute_datapipe(),
					 * datapipe_get_gbool(),
					 * datapipe_get_guint(),
					 * datapipe_get_old_gint(),
					 * append_output_trigger_to_datapipe(),
					 * remove_output_trigger_from_datapipe()
					 */
#include "mce-conf.h"			/* mce_conf_get_bool() */

#include "mce-dbus.h"
#include <mce/dbus-names.h>
#include <mce/mode-names.h>

/** Module name */
#define MODULE_NAME		"keypad"

/** Functionality provided by this module */
static const gchar *const provides[] = { MODULE_NAME, NULL };

/** Module information */
G_MODULE_EXPORT module_info_struct module_info = {
	/** Name of the module */
	.name = MODULE_NAME,
	/** Module provides */
	.provides = provides,
	/** Module priority */
	.priority = 100
};

/**
 * The ID of the timeout used for the key backlight
 */
static guint key_backlight_timeout_cb_id = 0;

/** Default backlight brightness */
static gint key_backlight_timeout = DEFAULT_KEY_BACKLIGHT_TIMEOUT;

/** Default backlight fade in time */
static gint key_backlight_fade_in_time = DEFAULT_KEY_BACKLIGHT_FADE_IN_TIME;

/** Default backlight fade out time */
static gint key_backlight_fade_out_time = DEFAULT_KEY_BACKLIGHT_FADE_OUT_TIME;

/** Key backlight enabled/disabled */
static gboolean key_backlight_is_enabled = FALSE;

/** Key backlight channel 0 LED current path */
static gchar *led_current_kb0_path = NULL;
/** Key backlight channel 1 LED current path */
static gchar *led_current_kb1_path = NULL;
/** Key backlight channel 2 LED current path */
static gchar *led_current_kb2_path = NULL;
/** Key backlight channel 3 LED current path */
static gchar *led_current_kb3_path = NULL;
/** Key backlight channel 4 LED current path */
static gchar *led_current_kb4_path = NULL;
/** Key backlight channel 5 LED current path */
static gchar *led_current_kb5_path = NULL;

/** Key backlight channel 0 backlight path */
static gchar *led_brightness_kb0_path = NULL;
/** Key backlight channel 1 backlight path */
static gchar *led_brightness_kb1_path = NULL;
/** Key backlight channel 2 backlight path */
static gchar *led_brightness_kb2_path = NULL;
/** Key backlight channel 3 backlight path */
static gchar *led_brightness_kb3_path = NULL;
/** Key backlight channel 4 backlight path */
static gchar *led_brightness_kb4_path = NULL;
/** Key backlight channel 5 backlight path */
static gchar *led_brightness_kb5_path = NULL;

/** Path to engine 3 mode */
static gchar *engine3_mode_path = NULL;

/** Path to engine 3 load */
static gchar *engine3_load_path = NULL;

/** Path to engine 3 leds */
static gchar *engine3_leds_path = NULL;

/** File pointer for the keyboard backlight 1 LED brightness */
static FILE *led_brightness_kb0_fp = NULL;
/** File pointer for the keyboard backlight 2 LED brightness */
static FILE *led_brightness_kb1_fp = NULL;
/** File pointer for the keyboard backlight 3 LED brightness */
static FILE *led_brightness_kb2_fp = NULL;
/** File pointer for the keyboard backlight 4 LED brightness */
static FILE *led_brightness_kb3_fp = NULL;
/** File pointer for the keyboard backlight 5 LED brightness */
static FILE *led_brightness_kb4_fp = NULL;
/** File pointer for the keyboard backlight 6 LED brightness */
static FILE *led_brightness_kb5_fp = NULL;

/** File pointer for the keyboard backlight 1 LED current */
static FILE *led_current_kb0_fp = NULL;
/** File pointer for the keyboard backlight 2 LED current */
static FILE *led_current_kb1_fp = NULL;
/** File pointer for the keyboard backlight 3 LED current */
static FILE *led_current_kb2_fp = NULL;
/** File pointer for the keyboard backlight 4 LED current */
static FILE *led_current_kb3_fp = NULL;
/** File pointer for the keyboard backlight 5 LED current */
static FILE *led_current_kb4_fp = NULL;
/** File pointer for the keyboard backlight 6 LED current */
static FILE *led_current_kb5_fp = NULL;

/** File pointer for the N810 keypad fadetime */
static FILE *n810_keypad_fadetime_fp = NULL;
/** File pointer for the N810 keyboard fadetime */
static FILE *n810_keyboard_fadetime_fp = NULL;


/** Key backlight mask */
static guint key_backlight_mask = 0;

static void cancel_key_backlight_timeout(void);

/**
 * Setup model specific key backlight values/paths
 */
static void setup_key_backlight(void)
{
	switch (get_product_id()) {
	case PRODUCT_RM690:
	case PRODUCT_RM680:
		key_backlight_mask = MCE_LYSTI_KB_BACKLIGHT_MASK_RM680;

		led_current_kb0_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb1_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL1, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb2_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL2, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb3_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL3, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb4_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL4, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb5_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL5, MCE_LED_CURRENT_SUFFIX, NULL);

		led_brightness_kb0_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb1_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL1, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb2_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL2, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb3_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL3, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb4_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL4, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb5_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL5, MCE_LED_BRIGHTNESS_SUFFIX, NULL);

		engine3_mode_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_DEVICE, MCE_LED_ENGINE3, MCE_LED_MODE_SUFFIX, NULL);
		engine3_load_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_DEVICE, MCE_LED_ENGINE3, MCE_LED_LOAD_SUFFIX, NULL);
		engine3_leds_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_DEVICE, MCE_LED_ENGINE3, MCE_LED_LEDS_SUFFIX, NULL);
		break;

	case PRODUCT_RX51:
		key_backlight_mask = MCE_LYSTI_KB_BACKLIGHT_MASK_RX51;

		led_current_kb0_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb1_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL1, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb2_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL2, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb3_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL3, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb4_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL7, MCE_LED_CURRENT_SUFFIX, NULL);
		led_current_kb5_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL8, MCE_LED_CURRENT_SUFFIX, NULL);

		led_brightness_kb0_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb1_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL1, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb2_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL2, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb3_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL3, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb4_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL7, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb5_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL8, MCE_LED_BRIGHTNESS_SUFFIX, NULL);

		engine3_mode_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_DEVICE, MCE_LED_ENGINE3, MCE_LED_MODE_SUFFIX, NULL);
		engine3_load_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_DEVICE, MCE_LED_ENGINE3, MCE_LED_LOAD_SUFFIX, NULL);
		engine3_leds_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_LP5523_PREFIX, MCE_LED_CHANNEL0, MCE_LED_DEVICE, MCE_LED_ENGINE3, MCE_LED_LEDS_SUFFIX, NULL);
		break;

	case PRODUCT_RX48:
	case PRODUCT_RX44:
		/* Has backlight, but no special setup needed */
		led_brightness_kb0_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_COVER_PREFIX, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		led_brightness_kb1_path = g_strconcat(MCE_LED_DIRECT_SYS_PATH, MCE_LED_KEYBOARD_PREFIX, MCE_LED_BRIGHTNESS_SUFFIX, NULL);
		break;

	default:
		/* No keyboard available */
		break;
	}
}

/**
 * Key backlight brightness for Lysti
 *
 * @param fadetime The fade time
 * @param brightness Backlight brightness
 */
static void set_lysti_backlight_brightness(guint fadetime, guint brightness)
{
	                       /* remux|bright| fade | stop
			        * xxxx   xx            xxxx */
	static gchar pattern[] = "9d80" "4000" "0000" "c000";
	static gchar convert[] = "0123456789abcdef";
	static gint old_brightness = 0;
	gint steps = (gint)brightness - old_brightness;

	/* If we're fading towards 0 and receive a new brightness,
	 * without the backlight timeout being set, the ALS has
	 * adjusted the brightness; just ignore the request
	 */
	if ((old_brightness == 0) && (key_backlight_timeout_cb_id == 0))
		goto EXIT;

	/* Calculate fade time; if fade time is 0, set immediately. If
	 * old and new brightnesses are the same, also write the value
	 * just in case, this also avoids division by zero in other
	 * branch.
	 */
	if ( (fadetime == 0) || (steps == 0) ) {
		/* No fade */
		pattern[6] = convert[(brightness & 0xf0) >> 4];
		pattern[7] = convert[brightness & 0xf];
		pattern[8] = '0';
		pattern[9] = '0';
		pattern[10] = '0';
		pattern[11] = '0';
	} else {
		gint stepspeed;

		/* Figure out how big steps we need to take when
		 * fading (brightness - old_brightness) steps
		 *
		 * During calculations the fade time is multiplied by 1000
		 * to avoid losing precision
		 *
		 * Every step is 0.49ms big
		 */

		/* This should be ok already to avoid division by
		 * zero, but paranoid checking just in case some patch
		 * breaks previous check.
		 */
		if (steps == 0) {
			stepspeed = 1;
		} else {
			stepspeed = (((fadetime * 1000) / ABS(steps)) / 0.49) / 1000;
		}

		/* Sanity check the stepspeed */
		if (stepspeed < 1)
			stepspeed = 1;
		else if (stepspeed > 31)
			stepspeed = 31;

		/* Even for increment, odd for decrement */
		stepspeed *= 2;
		stepspeed += steps > 0 ? 0 : 1;

		/* Start from current brightness */
		pattern[6] = convert[(old_brightness & 0xf0) >> 4];
		pattern[7] = convert[old_brightness & 0xf];

		/* Program the step speed */
		pattern[8] = convert[(stepspeed & 0xf0) >> 4];
		pattern[9] = convert[stepspeed & 0xf];

		/* Program the number of steps */
		pattern[10] = convert[(ABS(steps) & 0xf0) >> 4];
		pattern[11] = convert[ABS(steps) & 0x0f];
	}

	/* Store the new brightness as the current one */
	old_brightness = brightness;

	/* Disable engine 3 */
	(void)mce_write_string_to_file(engine3_mode_path,
				       MCE_LED_DISABLED_MODE);

	/* Turn off all keyboard backlight LEDs */
	(void)mce_write_number_string_to_file(led_brightness_kb0_path, 0, &led_brightness_kb0_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_brightness_kb1_path, 0, &led_brightness_kb1_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_brightness_kb2_path, 0, &led_brightness_kb2_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_brightness_kb3_path, 0, &led_brightness_kb3_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_brightness_kb4_path, 0, &led_brightness_kb4_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_brightness_kb5_path, 0, &led_brightness_kb5_fp, TRUE, FALSE);

	/* Set backlight LED current */
	(void)mce_write_number_string_to_file(led_current_kb0_path, MAXIMUM_LYSTI_BACKLIGHT_LED_CURRENT, &led_current_kb0_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_current_kb1_path, MAXIMUM_LYSTI_BACKLIGHT_LED_CURRENT, &led_current_kb1_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_current_kb2_path, MAXIMUM_LYSTI_BACKLIGHT_LED_CURRENT, &led_current_kb2_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_current_kb3_path, MAXIMUM_LYSTI_BACKLIGHT_LED_CURRENT, &led_current_kb3_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_current_kb4_path, MAXIMUM_LYSTI_BACKLIGHT_LED_CURRENT, &led_current_kb4_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_current_kb5_path, MAXIMUM_LYSTI_BACKLIGHT_LED_CURRENT, &led_current_kb5_fp, TRUE, FALSE);

	/* Engine 3 */
	(void)mce_write_string_to_file(engine3_mode_path,
				       MCE_LED_LOAD_MODE);

	(void)mce_write_string_to_file(engine3_leds_path,
				       bin_to_string(key_backlight_mask));
	(void)mce_write_string_to_file(engine3_load_path,
				       pattern);
	(void)mce_write_string_to_file(engine3_mode_path,
				       MCE_LED_RUN_MODE);

EXIT:
	return;
}

/**
 * Key backlight brightness for N810/N810 WiMAX Edition
 *
 * @param fadetime The fade time
 * @param brightness Backlight brightness
 */
static void set_n810_backlight_brightness(guint fadetime, guint brightness)
{
	/* Set fade time */
	if (brightness == 0) {
		(void)mce_write_number_string_to_file(MCE_KEYPAD_BACKLIGHT_FADETIME_SYS_PATH, fadetime, &n810_keypad_fadetime_fp, TRUE, FALSE);
		(void)mce_write_number_string_to_file(MCE_KEYBOARD_BACKLIGHT_FADETIME_SYS_PATH, fadetime, &n810_keyboard_fadetime_fp, TRUE, FALSE);
	} else {
		(void)mce_write_number_string_to_file(MCE_KEYPAD_BACKLIGHT_FADETIME_SYS_PATH, 0, &n810_keypad_fadetime_fp, TRUE, FALSE);
		(void)mce_write_number_string_to_file(MCE_KEYBOARD_BACKLIGHT_FADETIME_SYS_PATH, 0, &n810_keyboard_fadetime_fp, TRUE, FALSE);
	}

	(void)mce_write_number_string_to_file(led_brightness_kb0_path, brightness, &led_brightness_kb0_fp, TRUE, FALSE);
	(void)mce_write_number_string_to_file(led_brightness_kb1_path, brightness, &led_brightness_kb1_fp, TRUE, FALSE);
}

/**
 * Set key backlight brightness
 *
 * @param data Backlight brightness passed as a gconstpointer
 */
static void set_backlight_brightness(gconstpointer data)
{
	static gint cached_brightness = -1;
	gint new_brightness = GPOINTER_TO_INT(data);
	gint fadetime;

	if (new_brightness == 0) {
		fadetime = key_backlight_fade_out_time;
	} else {
		fadetime = key_backlight_fade_in_time;
	}

	/* If we're just rehashing the same brightness value, don't bother */
	if ((new_brightness == cached_brightness) || (new_brightness == -1))
		goto EXIT;

	cached_brightness = new_brightness;

	key_backlight_is_enabled = (new_brightness != 0);

	/* Product specific key backlight handling */
	switch (get_product_id()) {
	case PRODUCT_RM690:
	case PRODUCT_RM680:
	case PRODUCT_RX51:
		set_lysti_backlight_brightness(fadetime, new_brightness);
		break;

	case PRODUCT_RX48:
	case PRODUCT_RX44:
		set_n810_backlight_brightness(fadetime, new_brightness);
		break;

	default:
		break;
	}

EXIT:
	return;
}

/**
 * Disable key backlight
 */
static void disable_key_backlight(void)
{
	cancel_key_backlight_timeout();

	execute_datapipe(&key_backlight_pipe, GINT_TO_POINTER(0),
			 USE_INDATA, CACHE_INDATA);
}

/**
 * Timeout callback for key backlight
 *
 * @param data Unused
 * @return Always returns FALSE, to disable the timeout
 */
static gboolean key_backlight_timeout_cb(gpointer data)
{
	(void)data;

	key_backlight_timeout_cb_id = 0;

	disable_key_backlight();

	return FALSE;
}

/**
 * Cancel key backlight timeout
 */
static void cancel_key_backlight_timeout(void)
{
	if (key_backlight_timeout_cb_id != 0) {
		g_source_remove(key_backlight_timeout_cb_id);
		key_backlight_timeout_cb_id = 0;
	}
}

/**
 * Setup key backlight timeout
 */
static void setup_key_backlight_timeout(void)
{
	cancel_key_backlight_timeout();

	/* Setup a new timeout */
	key_backlight_timeout_cb_id =
		g_timeout_add_seconds(key_backlight_timeout,
				      key_backlight_timeout_cb, NULL);
}

/**
 * Enable key backlight
 */
static void enable_key_backlight(void)
{
	cancel_key_backlight_timeout();

	/* Only enable the key backlight if the slide is open */
	if (datapipe_get_gint(keyboard_slide_pipe) != COVER_OPEN)
		goto EXIT;

	setup_key_backlight_timeout();

	/* If the backlight is off, turn it on */
	if (datapipe_get_guint(key_backlight_pipe) == 0) {
		execute_datapipe(&key_backlight_pipe,
				 GINT_TO_POINTER(DEFAULT_KEY_BACKLIGHT_LEVEL),
				 USE_INDATA, CACHE_INDATA);
	}

EXIT:
	return;
}

/**
 * Policy based enabling of key backlight
 */
static void enable_key_backlight_policy(void)
{
	cover_state_t kbd_slide_state = datapipe_get_gint(keyboard_slide_pipe);
	system_state_t system_state = datapipe_get_gint(system_state_pipe);
	alarm_ui_state_t alarm_ui_state =
		datapipe_get_gint(alarm_ui_state_pipe);

	/* If the keyboard slide isn't open, there's no point in enabling
	 * the backlight
	 *
	 * XXX: this policy will have to change if/when we get devices
	 * with external keypads that needs to be backlit, but for now
	 * that's not an issue
	 */
	if (kbd_slide_state != COVER_OPEN)
		goto EXIT;

	/* Only enable the key backlight in USER state
	 * and when the alarm dialog is visible
	 */
	if ((system_state == MCE_STATE_USER) ||
	    ((alarm_ui_state == MCE_ALARM_UI_VISIBLE_INT32) ||
	     (alarm_ui_state == MCE_ALARM_UI_RINGING_INT32))) {
		/* If there's a key backlight timeout active, restart it,
		 * else enable the backlight
		 */
		if (key_backlight_timeout_cb_id != 0)
			setup_key_backlight_timeout();
		else
			enable_key_backlight();
	}

EXIT:
	return;
}

/**
 * Send a key backlight state reply
 *
 * @param method_call A DBusMessage to reply to
 * @return TRUE on success, FALSE on failure
 */
static gboolean send_key_backlight_state(DBusMessage *const method_call)
{
	DBusMessage *msg = NULL;
	dbus_bool_t state = key_backlight_is_enabled;
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Sending key backlight state: %d",
		state);

	msg = dbus_new_method_reply(method_call);

	/* Append the display status */
	if (dbus_message_append_args(msg,
				     DBUS_TYPE_BOOLEAN, &state,
				     DBUS_TYPE_INVALID) == FALSE) {
		mce_log(LL_CRIT,
			"Failed to append reply argument to D-Bus message "
			"for %s.%s",
			MCE_REQUEST_IF, MCE_KEY_BACKLIGHT_STATE_GET);
		dbus_message_unref(msg);
		goto EXIT;
	}

	/* Send the message */
	status = dbus_send_message(msg);

EXIT:
	return status;
}

/**
 * D-Bus callback for the get key backlight state method call
 *
 * @param msg The D-Bus message
 * @return TRUE on success, FALSE on failure
 */
static gboolean key_backlight_state_get_dbus_cb(DBusMessage *const msg)
{
	gboolean status = FALSE;

	mce_log(LL_DEBUG,
		"Received key backlight state get request");

	/* Try to send a reply that contains the current key backlight state */
	if (send_key_backlight_state(msg) == FALSE)
		goto EXIT;

	status = TRUE;

EXIT:
	return status;
}

/**
 * Datapipe trigger for device inactivity
 *
 * @param data The inactivity stored in a pointer;
 *             TRUE if the device is inactive,
 *             FALSE if the device is active
 */
static void device_inactive_trigger(gconstpointer const data)
{
	gboolean device_inactive = GPOINTER_TO_INT(data);

	if (device_inactive == FALSE)
		enable_key_backlight_policy();
}

/**
 * Datapipe trigger for the keyboard slide
 *
 * @param data The keyboard slide state stored in a pointer;
 *             COVER_OPEN if the keyboard is open,
 *             COVER_CLOSED if the keyboard is closed
 */
static void keyboard_slide_trigger(gconstpointer const data)
{
	if ((GPOINTER_TO_INT(data) == COVER_OPEN) &&
	    ((mce_get_submode_int32() & MCE_TKLOCK_SUBMODE) == 0)) {
		enable_key_backlight_policy();
	} else {
		disable_key_backlight();
	}
}

/**
 * Datapipe trigger for display state
 *
 * @param data The display stated stored in a pointer
 */
static void display_state_trigger(gconstpointer data)
{
	static display_state_t old_display_state = MCE_DISPLAY_UNDEF;
	display_state_t display_state = GPOINTER_TO_INT(data);

	if (old_display_state == display_state)
		goto EXIT;

	/* Disable the key backlight if the display dims */
	switch (display_state) {
	case MCE_DISPLAY_OFF:
	case MCE_DISPLAY_LPM_OFF:
	case MCE_DISPLAY_LPM_ON:
	case MCE_DISPLAY_DIM:
		disable_key_backlight();
		break;

	case MCE_DISPLAY_ON:
		if (old_display_state != MCE_DISPLAY_ON)
			enable_key_backlight_policy();

		break;

	case MCE_DISPLAY_UNDEF:
	default:
		break;
	}

	old_display_state = display_state;

EXIT:
	return;
}

/**
 * Handle system state change
 *
 * @param data The system state stored in a pointer
 */
static void system_state_trigger(gconstpointer data)
{
	system_state_t system_state = GPOINTER_TO_INT(data);

	/* If we're changing to another state than USER,
	 * disable the key backlight
	 */
	if (system_state != MCE_STATE_USER)
		disable_key_backlight();
}

/**
 * Init function for the keypad module
 *
 * @todo XXX status needs to be set on error!
 *
 * @param module Unused
 * @return NULL on success, a string with an error message on failure
 */
G_MODULE_EXPORT const gchar *g_module_check_init(GModule *module);
const gchar *g_module_check_init(GModule *module)
{
	gchar *status = NULL;

	(void)module;

	/* Append triggers/filters to datapipes */
	append_output_trigger_to_datapipe(&system_state_pipe,
					  system_state_trigger);
	append_output_trigger_to_datapipe(&key_backlight_pipe,
					  set_backlight_brightness);
	append_output_trigger_to_datapipe(&device_inactive_pipe,
					  device_inactive_trigger);
	append_output_trigger_to_datapipe(&keyboard_slide_pipe,
					  keyboard_slide_trigger);
	append_output_trigger_to_datapipe(&display_state_pipe,
					  display_state_trigger);

	/* Get configuration options */
	key_backlight_timeout =
		mce_conf_get_int(MCE_CONF_KEYPAD_GROUP,
				 MCE_CONF_KEY_BACKLIGHT_TIMEOUT,
				 DEFAULT_KEY_BACKLIGHT_TIMEOUT,
				 NULL);

	key_backlight_fade_in_time =
		mce_conf_get_int(MCE_CONF_KEYPAD_GROUP,
			         MCE_CONF_KEY_BACKLIGHT_FADE_IN_TIME,
			         DEFAULT_KEY_BACKLIGHT_FADE_IN_TIME,
			         NULL);

	if (((key_backlight_fade_in_time % 125) != 0) &&
	    (key_backlight_fade_in_time > 1000))
		key_backlight_fade_in_time =
			DEFAULT_KEY_BACKLIGHT_FADE_IN_TIME;

	key_backlight_fade_out_time =
		mce_conf_get_int(MCE_CONF_KEYPAD_GROUP,
			         MCE_CONF_KEY_BACKLIGHT_FADE_OUT_TIME,
			         DEFAULT_KEY_BACKLIGHT_FADE_OUT_TIME,
			         NULL);

	if (((key_backlight_fade_out_time % 125) != 0) &&
	    (key_backlight_fade_out_time > 1000))
		key_backlight_fade_out_time =
			DEFAULT_KEY_BACKLIGHT_FADE_OUT_TIME;

	/* get_key_backlight_state */
	if (mce_dbus_handler_add(MCE_REQUEST_IF,
				 MCE_KEY_BACKLIGHT_STATE_GET,
				 NULL,
				 DBUS_MESSAGE_TYPE_METHOD_CALL,
				 key_backlight_state_get_dbus_cb) == NULL)
		goto EXIT;

	setup_key_backlight();

EXIT:
	return status;
}

/**
 * Exit function for the keypad module
 *
 * @param module Unused
 */
G_MODULE_EXPORT void g_module_unload(GModule *module);
void g_module_unload(GModule *module)
{
	(void)module;

	/* Close files */
	mce_close_file(led_current_kb0_path, &led_current_kb0_fp);
	mce_close_file(led_current_kb1_path, &led_current_kb1_fp);
	mce_close_file(led_current_kb2_path, &led_current_kb2_fp);
	mce_close_file(led_current_kb3_path, &led_current_kb3_fp);
	mce_close_file(led_current_kb4_path, &led_current_kb4_fp);
	mce_close_file(led_current_kb5_path, &led_current_kb5_fp);

	mce_close_file(led_brightness_kb0_path, &led_brightness_kb0_fp);
	mce_close_file(led_brightness_kb1_path, &led_brightness_kb1_fp);
	mce_close_file(led_brightness_kb2_path, &led_brightness_kb2_fp);
	mce_close_file(led_brightness_kb3_path, &led_brightness_kb3_fp);
	mce_close_file(led_brightness_kb4_path, &led_brightness_kb4_fp);
	mce_close_file(led_brightness_kb5_path, &led_brightness_kb5_fp);

	mce_close_file(MCE_KEYPAD_BACKLIGHT_FADETIME_SYS_PATH,
		       &n810_keypad_fadetime_fp);
	mce_close_file(MCE_KEYBOARD_BACKLIGHT_FADETIME_SYS_PATH,
		       &n810_keyboard_fadetime_fp);

	/* Free path strings */
	g_free(led_current_kb0_path);
	g_free(led_current_kb1_path);
	g_free(led_current_kb2_path);
	g_free(led_current_kb3_path);
	g_free(led_current_kb4_path);
	g_free(led_current_kb5_path);

	g_free(led_brightness_kb0_path);
	g_free(led_brightness_kb1_path);
	g_free(led_brightness_kb2_path);
	g_free(led_brightness_kb3_path);
	g_free(led_brightness_kb4_path);
	g_free(led_brightness_kb5_path);

	g_free(engine3_mode_path);
	g_free(engine3_load_path);
	g_free(engine3_leds_path);

	/* Remove triggers/filters from datapipes */
	remove_output_trigger_from_datapipe(&display_state_pipe,
					    display_state_trigger);
	remove_output_trigger_from_datapipe(&keyboard_slide_pipe,
					    keyboard_slide_trigger);
	remove_output_trigger_from_datapipe(&device_inactive_pipe,
					    device_inactive_trigger);
	remove_output_trigger_from_datapipe(&key_backlight_pipe,
					    set_backlight_brightness);
	remove_output_trigger_from_datapipe(&system_state_pipe,
					    system_state_trigger);

	/* Remove all timer sources */
	cancel_key_backlight_timeout();

	return;
}
