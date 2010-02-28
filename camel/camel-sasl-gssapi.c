/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#ifdef HAVE_KRB5
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <krb5/krb5.h>
#ifdef HAVE_ET_COM_ERR_H
#include <et/com_err.h>
#else
#include <com_err.h>
#endif
#ifdef HAVE_MIT_KRB5
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_generic.h>
#endif
#ifdef HAVE_HEIMDAL_KRB5
#include <gssapi.h>
#else
#ifdef  HAVE_SUN_KRB5
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_ext.h>
extern gss_OID gss_nt_service_name;
#endif
#endif

#ifndef GSS_C_OID_KRBV5_DES
#define GSS_C_OID_KRBV5_DES GSS_C_NO_OID
#endif

#include <glib/gi18n-lib.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "camel-net-utils.h"
#include "camel-sasl-gssapi.h"
#include "camel-session.h"

#define DBUS_PATH		"/org/gnome/KrbAuthDialog"
#define DBUS_INTERFACE		"org.gnome.KrbAuthDialog"
#define DBUS_METHOD		"org.gnome.KrbAuthDialog.acquireTgt"

#define CAMEL_SASL_GSSAPI_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SASL_GSSAPI, CamelSaslGssapiPrivate))

CamelServiceAuthType camel_sasl_gssapi_authtype = {
	N_("GSSAPI"),

	N_("This option will connect to the server using "
	   "Kerberos 5 authentication."),

	"GSSAPI",
	FALSE
};

enum {
	GSSAPI_STATE_INIT,
	GSSAPI_STATE_CONTINUE_NEEDED,
	GSSAPI_STATE_COMPLETE,
	GSSAPI_STATE_AUTHENTICATED
};

#define GSSAPI_SECURITY_LAYER_NONE       (1 << 0)
#define GSSAPI_SECURITY_LAYER_INTEGRITY  (1 << 1)
#define GSSAPI_SECURITY_LAYER_PRIVACY    (1 << 2)

#define DESIRED_SECURITY_LAYER  GSSAPI_SECURITY_LAYER_NONE

struct _CamelSaslGssapiPrivate {
	gint state;
	gss_ctx_id_t ctx;
	gss_name_t target;
};

static gpointer parent_class;

static void
gssapi_set_exception (OM_uint32 major,
                      OM_uint32 minor,
                      GError **error)
{
	const gchar *str;

	switch (major) {
	case GSS_S_BAD_MECH:
		str = _("The specified mechanism is not supported by the "
			"provided credential, or is unrecognized by the "
			"implementation.");
		break;
	case GSS_S_BAD_NAME:
		str = _("The provided target_name parameter was ill-formed.");
		break;
	case GSS_S_BAD_NAMETYPE:
		str = _("The provided target_name parameter contained an "
			"invalid or unsupported type of name.");
		break;
	case GSS_S_BAD_BINDINGS:
		str = _("The input_token contains different channel "
			"bindings to those specified via the "
			"input_chan_bindings parameter.");
		break;
	case GSS_S_BAD_SIG:
		str = _("The input_token contains an invalid signature, or a "
			"signature that could not be verified.");
		break;
	case GSS_S_NO_CRED:
		str = _("The supplied credentials were not valid for context "
			"initiation, or the credential handle did not "
			"reference any credentials.");
		break;
	case GSS_S_NO_CONTEXT:
		str = _("The supplied context handle did not refer to a valid context.");
		break;
	case GSS_S_DEFECTIVE_TOKEN:
		str = _("The consistency checks performed on the input_token failed.");
		break;
	case GSS_S_DEFECTIVE_CREDENTIAL:
		str = _("The consistency checks performed on the credential failed.");
		break;
	case GSS_S_CREDENTIALS_EXPIRED:
		str = _("The referenced credentials have expired.");
		break;
	case GSS_S_FAILURE:
		str = error_message (minor);
		break;
	default:
		str = _("Bad authentication response from server.");
	}

	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
		"%s", str);
}

static void
sasl_gssapi_finalize (GObject *object)
{
	CamelSaslGssapi *sasl = CAMEL_SASL_GSSAPI (object);
	guint32 status;

	if (sasl->priv->ctx != GSS_C_NO_CONTEXT)
		gss_delete_sec_context (
			&status, &sasl->priv->ctx, GSS_C_NO_BUFFER);

	if (sasl->priv->target != GSS_C_NO_NAME)
		gss_release_name (&status, &sasl->priv->target);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* DBUS Specific code */

static gboolean
send_dbus_message (gchar *name)
{
	DBusMessage *message, *reply;
	DBusError dbus_error;
	gint success = FALSE;
	DBusConnection *bus = NULL;

	dbus_error_init (&dbus_error);
	if (!(bus = dbus_bus_get (DBUS_BUS_SESSION, &dbus_error))) {
		g_warning ("could not get system bus: %s\n", dbus_error.message);
		dbus_error_free (&dbus_error);
		return FALSE;
	}

	dbus_error_free (&dbus_error);

	dbus_connection_setup_with_g_main (bus, NULL);
	dbus_connection_set_exit_on_disconnect (bus, FALSE);

	/* Create a new message on the DBUS_INTERFACE */
	if (!(message = dbus_message_new_method_call (DBUS_INTERFACE, DBUS_PATH, DBUS_INTERFACE, "acquireTgt"))) {
		g_object_unref (bus);
		return FALSE;
	}
	/* Appends the data as an argument to the message */
	if (strchr(name, '\\'))
		name = strchr(name, '\\');
	dbus_message_append_args (message,
				  DBUS_TYPE_STRING, &name,
				  DBUS_TYPE_INVALID);
	dbus_error_init(&dbus_error);

	/* Sends the message: Have a 300 sec wait timeout  */
	reply = dbus_connection_send_with_reply_and_block (bus, message, 300 * 1000, &dbus_error);

	if (dbus_error_is_set(&dbus_error))
		g_warning ("%s: %s\n", dbus_error.name, dbus_error.message);
	dbus_error_free(&dbus_error);

        if (reply)
        {
                dbus_error_init(&dbus_error);
                dbus_message_get_args(reply, &dbus_error, DBUS_TYPE_BOOLEAN, &success, DBUS_TYPE_INVALID);
                dbus_error_free(&dbus_error);
                dbus_message_unref(reply);
        }

	/* Free the message */
	dbus_message_unref (message);
	dbus_connection_unref (bus);

	return success;
}

/* END DBus stuff */

static GByteArray *
sasl_gssapi_challenge (CamelSasl *sasl,
                       GByteArray *token,
                       GError **error)
{
	CamelSaslGssapiPrivate *priv;
	CamelService *service;
	OM_uint32 major, minor, flags, time;
	gss_buffer_desc inbuf, outbuf;
	GByteArray *challenge = NULL;
	gss_buffer_t input_token;
	gint conf_state;
	gss_qop_t qop;
	gss_OID mech;
	gchar *str;
	struct addrinfo *ai, hints;
	const gchar *service_name;

	priv = CAMEL_SASL_GSSAPI_GET_PRIVATE (sasl);

	service = camel_sasl_get_service (sasl);
	service_name = camel_sasl_get_service_name (sasl);

	switch (priv->state) {
	case GSSAPI_STATE_INIT:
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;
		ai = camel_getaddrinfo(service->url->host?service->url->host:"localhost", NULL, &hints, error);
		if (ai == NULL)
			return NULL;

		str = g_strdup_printf("%s@%s", service_name, ai->ai_canonname);
		camel_freeaddrinfo(ai);

		inbuf.value = str;
		inbuf.length = strlen (str);
		major = gss_import_name (&minor, &inbuf, GSS_C_NT_HOSTBASED_SERVICE, &priv->target);
		g_free (str);

		if (major != GSS_S_COMPLETE) {
			gssapi_set_exception (major, minor, error);
			return NULL;
		}

		input_token = GSS_C_NO_BUFFER;

		goto challenge;
		break;
	case GSSAPI_STATE_CONTINUE_NEEDED:
		if (token == NULL) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Bad authentication response from server."));
			return NULL;
		}

		inbuf.value = token->data;
		inbuf.length = token->len;
		input_token = &inbuf;

	challenge:
		major = gss_init_sec_context (&minor, GSS_C_NO_CREDENTIAL, &priv->ctx, priv->target,
					      GSS_C_OID_KRBV5_DES, GSS_C_MUTUAL_FLAG |
					      GSS_C_REPLAY_FLAG | GSS_C_SEQUENCE_FLAG,
					      0, GSS_C_NO_CHANNEL_BINDINGS,
					      input_token, &mech, &outbuf, &flags, &time);

		switch (major) {
		case GSS_S_COMPLETE:
			priv->state = GSSAPI_STATE_COMPLETE;
			break;
		case GSS_S_CONTINUE_NEEDED:
			priv->state = GSSAPI_STATE_CONTINUE_NEEDED;
			break;
		default:
			if (major == (OM_uint32)GSS_S_FAILURE &&
			    (minor == (OM_uint32)KRB5KRB_AP_ERR_TKT_EXPIRED ||
			     minor == (OM_uint32)KRB5KDC_ERR_NEVER_VALID)) {
				CamelService *service;

				service = camel_sasl_get_service (sasl);
				if (send_dbus_message (service->url->user))
					goto challenge;
			} else
				gssapi_set_exception (major, minor, error);
			return NULL;
		}

		challenge = g_byte_array_new ();
		g_byte_array_append (challenge, outbuf.value, outbuf.length);
#ifndef HAVE_HEIMDAL_KRB5
		gss_release_buffer (&minor, &outbuf);
#endif
		break;
	case GSSAPI_STATE_COMPLETE:
		if (token == NULL) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Bad authentication response from server."));
			return NULL;
		}

		inbuf.value = token->data;
		inbuf.length = token->len;

		major = gss_unwrap (&minor, priv->ctx, &inbuf, &outbuf, &conf_state, &qop);
		if (major != GSS_S_COMPLETE) {
			gssapi_set_exception (major, minor, error);
			return NULL;
		}

		if (outbuf.length < 4) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Bad authentication response from server."));
#ifndef HAVE_HEIMDAL_KRB5
			gss_release_buffer (&minor, &outbuf);
#endif
			return NULL;
		}

		/* check that our desired security layer is supported */
		if ((((guchar *) outbuf.value)[0] & DESIRED_SECURITY_LAYER) != DESIRED_SECURITY_LAYER) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("Unsupported security layer."));
#ifndef HAVE_HEIMDAL_KRB5
			gss_release_buffer (&minor, &outbuf);
#endif
			return NULL;
		}

		inbuf.length = 4 + strlen (service->url->user);
		inbuf.value = str = g_malloc (inbuf.length);
		memcpy (inbuf.value, outbuf.value, 4);
		str[0] = DESIRED_SECURITY_LAYER;
		memcpy (str + 4, service->url->user, inbuf.length - 4);

#ifndef HAVE_HEIMDAL_KRB5
		gss_release_buffer (&minor, &outbuf);
#endif

		major = gss_wrap (&minor, priv->ctx, FALSE, qop, &inbuf, &conf_state, &outbuf);
		if (major != GSS_S_COMPLETE) {
			gssapi_set_exception (major, minor, error);
			g_free (str);
			return NULL;
		}

		g_free (str);
		challenge = g_byte_array_new ();
		g_byte_array_append (challenge, outbuf.value, outbuf.length);

#ifndef HAVE_HEIMDAL_KRB5
		gss_release_buffer (&minor, &outbuf);
#endif

		priv->state = GSSAPI_STATE_AUTHENTICATED;

		camel_sasl_set_authenticated (sasl, TRUE);
		break;
	default:
		return NULL;
	}

	return challenge;
}

static void
sasl_gssapi_class_init (CamelSaslGssapiClass *class)
{
	GObjectClass *object_class;
	CamelSaslClass *sasl_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (CamelSaslGssapiPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = sasl_gssapi_finalize;

	sasl_class = CAMEL_SASL_CLASS (class);
	sasl_class->challenge = sasl_gssapi_challenge;
}

static void
sasl_gssapi_init (CamelSaslGssapi *sasl)
{
	sasl->priv = CAMEL_SASL_GSSAPI_GET_PRIVATE (sasl);

	sasl->priv->state = GSSAPI_STATE_INIT;
	sasl->priv->ctx = GSS_C_NO_CONTEXT;
	sasl->priv->target = GSS_C_NO_NAME;
}

GType
camel_sasl_gssapi_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = g_type_register_static_simple (
			CAMEL_TYPE_SASL,
			"CamelSaslGssapi",
			sizeof (CamelSaslGssapiClass),
			(GClassInitFunc) sasl_gssapi_class_init,
			sizeof (CamelSaslGssapi),
			(GInstanceInitFunc) sasl_gssapi_init,
			0);

	return type;
}

#endif /* HAVE_KRB5 */
