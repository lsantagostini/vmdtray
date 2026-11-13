/*	$OpenBSD$	*/

/*
 * Copyright (c) 2026 Leonardo Santagostini <leonardo@santagostini.com.ar>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * StatusNotifierItem over D-Bus, the tray protocol of KDE Plasma and other
 * desktops.  We export an object with the icon and the tooltip, register
 * it with the StatusNotifierWatcher, and the tray asks us for them and
 * tells us about clicks.
 */

#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include "extern.h"

#define ITEM_IFACE	"org.kde.StatusNotifierItem"
#define ITEM_PATH	"/StatusNotifierItem"
#define WATCHER_NAME	"org.kde.StatusNotifierWatcher"
#define WATCHER_PATH	"/StatusNotifierWatcher"
#define NSIZES		(sizeof(sizes) / sizeof(sizes[0]))

/* Icon sizes offered, so the tray can pick the one that fits its panel. */
static const int sizes[] = { 16, 22, 32, 48, 64 };

/* Properties of the item, in the order GetAll returns them. */
static const char *properties[] = {
	"Category", "Id", "Title", "Status", "WindowId", "IconName",
	"IconPixmap", "OverlayIconName", "OverlayIconPixmap",
	"AttentionIconName", "AttentionIconPixmap", "AttentionMovieName",
	"ToolTip", "ItemIsMenu", "Menu"
};

static const char *introspection =
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
    "<node>\n"
    " <interface name=\"" ITEM_IFACE "\">\n"
    "  <method name=\"Activate\">"
    "<arg name=\"x\" type=\"i\"/><arg name=\"y\" type=\"i\"/></method>\n"
    "  <method name=\"SecondaryActivate\">"
    "<arg name=\"x\" type=\"i\"/><arg name=\"y\" type=\"i\"/></method>\n"
    "  <method name=\"ContextMenu\">"
    "<arg name=\"x\" type=\"i\"/><arg name=\"y\" type=\"i\"/></method>\n"
    "  <method name=\"Scroll\">"
    "<arg name=\"delta\" type=\"i\"/>"
    "<arg name=\"orientation\" type=\"s\"/></method>\n"
    "  <signal name=\"NewIcon\"/>\n"
    "  <signal name=\"NewToolTip\"/>\n"
    "  <property name=\"Category\" type=\"s\" access=\"read\"/>\n"
    "  <property name=\"Id\" type=\"s\" access=\"read\"/>\n"
    "  <property name=\"Title\" type=\"s\" access=\"read\"/>\n"
    "  <property name=\"Status\" type=\"s\" access=\"read\"/>\n"
    "  <property name=\"WindowId\" type=\"i\" access=\"read\"/>\n"
    "  <property name=\"IconName\" type=\"s\" access=\"read\"/>\n"
    "  <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>\n"
    "  <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>\n"
    "  <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>\n"
    "  <property name=\"Menu\" type=\"o\" access=\"read\"/>\n"
    " </interface>\n"
    " <interface name=\"" DBUS_INTERFACE_PROPERTIES "\">\n"
    "  <method name=\"Get\"><arg name=\"interface\" type=\"s\"/>"
    "<arg name=\"name\" type=\"s\"/>"
    "<arg name=\"value\" type=\"v\" direction=\"out\"/></method>\n"
    "  <method name=\"GetAll\"><arg name=\"interface\" type=\"s\"/>"
    "<arg name=\"values\" type=\"a{sv}\" direction=\"out\"/></method>\n"
    " </interface>\n"
    " <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"
    "  <method name=\"Introspect\">"
    "<arg name=\"xml\" type=\"s\" direction=\"out\"/></method>\n"
    " </interface>\n"
    "</node>\n";

static DBusConnection	*conn;
static unsigned char	*pixmaps[NSIZES];
static char		 busname[64];
static char		 tooltip[128];
static int		 lamp = -1;

static void		 append(DBusMessageIter *, int, const void *);
static void		 appendpixmaps(DBusMessageIter *, int);
static int		 appendproperty(DBusMessageIter *, const char *);
static void		 appendstring(DBusMessageIter *, const char *);
static void		 closec(DBusMessageIter *, DBusMessageIter *);
static void		 emit(const char *);
static DBusHandlerResult filter(DBusConnection *, DBusMessage *, void *);
static DBusHandlerResult message(DBusConnection *, DBusMessage *, void *);
static void		 openc(DBusMessageIter *, int, const char *,
			    DBusMessageIter *);
static void		 registeritem(void);
static void		 sni_dispatch(void);
static int		 sni_fd(void);
static void		 sni_update(int, const char *);

/*
 * Use StatusNotifierItem if there is a session bus with a watcher on it.
 * Own the item name, export the object and register it with the watcher,
 * then fill in t and return 0.  Return -1 if there is no such tray.
 */
int
sni_init(struct tray *t)
{
	static const DBusObjectPathVTable vtable = { NULL, message };
	DBusError e;
	size_t i;

	if (getenv("DBUS_SESSION_BUS_ADDRESS") == NULL)
		return -1;
	dbus_error_init(&e);
	if ((conn = dbus_bus_get(DBUS_BUS_SESSION, &e)) == NULL) {
		dbus_error_free(&e);
		return -1;
	}
	if (!dbus_bus_name_has_owner(conn, WATCHER_NAME, &e)) {
		dbus_error_free(&e);
		dbus_connection_unref(conn);
		conn = NULL;
		return -1;
	}

	(void)snprintf(busname, sizeof(busname),
	    "org.kde.StatusNotifierItem-%ld-1", (long)getpid());
	if (dbus_bus_request_name(conn, busname, DBUS_NAME_FLAG_DO_NOT_QUEUE,
	    &e) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
		errx(1, "cannot own %s%s%s", busname,
		    dbus_error_is_set(&e) ? ": " : "",
		    dbus_error_is_set(&e) ? e.message : "");
	if (!dbus_connection_register_object_path(conn, ITEM_PATH, &vtable,
	    NULL))
		errx(1, "out of memory");

	/* Register again whenever a new watcher shows up. */
	dbus_bus_add_match(conn, "type='signal',"
	    "sender='" DBUS_SERVICE_DBUS "',"
	    "interface='" DBUS_INTERFACE_DBUS "',"
	    "member='NameOwnerChanged',"
	    "arg0='" WATCHER_NAME "'", &e);
	if (dbus_error_is_set(&e))
		errx(1, "%s", e.message);
	if (!dbus_connection_add_filter(conn, filter, NULL, NULL))
		errx(1, "out of memory");

	for (i = 0; i < NSIZES; i++)
		if ((pixmaps[i] = calloc(sizes[i] * sizes[i], 4)) == NULL)
			err(1, NULL);
	registeritem();

	t->fd = sni_fd;
	t->dispatch = sni_dispatch;
	t->update = sni_update;
	return 0;
}

static int
sni_fd(void)
{
	int fd;

	if (!dbus_connection_get_unix_fd(conn, &fd))
		errx(1, "no session bus socket");
	return fd;
}

/* Read what the bus sent us and handle every message in the queue. */
static void
sni_dispatch(void)
{
	if (!dbus_connection_read_write(conn, 0))
		errx(1, "lost the session bus");
	while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS)
		continue;
	dbus_connection_flush(conn);
}

/*
 * Light the given lamp and put the description in the tooltip.  Tell the
 * tray only when something changed.
 */
static void
sni_update(int newlamp, const char *text)
{
	size_t i;

	if (newlamp != lamp) {
		lamp = newlamp;
		for (i = 0; i < NSIZES; i++)
			iconrender(pixmaps[i], sizes[i], lamp);
		emit("NewIcon");
	}
	if (strcmp(text, tooltip) != 0) {
		(void)strlcpy(tooltip, text, sizeof(tooltip));
		emit("NewToolTip");
	}
	dbus_connection_flush(conn);
}

/* Tell the watcher, and through it the tray, about our item. */
static void
registeritem(void)
{
	DBusMessage *msg;
	const char *name;

	name = busname;
	if ((msg = dbus_message_new_method_call(WATCHER_NAME, WATCHER_PATH,
	    WATCHER_NAME, "RegisterStatusNotifierItem")) == NULL ||
	    !dbus_message_append_args(msg, DBUS_TYPE_STRING, &name,
	    DBUS_TYPE_INVALID))
		errx(1, "out of memory");
	dbus_message_set_no_reply(msg, TRUE);
	if (!dbus_connection_send(conn, msg, NULL))
		errx(1, "out of memory");
	dbus_message_unref(msg);
	dbus_connection_flush(conn);
}

/* Send one of the argument-less signals of the item. */
static void
emit(const char *member)
{
	DBusMessage *msg;

	if ((msg = dbus_message_new_signal(ITEM_PATH, ITEM_IFACE,
	    member)) == NULL || !dbus_connection_send(conn, msg, NULL))
		errx(1, "out of memory");
	dbus_message_unref(msg);
}

/* Watch for a new StatusNotifierWatcher, as when the tray restarts. */
static DBusHandlerResult
filter(DBusConnection *c, DBusMessage *msg, void *arg)
{
	const char *name, *newowner, *oldowner;

	if (dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS,
	    "NameOwnerChanged") &&
	    dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
	    DBUS_TYPE_STRING, &oldowner, DBUS_TYPE_STRING, &newowner,
	    DBUS_TYPE_INVALID) &&
	    strcmp(name, WATCHER_NAME) == 0 && *newowner != '\0')
		registeritem();
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/*
 * Answer the method calls on our object: the properties, introspection,
 * and the clicks, of which only the primary one does something.
 */
static DBusHandlerResult
message(DBusConnection *c, DBusMessage *msg, void *arg)
{
	DBusMessageIter it, dict, entry;
	DBusMessage *reply;
	const char *iface, *name;
	size_t i;

	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if ((reply = dbus_message_new_method_return(msg)) == NULL)
		errx(1, "out of memory");
	dbus_message_iter_init_append(reply, &it);

	if (dbus_message_is_method_call(msg, DBUS_INTERFACE_INTROSPECTABLE,
	    "Introspect"))
		append(&it, DBUS_TYPE_STRING, &introspection);
	else if (dbus_message_is_method_call(msg, DBUS_INTERFACE_PROPERTIES,
	    "Get")) {
		if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
		    DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID) ||
		    appendproperty(&it, name) == -1) {
			dbus_message_unref(reply);
			if ((reply = dbus_message_new_error(msg,
			    DBUS_ERROR_UNKNOWN_PROPERTY, "no such property")) ==
			    NULL)
				errx(1, "out of memory");
		}
	} else if (dbus_message_is_method_call(msg, DBUS_INTERFACE_PROPERTIES,
	    "GetAll")) {
		openc(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
		for (i = 0; i < sizeof(properties) / sizeof(properties[0]);
		    i++) {
			openc(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
			append(&entry, DBUS_TYPE_STRING, &properties[i]);
			(void)appendproperty(&entry, properties[i]);
			closec(&dict, &entry);
		}
		closec(&it, &dict);
	} else if (dbus_message_is_method_call(msg, ITEM_IFACE, "Activate"))
		launch();
	else if (!dbus_message_is_method_call(msg, ITEM_IFACE,
	    "SecondaryActivate") &&
	    !dbus_message_is_method_call(msg, ITEM_IFACE, "ContextMenu") &&
	    !dbus_message_is_method_call(msg, ITEM_IFACE, "Scroll")) {
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!dbus_connection_send(conn, reply, NULL))
		errx(1, "out of memory");
	dbus_message_unref(reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Append the value of a property as a variant.  Return -1 if there is no
 * property with that name.
 */
static int
appendproperty(DBusMessageIter *it, const char *name)
{
	DBusMessageIter tip, v;
	dbus_int32_t zero;
	dbus_bool_t no;
	const char *s;

	zero = 0;
	no = FALSE;
	if (strcmp(name, "Category") == 0)
		appendstring(it, "SystemServices");
	else if (strcmp(name, "Id") == 0)
		appendstring(it, "vmdtray");
	else if (strcmp(name, "Title") == 0)
		appendstring(it, "vmd");
	else if (strcmp(name, "Status") == 0)
		appendstring(it, "Active");
	else if (strcmp(name, "IconName") == 0 ||
	    strcmp(name, "OverlayIconName") == 0 ||
	    strcmp(name, "AttentionIconName") == 0 ||
	    strcmp(name, "AttentionMovieName") == 0)
		appendstring(it, "");
	else if (strcmp(name, "WindowId") == 0) {
		openc(it, DBUS_TYPE_VARIANT, "i", &v);
		append(&v, DBUS_TYPE_INT32, &zero);
		closec(it, &v);
	} else if (strcmp(name, "IconPixmap") == 0) {
		openc(it, DBUS_TYPE_VARIANT, "a(iiay)", &v);
		appendpixmaps(&v, 1);
		closec(it, &v);
	} else if (strcmp(name, "OverlayIconPixmap") == 0 ||
	    strcmp(name, "AttentionIconPixmap") == 0) {
		openc(it, DBUS_TYPE_VARIANT, "a(iiay)", &v);
		appendpixmaps(&v, 0);
		closec(it, &v);
	} else if (strcmp(name, "ToolTip") == 0) {
		openc(it, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &v);
		openc(&v, DBUS_TYPE_STRUCT, NULL, &tip);
		s = "";
		append(&tip, DBUS_TYPE_STRING, &s);
		appendpixmaps(&tip, 0);
		s = "vmd";
		append(&tip, DBUS_TYPE_STRING, &s);
		s = tooltip;
		append(&tip, DBUS_TYPE_STRING, &s);
		closec(&v, &tip);
		closec(it, &v);
	} else if (strcmp(name, "ItemIsMenu") == 0) {
		openc(it, DBUS_TYPE_VARIANT, "b", &v);
		append(&v, DBUS_TYPE_BOOLEAN, &no);
		closec(it, &v);
	} else if (strcmp(name, "Menu") == 0) {
		openc(it, DBUS_TYPE_VARIANT, "o", &v);
		s = "/NO_DBUSMENU";
		append(&v, DBUS_TYPE_OBJECT_PATH, &s);
		closec(it, &v);
	} else
		return -1;
	return 0;
}

/*
 * Append an array of icons, each a (width, height, ARGB32 bytes) struct:
 * every size of the traffic light, or none.
 */
static void
appendpixmaps(DBusMessageIter *it, int all)
{
	DBusMessageIter array, bytes, icon;
	const unsigned char *p;
	dbus_int32_t size;
	size_t i;

	openc(it, DBUS_TYPE_ARRAY, "(iiay)", &array);
	for (i = 0; all && lamp != -1 && i < NSIZES; i++) {
		size = sizes[i];
		p = pixmaps[i];
		openc(&array, DBUS_TYPE_STRUCT, NULL, &icon);
		append(&icon, DBUS_TYPE_INT32, &size);
		append(&icon, DBUS_TYPE_INT32, &size);
		openc(&icon, DBUS_TYPE_ARRAY, "y", &bytes);
		if (!dbus_message_iter_append_fixed_array(&bytes,
		    DBUS_TYPE_BYTE, &p, size * size * 4))
			errx(1, "out of memory");
		closec(&icon, &bytes);
		closec(&array, &icon);
	}
	closec(it, &array);
}

static void
appendstring(DBusMessageIter *it, const char *s)
{
	DBusMessageIter v;

	openc(it, DBUS_TYPE_VARIANT, "s", &v);
	append(&v, DBUS_TYPE_STRING, &s);
	closec(it, &v);
}

static void
append(DBusMessageIter *it, int type, const void *value)
{
	if (!dbus_message_iter_append_basic(it, type, value))
		errx(1, "out of memory");
}

static void
openc(DBusMessageIter *it, int type, const char *signature,
    DBusMessageIter *sub)
{
	if (!dbus_message_iter_open_container(it, type, signature, sub))
		errx(1, "out of memory");
}

static void
closec(DBusMessageIter *it, DBusMessageIter *sub)
{
	if (!dbus_message_iter_close_container(it, sub))
		errx(1, "out of memory");
}
