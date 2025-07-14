/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 * Copyright 2025 Lukas Voegl <lukas@voegl.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-qcdm-device-struct.h"
#include "fu-mm-qcdm-device.h"

G_DEFINE_TYPE(FuMmQcdmDevice, fu_mm_qcdm_device, FU_TYPE_MM_DEVICE)

#define FU_MM_QCDM_DEVICE_ESCAPE_MASK 0x20

static void
fu_mm_qcdm_device_escape_data(guint8 *buf, gsize bufsz, GByteArray *out)
{
	for (gsize i = 0; i < bufsz; i++) {
		guint8 tmp = buf[i];

		if (tmp == FU_MM_QCDM_DEVICE_SPECIAL_CHAR_CONTROL ||
		    tmp == FU_MM_QCDM_DEVICE_SPECIAL_CHAR_ESCAPE) {
			/* special characters need to be escaped */
			guint8 escaped[2] = {FU_MM_QCDM_DEVICE_SPECIAL_CHAR_CONTROL,
					     tmp ^ FU_MM_QCDM_DEVICE_ESCAPE_MASK};

			g_byte_array_append(out, escaped, 2);
		} else {
			g_byte_array_append(out, &tmp, 1);
		}
	}
}

static GBytes *
fu_mm_qcdm_device_encode_data(guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(GByteArray) out = g_byte_array_new();
	guint8 control_char = FU_MM_QCDM_DEVICE_SPECIAL_CHAR_CONTROL;

	fu_mm_qcdm_device_escape_data(buf, bufsz, out);
	/* packet is guarded by control characters at the beginning and end */
	g_byte_array_prepend(out, &control_char, 1);
	g_byte_array_append(out, &control_char, 1);

	return g_bytes_new(out->data, out->len);
}

static GByteArray *
fu_mm_qcdm_device_unescape_data(GByteArray *buf, GError **error)
{
	g_autoptr(GByteArray) out = g_byte_array_new();

	for (gsize i = 0; i < buf->len; i++) {
		guint8 tmp = buf->data[i];

		if (tmp == FU_MM_QCDM_DEVICE_SPECIAL_CHAR_ESCAPE) {
			/* next byte is escaped */
			guint8 escaped;

			if (i == buf->len - 1) {
				/* next byte does not exist */
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "no escaped character");
				return NULL;
			}

			escaped = buf->data[++i];
			tmp = escaped ^ FU_MM_QCDM_DEVICE_ESCAPE_MASK;
		}

		g_byte_array_append(out, &tmp, 1);
	}

	return g_steal_pointer(&out);
}

static GBytes *
fu_mm_qcdm_device_decode_data(GBytes *res, GError **error)
{
	g_autoptr(GByteArray) res_array = g_byte_array_new();
	g_autoptr(GByteArray) res_decoded_array = NULL;

	g_byte_array_append(res_array, g_bytes_get_data(res, NULL), g_bytes_get_size(res));

	/* remove control char at the end */
	if (res_array->len > 0 &&
	    res_array->data[res_array->len - 1] == FU_MM_QCDM_DEVICE_SPECIAL_CHAR_CONTROL)
		g_byte_array_remove_index(res_array, res_array->len - 1);
	/* remove control char at the beginning */
	if (res_array->len > 0 && res_array->data[0] == FU_MM_QCDM_DEVICE_SPECIAL_CHAR_CONTROL)
		g_byte_array_remove_index(res_array, 0);

	res_decoded_array = fu_mm_qcdm_device_unescape_data(res_array, error);
	if (res_decoded_array == NULL) {
		g_prefix_error(error, "failed to decode qcdm response: ");
		return NULL;
	}

	return g_bytes_new(res_decoded_array->data, res_decoded_array->len);
}

static GBytes *
fu_mm_qcdm_device_cmd(FuMmQcdmDevice *self, guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(GBytes) qcdm_req = NULL;
	g_autoptr(GBytes) qcdm_res = NULL;

	qcdm_req = fu_mm_qcdm_device_encode_data(buf, bufsz, error);
	if (qcdm_req == NULL) {
		g_prefix_error(error, "failed to encode qcdm request: ");
		return NULL;
	}

	fu_dump_bytes(G_LOG_DOMAIN, "writing", qcdm_req);
	if (!fu_udev_device_write_bytes(FU_UDEV_DEVICE(self),
					qcdm_req,
					1500,
					FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
					error)) {
		g_prefix_error(error, "failed to write qcdm request: ");
		return NULL;
	}

	/* response */
	qcdm_res = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
					     -1,
					     1500,
					     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					     error);
	if (qcdm_res == NULL) {
		g_prefix_error(error, "failed to read qcdm response: ");
		return NULL;
	}
	fu_dump_bytes(G_LOG_DOMAIN, "read", qcdm_res);

	return fu_mm_qcdm_device_decode_data(qcdm_res, error);
}

static gboolean
fu_mm_qcdm_device_switch_to_edl_cb(FuDevice *device, gpointer userdata, GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	g_autoptr(FuMmQcdmDeviceBasePacket) pkt_req = NULL;
	g_autoptr(FuMmQcdmDevicePacketData) req_data = NULL;
	g_autoptr(FuMmQcdmDeviceBasePacket) pkt_res = NULL;
	g_autoptr(GBytes) pkt_req_bytes = NULL;
	g_autoptr(GBytes) pkt_res_bytes = NULL;
	g_autoptr(GBytes) res = NULL;
	g_autoptr(GError) inner_error = NULL;

	/* initialize qcdm frame */
	req_data = fu_mm_qcdm_device_packet_data_new();
	fu_mm_qcdm_device_packet_data_set_command(req_data, FU_MM_QCDM_DEVICE_COMMAND_SUBSYSTEM);
	fu_mm_qcdm_device_packet_data_set_subsystem(req_data,
						    FU_MM_QCDM_DEVICE_SUBSYSTEM_OPERATIONS);
	fu_mm_qcdm_device_packet_data_set_subsystem_command(
	    req_data,
	    FU_MM_QCDM_DEVICE_SUBSYSTEM_COMMAND_REBOOT_EDL);

	/* initialize qcdm packet data with frame and crc */
	pkt_req = fu_mm_qcdm_device_base_packet_new();
	fu_mm_qcdm_device_base_packet_set_crc(
	    pkt_req,
	    fu_crc16(FU_CRC_KIND_B16_ISO_HDLC, req_data->data, req_data->len));
	if (!fu_mm_qcdm_device_base_packet_set_data(pkt_req, req_data, error))
		return FALSE;

	res = fu_mm_qcdm_device_cmd(self, pkt_req->data, pkt_req->len, &inner_error);
	if (res == NULL) {
		/* when the QCDM port does not exist anymore, we are detached */
		if (!g_file_test(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)),
				 G_FILE_TEST_EXISTS))
			return TRUE;

		/* command has failed without detaching */
		g_propagate_error(error, g_steal_pointer(&inner_error));
		return FALSE;
	}

	pkt_res = fu_mm_qcdm_device_base_packet_parse(g_bytes_get_data(res, NULL),
						      g_bytes_get_size(res),
						      0x0,
						      error);
	if (pkt_res == NULL) {
		g_prefix_error(error, "could not parse qcdm reboot response: ");
		return FALSE;
	}

	/* expect response packet data to match request packet data */
	pkt_req_bytes = g_bytes_new(pkt_req->data, pkt_req->len);
	pkt_res_bytes = g_bytes_new(pkt_res->data, pkt_res->len);
	if (g_bytes_compare(pkt_req_bytes, pkt_res_bytes)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to read valid qcdm response");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qcdm_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* retry up to 30 times until the QCDM port goes away */
	if (!fu_device_retry_full(device,
				  fu_mm_qcdm_device_switch_to_edl_cb,
				  30,
				  1000,
				  NULL,
				  error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_qcdm_device_probe(FuDevice *device, GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_QCDM, error);
}

static gboolean
fu_mm_qcdm_device_prepare(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_qcdm_device_cleanup(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_qcdm_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mm_qcdm_device_init(FuMmQcdmDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_protocol(FU_UDEV_DEVICE(self), "com.qualcomm.firehose");
}

static void
fu_mm_qcdm_device_class_init(FuMmQcdmDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_mm_qcdm_device_probe;
	device_class->detach = fu_mm_qcdm_device_detach;
	device_class->prepare = fu_mm_qcdm_device_prepare;
	device_class->cleanup = fu_mm_qcdm_device_cleanup;
	device_class->set_progress = fu_mm_qcdm_device_set_progress;
}
