/* Evolution calendar listener
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_LISTENER_H
#define E_CAL_LISTENER_H

#include <bonobo/bonobo-object.h>
#include <libecal/Evolution-DataServer-Calendar.h>
#include <libecal/e-cal-types.h>

G_BEGIN_DECLS



#define E_TYPE_CAL_LISTENER            (e_cal_listener_get_type ())
#define E_CAL_LISTENER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_LISTENER, ECalListener))
#define E_CAL_LISTENER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_LISTENER,	\
				      ECalListenerClass))
#define E_IS_CAL_LISTENER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_LISTENER))
#define E_IS_CAL_LISTENER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_LISTENER))

typedef struct ECalListenerPrivate ECalListenerPrivate;

typedef struct {
	BonoboObject xobject;

	/*< private >*/
	ECalListenerPrivate *priv;
} ECalListener;

typedef struct {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Calendar_CalListener__epv epv;

	/* Signals */
	void (*read_only) (ECalListener *listener, ECalendarStatus status, gboolean read_only);
	void (*cal_address) (ECalListener *listener, ECalendarStatus status, const char *address);
	void (*alarm_address) (ECalListener *listener, ECalendarStatus status, const char *address);
	void (*ldap_attribute) (ECalListener *listener, ECalendarStatus status, const char *ldap_attribute);
	void (*static_capabilities) (ECalListener *listener, ECalendarStatus status, const char *capabilities);

	void (*open) (ECalListener *listener, ECalendarStatus status);
	void (*remove) (ECalListener *listener, ECalendarStatus status);

	void (*create_object) (ECalListener *listener, ECalendarStatus status, const char *id);
	void (*modify_object) (ECalListener *listener, ECalendarStatus status);
	void (*remove_object) (ECalListener *listener, ECalendarStatus status);

	void (*discard_alarm) (ECalListener *listener, ECalendarStatus status);

 	void (*receive_objects) (ECalListener *listener, ECalendarStatus status);
 	void (*send_objects) (ECalListener *listener, ECalendarStatus status, GList *users, const char *object);

	void (*default_object) (ECalListener *listener, ECalendarStatus status, const char *object);
	void (*object) (ECalListener *listener, ECalendarStatus status, const char *object);
	void (*object_list) (ECalListener *listener, ECalendarStatus status, GList **objects);

	void (*get_timezone) (ECalListener *listener, ECalendarStatus status, const char *object);
	void (*add_timezone) (ECalListener *listener, ECalendarStatus status, const char *tzid);
	void (*set_default_timezone) (ECalListener *listener, ECalendarStatus status, const char *tzid);

	void (*get_changes) (ECalListener *listener, ECalendarStatus status, GList *changes);
	void (*get_free_busy) (ECalListener *listener, ECalendarStatus status, GList *freebusy);
	
	void (*query) (ECalListener *listener, ECalendarStatus status, GNOME_Evolution_Calendar_CalView query);

	void (*categories_changed) (ECalListener *listener, ECalendarStatus status, GPtrArray *categories);
  void (*auth_required)      (ECalListener *listener);
	void (*backend_error) (ECalListener *listener, ECalendarStatus status, const char *message);
} ECalListenerClass;

/* Notification functions */
typedef void (* ECalListenerCalSetModeFn) (ECalListener *listener,
					  GNOME_Evolution_Calendar_CalListener_SetModeStatus status,
					  GNOME_Evolution_Calendar_CalMode mode,
					  gpointer data);

GType e_cal_listener_get_type (void);

ECalListener *e_cal_listener_construct (ECalListener *listener,
				     ECalListenerCalSetModeFn cal_set_mode_fn,
				     gpointer fn_data);

ECalListener *e_cal_listener_new (ECalListenerCalSetModeFn cal_set_mode_fn,
			       gpointer fn_data);

void e_cal_listener_stop_notification (ECalListener *listener);



G_END_DECLS

#endif
