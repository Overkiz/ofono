/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <sys/socket.h>

#include <glib.h>
#include <gatchat.h>
#include <gattty.h>
#include <gatmux.h>

#define OFONO_API_SUBJECT_TO_CHANGE
#include <ofono/plugin.h>
#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/call-barring.h>
#include <ofono/call-forwarding.h>
#include <ofono/call-meter.h>
#include <ofono/call-settings.h>
#include <ofono/devinfo.h>
#include <ofono/message-waiting.h>
#include <ofono/netreg.h>
#include <ofono/phonebook.h>
#include <ofono/sim.h>
#include <ofono/gprs.h>
#include <ofono/gprs-context.h>
#include <ofono/sms.h>
#include <ofono/ussd.h>
#include <ofono/voicecall.h>

#include <drivers/atmodem/atutil.h>
#include <drivers/atmodem/vendor.h>

#define NUM_DLC 2
#define NUM_TIMEOUT 1

#define ENABLE_SIM_TIMEOUT 0

#define SETUP_DLC   0
#define GPRS_DLC  1

static char *dlc_prefixes[NUM_DLC] = { "Setup: " , "GPRS: "};

static const char *none_prefix[] = { NULL };
static const char *rsen_prefix[]= { "#RSEN:", NULL };
static const char *qss_prefix[]= { "#QSS:", NULL };

struct ge910_data {
	GAtChat * dlcs[NUM_DLC];
	struct ofono_sim *sim;
	ofono_bool_t have_sim;
	GIOChannel *device;
	GAtMux *mux;
	guint timeout_sources[NUM_TIMEOUT];
};

static void ge910_debug(const char *str, void *user_data)
{
	const char *prefix = user_data;

	ofono_info("%s%s", prefix, str);
}

static GAtChat *open_device(struct ofono_modem *modem,
				const char *key, char *debug)
{
	struct ge910_data *data = ofono_modem_get_data(modem);
	const char *device;
	GAtSyntax *syntax;
	GIOChannel *channel;
	GAtChat *chat;
	GHashTable *options;

	device = ofono_modem_get_string(modem, key);
	if (device == NULL)
		return NULL;

	DBG("%s %s", key, device);

	options = g_hash_table_new(g_str_hash, g_str_equal);
	if (options == NULL)
		return NULL;

	g_hash_table_insert(options, "Baud", "115200");

	channel = g_at_tty_open(device, options);

	g_hash_table_destroy(options);

	if (channel == NULL)
		return NULL;

	data->device = channel;
	syntax = g_at_syntax_new_gsmv1();
	chat = g_at_chat_new(channel, syntax);
	g_at_syntax_unref(syntax);
	g_io_channel_unref(channel);

	if (chat == NULL) {
		g_io_channel_unref(data->device);
		data->device = NULL;

		return NULL;
	}

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(chat, ge910_debug, debug);

	return chat;
}

static GAtChat *create_chat(GIOChannel *channel, struct ofono_modem *modem,
				char *debug)
{
	GAtSyntax *syntax;
	GAtChat *chat;

	if (channel == NULL)
		return NULL;

	syntax = g_at_syntax_new_gsmv1();
	chat = g_at_chat_new(channel, syntax);
	g_at_syntax_unref(syntax);
	g_io_channel_unref(channel);

	if (chat == NULL)
		return NULL;

	if (getenv("OFONO_AT_DEBUG"))
		g_at_chat_set_debug(chat, ge910_debug, debug);

	return chat;
}

static void shutdown_device(struct ge910_data *data)
{
	int i;

	DBG("");

	for (i = 0; i < NUM_TIMEOUT; ++i) {
		if (data->timeout_sources[i] != 0)
			g_source_remove(data->timeout_sources[i]);
	}

	for (i = 0; i < NUM_DLC; i++) {
		if (data->dlcs[i] == NULL)
			continue;

		g_at_chat_unref(data->dlcs[i]);
		data->dlcs[i] = NULL;
	}

	DBG("");

	if (data->mux) {
		g_at_mux_unref(data->mux);
		data->mux = NULL;
	}

	DBG("");

	if (data->device) {
		g_io_channel_unref(data->device);
		data->device = NULL;
	}
}


static void setup_internal_mux(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);
	int i;

	DBG("");

	data->mux = g_at_mux_new_gsm0710_basic(data->device,
						128);

	if (data->mux == NULL)
		goto error;

	if (getenv("OFONO_MUX_DEBUG"))
		g_at_mux_set_debug(data->mux, ge910_debug, "MUX: ");

	g_at_mux_set_vendor(data->mux, OFONO_VENDOR_TELIT);

	if (!g_at_mux_start(data->mux)) {
		g_at_mux_shutdown(data->mux);
		g_at_mux_unref(data->mux);
		data->mux = NULL;
		goto error;
	}

	for (i = 0; i < NUM_DLC; i++) {
		GIOChannel *channel = g_at_mux_create_channel(data->mux);

		data->dlcs[i] = create_chat(channel, modem, dlc_prefixes[i]);
		if (data->dlcs[i] == NULL) {
			ofono_error("Failed to create channel");
			goto error;
		}

		/* Disable echo */
		g_at_chat_send(data->dlcs[i], "ATE0 +CMEE=1", none_prefix, NULL, NULL, NULL);
	}

	return;

error:
	ofono_error("Internal Mux setup failed");
	shutdown_device(data);
	ofono_modem_set_powered(modem, FALSE);
}

static void ge910_qss_notify(GAtResult *result, gpointer user_data);
static void mux_setup_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct ge910_data *data = ofono_modem_get_data(modem);

	DBG("");

	if (!ok)
		goto error;

	g_at_chat_unref(data->dlcs[SETUP_DLC]);
	data->dlcs[SETUP_DLC] = NULL;

	setup_internal_mux(modem);

	/* Follow sim state */
	g_at_chat_register(data->dlcs[SETUP_DLC], "#QSS:", ge910_qss_notify,
				FALSE, modem, NULL);

	/* Enable sim state notification */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#QSS=2", none_prefix, NULL, NULL, NULL);

	return;

error:
	ofono_error("Mux setup failed");
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#REBOOT", none_prefix, NULL, NULL, NULL);
}

static void switch_sim_state_status(struct ofono_modem *modem, int status)
{
	struct ge910_data *data = ofono_modem_get_data(modem);

	DBG("%p, SIM status: %d", modem, status);

	switch (status) {
	case 0:	/* SIM not inserted */
		if (data->have_sim == TRUE) {
			ofono_sim_inserted_notify(data->sim, FALSE);
			data->have_sim = FALSE;
		}
		break;
	case 1:	/* SIM inserted */
	case 2:	/* SIM inserted and PIN unlocked */
		break;
	case 3:	/* SIM inserted, SMS and phonebook ready */
		if (data->have_sim == FALSE) {
			ofono_modem_set_powered(modem, TRUE);
			ofono_sim_inserted_notify(data->sim, TRUE);
			data->have_sim = TRUE;
		}
		break;
	default:
		ofono_warn("Unknown SIM state %d received", status);
		break;
	}
}

static void ge910_qss_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;

	int status, res;
	GAtResultIter iter;

	DBG("%p", modem);

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "#QSS:"))
		return;

	g_at_result_iter_next_number(&iter, &res);

	g_at_result_iter_next_number(&iter, &status);

	switch_sim_state_status(modem, status);
}

static void ge910_qss_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	int status;
	GAtResultIter iter;

	DBG("%p", modem);

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "#QSS:"))
		return;

	g_at_result_iter_next_number(&iter, &status);

	switch_sim_state_status(modem, status);
}

static gboolean ge910_enable_sim_det(gpointer user_data)
{
	struct ofono_modem *modem = user_data;
	struct ge910_data *data = ofono_modem_get_data(modem);

	DBG("%p, enable sim det.", modem);

	/* Standard sim detection */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#SIMDET=1", none_prefix, NULL, NULL, NULL);

	/* Clear timeout source */
	data->timeout_sources[ENABLE_SIM_TIMEOUT] = 0;

	return FALSE;
}

static int ge910_enable(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_info("ge910_enable");

	data->dlcs[SETUP_DLC] = open_device(modem, "Modem", "Modem: ");
	if (data->dlcs[SETUP_DLC] == NULL) {
		ofono_error("ge910_enable: failed to open device.");
		return -EIO;
	}

	if (getenv("OFONO_TELIT_STATUS"))
		g_at_chat_send(data->dlcs[SETUP_DLC], "AT#SLED=2", none_prefix, NULL, NULL, NULL);

	/*
	 * Disable command echo and
	 * enable the Extended Error Result Codes
	 */
	g_at_chat_send(data->dlcs[SETUP_DLC], "ATE0 +CMEE=1", none_prefix,
				NULL, NULL, NULL);

	/* Enable SELINT 2 commands */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#SELINT=2", none_prefix,
				NULL, NULL, NULL);

	/* Start SIM detection process */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#SIMDET=0", none_prefix, NULL, NULL, NULL);
	data->timeout_sources[ENABLE_SIM_TIMEOUT] = g_timeout_add_seconds(10, ge910_enable_sim_det, modem);

	/*
	 * Disable sim state notification so that we sure get a notification
	 * when we enable it again later and don't have to query it.
	 */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#QSS=0", none_prefix, NULL, NULL, NULL);

	g_at_chat_send(data->dlcs[SETUP_DLC], "AT+CGREG=0", none_prefix, NULL, NULL, NULL);

	/*
	 * Switch data carrier detect signal off.
	 * When the DCD is disabled the modem does not hangup anymore
	 * after the data connection.
	 */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT&C0", NULL, NULL, NULL, NULL);

	data->have_sim = FALSE;

	/* Setup the MUX so subsequent chat is funneled on MUX channel 1 */
	g_at_chat_send(data->dlcs[SETUP_DLC],
			"AT+CMUX=0,0,5,128", NULL,
			mux_setup_cb, modem, NULL);

	return -EINPROGRESS;
}

static int ge910_disable(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);
	DBG("%p", modem);

	shutdown_device(data);

	ofono_modem_set_powered(modem, FALSE);

	return -EINPROGRESS;
}

static void ge910_pre_sim(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	ofono_devinfo_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
	data->sim = ofono_sim_create(modem, OFONO_VENDOR_TELIT, "atmodem",
					data->dlcs[SETUP_DLC]);
}

static void ge910_post_sim(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);


	DBG("%p", modem);

	/*
	 * Switch data carrier detect signal off.
	 * When the DCD is disabled the modem does not hangup anymore
	 * after the data connection.
	 */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT&C0", NULL, NULL, NULL, NULL);

	/*
	 * Tell the modem not to automatically initiate auto-attach
	 * proceedures on its own.
	 */
	g_at_chat_send(data->dlcs[SETUP_DLC], "AT#AUTOATT=0", none_prefix,
				NULL, NULL, NULL);

//	ofono_phonebook_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
//	ofono_sms_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
//
//	ofono_voicecall_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);


}

static void set_online_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_modem_online_cb_t cb = cbd->cb;
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));
	cb(&error, cbd->data);
}

static void ge910_set_online(struct ofono_modem *modem, ofono_bool_t online,
				ofono_modem_online_cb_t cb, void *user_data)
{
	struct ge910_data *data = ofono_modem_get_data(modem);
	struct cb_data *cbd = cb_data_new(cb, user_data);
	char const *command = online ? "AT+CFUN=1,0" : "AT+CFUN=4,0";

	DBG("modem %p %s", modem, online ? "online" : "offline");

	g_at_chat_send(data->dlcs[SETUP_DLC], command, none_prefix, set_online_cb,
						cbd, g_free);
}

static void ge910_post_online(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);
	struct ofono_message_waiting *mw;

	struct ofono_gprs *gprs;
	struct ofono_gprs_context *gc;

	DBG("%p", modem);

	ofono_netreg_create(modem, OFONO_VENDOR_TELIT, "atmodem", data->dlcs[SETUP_DLC]);
//	ofono_ussd_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
//	ofono_call_forwarding_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
//	ofono_call_settings_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
//	ofono_call_meter_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);
//	ofono_call_barring_create(modem, 0, "atmodem", data->dlcs[SETUP_DLC]);

//	mw = ofono_message_waiting_create(modem);
//	if (mw)
//		ofono_message_waiting_register(mw);

	gprs = ofono_gprs_create(modem, OFONO_VENDOR_TELIT, "atmodem",
					data->dlcs[SETUP_DLC]);
	gc = ofono_gprs_context_create(modem, 0, "atmodem", data->dlcs[GPRS_DLC]);

	if (gprs && gc)
		ofono_gprs_add_context(gprs, gc);
}

static int ge910_probe(struct ofono_modem *modem)
{
	struct ge910_data *data;

	DBG("%p", modem);

	data = g_try_new0(struct ge910_data, 1);
	if (data == NULL)
		return -ENOMEM;

	ofono_modem_set_data(modem, data);

	int i;
	for (i = 0; i < NUM_TIMEOUT; ++i) {
		data->timeout_sources[i] = 0;
	}


	return 0;
}

static void ge910_remove(struct ofono_modem *modem)
{
	struct ge910_data *data = ofono_modem_get_data(modem);

	DBG("%p", modem);

	int i;
	for (i = 0; i < NUM_TIMEOUT; ++i) {
		if (data->timeout_sources[i] != 0)
			g_source_remove(data->timeout_sources[i]);
		}

	ofono_modem_set_data(modem, NULL);

	g_free(data);
}

static struct ofono_modem_driver ge910_driver = {
	.name		= "ge910",
	.probe		= ge910_probe,
	.remove		= ge910_remove,
	.enable		= ge910_enable,
	.disable	= ge910_disable,
	.pre_sim	= ge910_pre_sim,
	.post_sim	= ge910_post_sim,
	.post_online	= ge910_post_online,
};

static int ge910_init(void)
{
	return ofono_modem_driver_register(&ge910_driver);
}

static void ge910_exit(void)
{
	ofono_modem_driver_unregister(&ge910_driver);
}

OFONO_PLUGIN_DEFINE(ge910, "Telit GE10 driver", VERSION,
		OFONO_PLUGIN_PRIORITY_DEFAULT, ge910_init, ge910_exit)
