#include "xutil.h"

/**************************************************************************
  X error handlers
**************************************************************************/

static int X_error_handler(Display *dpy, XErrorEvent *error)
{
	char buf[1024];
	if (error->error_code == BadWindow)
		return 0;
	XGetErrorText(dpy, error->error_code, buf, sizeof(buf));
	XWARNING("X error: %s (resource id: %d)", buf, error->resourceid);
	return 0;
}

static int X_io_error_handler(Display *dpy)
{
	XWARNING("fatal IO error, connection to X server lost? exiting...");
	return 0;
}

static char *atom_names[] = {
	"WM_STATE",
	"_NET_DESKTOP_NAMES",
	"_NET_WM_STATE",
	"_NET_ACTIVE_WINDOW",
	"_NET_CLOSE_WINDOW",
	"_NET_WM_NAME",
	"_NET_WM_NAME",
	"_NET_WM_VISIBLE_ICON_NAME",
	"_NET_WORKAREA",
	"_NET_WM_ICON",
	"_NET_WM_ICON_GEOMETRY",
	"_NET_WM_VISIBLE_NAME",
	"_NET_WM_STATE_SKIP_TASKBAR",
	"_NET_WM_STATE_SHADED",
	"_NET_WM_STATE_HIDDEN",
	"_NET_WM_STATE_DEMANDS_ATTENTION",
	"_NET_WM_DESKTOP",
	"_NET_MOVERESIZE_WINDOW",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DOCK",
	"_NET_WM_WINDOW_TYPE_DESKTOP",
	"_NET_WM_STRUT",
	"_NET_WM_STRUT_PARTIAL",
	"_NET_CLIENT_LIST",
	"_NET_CLIENT_LIST_STACKING",
	"_NET_NUMBER_OF_DESKTOPS",
	"_NET_CURRENT_DESKTOP",
	"_NET_FRAME_EXTENTS",
	"_NET_SYSTEM_TRAY_OPCODE",
	"_NET_SHOWING_DESKTOP",
	"UTF8_STRING",
	"_MOTIF_WM_HINTS",
	"_XROOTPMAP_ID",
	"ESETROOT_PMAP_ID",
	"XdndAware",
	"XdndPosition",
	"XdndStatus"
};

void *x_get_prop_data(struct x_connection *c, Window win, Atom prop,
		      Atom type, int *items)
{
	Atom type_ret;
	int format_ret;
	unsigned long items_ret;
	unsigned long after_ret;
	unsigned char *prop_data;

	prop_data = 0;

	XGetWindowProperty(c->dpy, win, prop, 0, 0x7fffffff, False,
			type, &type_ret, &format_ret, &items_ret,
			&after_ret, &prop_data);
	if (items)
		*items = items_ret;
	if (type != type_ret) {
		if (prop_data)
			XFree(prop_data);
		return 0;
	}

	return prop_data;
}

int x_get_prop_int(struct x_connection *c, Window win, Atom at)
{
	int num = 0;
	long *data;

	data = x_get_prop_data(c, win, at, XA_CARDINAL, 0);
	if (data) {
		num = *data;
		XFree(data);
	}
	return num;
}

Window x_get_prop_window(struct x_connection *c, Window win, Atom at)
{
	Window num = 0;
	Window *data;

	data = x_get_prop_data(c, win, at, XA_WINDOW, 0);
	if (data) {
		num = *data;
		XFree(data);
	}
	return num;
}

Pixmap x_get_prop_pixmap(struct x_connection *c, Window win, Atom at)
{
	Pixmap num = None;
	Pixmap *data;

	data = x_get_prop_data(c, win, at, XA_PIXMAP, 0);
	if (data) {
		num = *data;
		XFree(data);
	}
	return num;
}

int x_get_window_desktop(struct x_connection *c, Window win)
{
	return x_get_prop_int(c, win, c->atoms[XATOM_NET_WM_DESKTOP]);
}

/**************************************************************************
  multiheads setup
**************************************************************************/

static int init_xrandr(struct x_connection *c)
{
#ifdef HAVE_XRANDR
	XRRScreenResources *resources;
	struct x_monitor *monitors;
	int i, monitors_n = 0;

	resources = XRRGetScreenResourcesCurrent(c->dpy, c->root);
	if (!resources)
		return 0;

	if (!resources->noutput) {
		XRRFreeScreenResources(resources);
		return 0;
	}

	monitors = xmallocz(sizeof(struct x_monitor) * resources->noutput);
	for (i = 0; i < resources->noutput; ++i) {
		XRROutputInfo *output = XRRGetOutputInfo(c->dpy,
							 resources,
							 resources->outputs[i]);

		if (output->connection == RR_Disconnected)
		        continue;

		if (output->crtc) {
			XRRCrtcInfo *crtc = XRRGetCrtcInfo(c->dpy, resources, output->crtc);
			monitors[monitors_n].x = crtc->x;
			monitors[monitors_n].y = crtc->y;
			monitors[monitors_n].width = crtc->width;
			monitors[monitors_n].height = crtc->height;
			XRRFreeCrtcInfo (crtc);
			monitors_n++;
		}

		XRRFreeOutputInfo (output);
	}
	XRRFreeScreenResources(resources);

	if (!monitors_n) {
		xfree(monitors);
		return 0;
	}
	c->monitors = monitors;
	c->monitors_n = monitors_n;

	return 1;
#else
	return 0;
#endif
}

static int init_xinerama(struct x_connection *c)
{
#ifdef HAVE_XINERAMA
	XineramaScreenInfo *xmonitors;
	struct x_monitor *monitors;
	int i, monitors_n;

	if (!XineramaIsActive(c->dpy))
		return 0;

	xmonitors = XineramaQueryScreens(c->dpy, &monitors_n);
	if (!xmonitors)
		return 0;

	if (monitors_n <= 0) {
		XFree(xmonitors);
		return 0;
	}

	monitors = xmallocz(sizeof(struct x_monitor) * monitors_n);
	for (i = 0; i < monitors_n; ++i) {
		monitors[i].x = xmonitors[i].x_org;
		monitors[i].y = xmonitors[i].y_org;
		monitors[i].width = xmonitors[i].width;
		monitors[i].height = xmonitors[i].height;
	}
	XFree(xmonitors);

	c->monitors = monitors;
	c->monitors_n = monitors_n;
	return 1;
#else
	return 0;
#endif
}

static void init_monitors(struct x_connection *c)
{
	int opcode, event, error;
	if (XQueryExtension(c->dpy, "XINERAMA", &opcode, &event, &error)) {
		if (init_xinerama(c))
			return;
	}

	if (init_xrandr(c))
		return;

	c->monitors = xmallocz(sizeof(struct x_monitor));
	c->monitors_n = 1;
	*c->monitors = (struct x_monitor){0,0,c->screen_width,c->screen_height};
}

/**************************************************************************
  *the* interface
**************************************************************************/

void x_connect(struct x_connection *c, const char *display)
{
	CLEAR_STRUCT(c);
	c->dpy = XOpenDisplay(display);
	if (!c->dpy)
		XDIE("Failed to connect to X server");

#ifndef NDEBUG
	//XSynchronize(c->dpy, True);
#endif
	XSetErrorHandler(X_error_handler);
	XSetIOErrorHandler(X_io_error_handler);

	/* get internal atoms */
	XInternAtoms(c->dpy, atom_names, XATOM_COUNT, False, c->atoms);

	c->screen		= DefaultScreen(c->dpy);
	c->screen_width		= DisplayWidth(c->dpy, c->screen);
	c->screen_height	= DisplayHeight(c->dpy, c->screen);
	c->default_visual	= DefaultVisual(c->dpy, c->screen);
	c->default_colormap	= DefaultColormap(c->dpy, c->screen);
	c->default_depth	= DefaultDepth(c->dpy, c->screen);
	c->root			= RootWindow(c->dpy, c->screen);
	x_update_root_pmap(c);

	XSelectInput(c->dpy, c->root, PropertyChangeMask | StructureNotifyMask);

	init_monitors(c);
}

void x_disconnect(struct x_connection *c)
{
	xfree(c->monitors);
	if (c->argb_visual)
		XFreeColormap(c->dpy, c->argb_colormap);
	XCloseDisplay(c->dpy);
}

void x_update_monitors_info(struct x_connection *c)
{
	xfree(c->monitors);
	init_monitors(c);
}

void x_update_root_pmap(struct x_connection *c)
{
	c->root_pixmap = x_get_prop_pixmap(c, c->root, c->atoms[XATOM_XROOTPMAP_ID]);
	if (c->root_pixmap == None)
		c->root_pixmap = x_get_prop_pixmap(c, c->root, c->atoms[XATOM_XROOTPMAP_ID2]);
}

Window x_create_default_window(struct x_connection *c, int x, int y,
		unsigned int w, unsigned int h, unsigned long valuemask,
		XSetWindowAttributes *attrs)
{
	return XCreateWindow(c->dpy, c->root, x, y, w, h, 0,
			     c->default_depth, InputOutput,
			     c->default_visual, valuemask, attrs);
}

Pixmap x_create_default_pixmap(struct x_connection *c, unsigned int w,
		unsigned int h)
{
	return XCreatePixmap(c->dpy, c->root, w, h, c->default_depth);
}

Window x_create_default_embedder(struct x_connection *c, Window parent,
				 Window icon, unsigned int w, unsigned int h)
{
	XSetWindowAttributes attrs;
	attrs.background_pixmap = ParentRelative;
	return XCreateWindow(c->dpy, parent, 0, 0, w, h, 0,
			     c->default_depth, InputOutput,
			     c->default_visual, CWBackPixmap, &attrs);
}

void x_set_prop_int(struct x_connection *c, Window win, Atom type, int value)
{
	XChangeProperty(c->dpy, win, type, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char*)&value, 1);
}

void x_set_prop_visualid(struct x_connection *c, Window win,
			 Atom type, VisualID value)
{
	XChangeProperty(c->dpy, win, type, XA_VISUALID, 32,
			PropModeReplace, (unsigned char*)&value, 1);
}

void x_set_prop_atom(struct x_connection *c, Window win, Atom type, Atom at)
{
	XChangeProperty(c->dpy, win, type, XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&at, 1);
}

void x_set_prop_array(struct x_connection *c, Window win, Atom type,
		      const long *values, size_t len)
{
	XChangeProperty(c->dpy, win, type, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char*)values, len);
}

int x_is_window_visible_on_panel(struct x_connection *c, Window win)
{
	Atom *data;
	int ret = 1;
	int num;

	data = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_WINDOW_TYPE],
			XA_ATOM, &num);
	if (data) {
		while (num) {
			num--;
			if (data[num] == c->atoms[XATOM_NET_WM_WINDOW_TYPE_DOCK] ||
			    data[num] == c->atoms[XATOM_NET_WM_WINDOW_TYPE_DESKTOP])
			{
				XFree(data);
				return 0;
			}

		}
		XFree(data);
	}

	data = x_get_prop_data(c, win, c->atoms[XATOM_WM_STATE],
			       c->atoms[XATOM_WM_STATE], 0);
	if (data) {
		if (data[0] == WithdrawnState) {
			XFree(data);
			return 0;
		}
		XFree(data);
	}

	data = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_STATE], XA_ATOM, &num);
	if (!data)
		return 1;

	while (num) {
		num--;
		if (data[num] == c->atoms[XATOM_NET_WM_STATE_SKIP_TASKBAR])
			ret = 0;
	}
	XFree(data);

	return ret;
}

int x_is_window_visible_on_screen(struct x_connection *c, Window win)
{
	Atom *data;
	int ret = 1;
	int num;

	data = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_WINDOW_TYPE],
			XA_ATOM, &num);
	if (data) {
		while (num) {
			num--;
			if (data[num] == c->atoms[XATOM_NET_WM_WINDOW_TYPE_DOCK] ||
			    data[num] == c->atoms[XATOM_NET_WM_WINDOW_TYPE_DESKTOP])
			{
				XFree(data);
				return 0;
			}

		}
		XFree(data);
	}

	data = x_get_prop_data(c, win, c->atoms[XATOM_WM_STATE],
			       c->atoms[XATOM_WM_STATE], 0);
	if (data) {
		if (data[0] == WithdrawnState) {
			XFree(data);
			return 0;
		}
		XFree(data);
	}

	data = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_STATE], XA_ATOM, &num);
	if (!data)
		return 1;

	while (num) {
		num--;
		if (data[num] == c->atoms[XATOM_NET_WM_STATE_SKIP_TASKBAR] ||
		    data[num] == c->atoms[XATOM_NET_WM_STATE_HIDDEN])
			ret = 0;
	}
	XFree(data);

	return ret;
}

int x_is_window_demands_attention(struct x_connection *c, Window win)
{
	Atom *data;
	int ret = 0;
	int num;

	XWMHints *wmh = XGetWMHints(c->dpy, win);
	if (wmh) {
		if (wmh->flags & XUrgencyHint) {
			XFree(wmh);
			return 1;
		}
		XFree(wmh);
	}

	data = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_STATE], XA_ATOM, &num);
	if (!data)
		return 0;

	while (num) {
		num--;
		if (data[num] == c->atoms[XATOM_NET_WM_STATE_DEMANDS_ATTENTION])
			ret = 1;
	}
	XFree(data);

	return ret;
}

int x_is_window_iconified(struct x_connection *c, Window win)
{
	unsigned long *data;
	int ret = 0;

	data = x_get_prop_data(c, win, c->atoms[XATOM_WM_STATE],
			       c->atoms[XATOM_WM_STATE], 0);
	if (data) {
		if (data[0] == IconicState) {
			ret = 1;
		}
		XFree(data);
	}

	int num;
	data = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_STATE], XA_ATOM, &num);
	if (data) {
		while (num) {
			num--;
			if (data[num] == c->atoms[XATOM_NET_WM_STATE_HIDDEN])
				ret = 1;
		}
		XFree(data);
	}

	return ret;
}

void x_realloc_window_name(struct strbuf *sb, struct x_connection *c,
			   Window win, Atom *atom, Atom *atype)
{
	char *name = 0;
	if (*atom != None) {
		/* fast path */
		name = x_get_prop_data(c, win, *atom, *atype, 0);
		if (name)
			goto name_here;
	}
	/****************/
	*atype = c->atoms[XATOM_UTF8_STRING];
	*atom = c->atoms[XATOM_NET_WM_VISIBLE_ICON_NAME];

	name = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_VISIBLE_ICON_NAME],
			c->atoms[XATOM_UTF8_STRING], 0);
	if (name)
		goto name_here;
	/****************/
	*atype = c->atoms[XATOM_UTF8_STRING];
	*atom = c->atoms[XATOM_NET_WM_ICON_NAME];

	name = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_ICON_NAME],
			c->atoms[XATOM_UTF8_STRING], 0);
	if (name)
		goto name_here;
	/****************/
	*atype = XA_STRING;
	*atom = XA_WM_ICON_NAME;

	name = x_get_prop_data(c, win, XA_WM_ICON_NAME, XA_STRING, 0);
	if (name)
		goto name_here;
	/****************/
	*atype = c->atoms[XATOM_UTF8_STRING];
	*atom = XA_WM_ICON_NAME;

	name = x_get_prop_data(c, win, XA_WM_ICON_NAME, c->atoms[XATOM_UTF8_STRING], 0);
	if (name)
		goto name_here;
	/****************/
	*atype = c->atoms[XATOM_UTF8_STRING];
	*atom = c->atoms[XATOM_NET_WM_VISIBLE_NAME];

	name = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_VISIBLE_NAME],
			c->atoms[XATOM_UTF8_STRING], 0);
	if (name)
		goto name_here;
	/****************/
	*atype = c->atoms[XATOM_UTF8_STRING];
	*atom = c->atoms[XATOM_NET_WM_NAME];

	name = x_get_prop_data(c, win, c->atoms[XATOM_NET_WM_NAME],
			c->atoms[XATOM_UTF8_STRING], 0);
	if (name)
		goto name_here;
	/****************/
	*atype = XA_STRING;
	*atom = XA_WM_NAME;

	name = x_get_prop_data(c, win, XA_WM_NAME, XA_STRING, 0);
	if (name)
		goto name_here;
	/****************/
	*atype = c->atoms[XATOM_UTF8_STRING];
	*atom = XA_WM_NAME;

	name = x_get_prop_data(c, win, XA_WM_NAME, c->atoms[XATOM_UTF8_STRING], 0);
	if (name)
		goto name_here;
	else {
		*atom = None;
		*atype = None;
		strbuf_assign(sb, "<unknown>");
		return;
	}
	/****************/
name_here:
	strbuf_assign(sb, name);
	XFree(name);
}

void x_send_netwm_message(struct x_connection *c, Window win,
		Atom a, long l0, long l1, long l2, long l3, long l4)
{
	XClientMessageEvent e;

	e.type = ClientMessage;
	e.window = win;
	e.message_type = a;
	e.format = 32;
	e.data.l[0] = l0;
	e.data.l[1] = l1;
	e.data.l[2] = l2;
	e.data.l[3] = l3;
	e.data.l[4] = l4;

	XSendEvent(c->dpy, c->root, False, SubstructureNotifyMask |
			SubstructureRedirectMask, (XEvent*)&e);
}

void x_send_dnd_message(struct x_connection *c, Window win,
		Atom a, long l0, long l1, long l2, long l3, long l4)
{
	XClientMessageEvent e;

	e.type = ClientMessage;
	e.window = win;
	e.message_type = a;
	e.format = 32;
	e.data.l[0] = l0;
	e.data.l[1] = l1;
	e.data.l[2] = l2;
	e.data.l[3] = l3;
	e.data.l[4] = l4;

	XSendEvent(c->dpy, win, False, NoEventMask, (XEvent*)&e);
}

void x_translate_coordinates(struct x_connection *c, int x, int y,
			     int *xout, int *yout, Window win)
{
	Window tmpwin;
	XTranslateCoordinates(c->dpy, win, c->root, x, y, xout, yout, &tmpwin);
}

/**************************************************************************
  X error trap
**************************************************************************/

static int trapped_error;
static int (*old_error_handler)(Display*, XErrorEvent*);

static int X_error_trap(Display *dpy, XErrorEvent *error)
{
	trapped_error = -1;
	return 0;
}

void x_set_error_trap()
{
	old_error_handler = XSetErrorHandler(X_error_trap);
}

int x_done_error_trap()
{
	XSetErrorHandler(old_error_handler);
	int ret = trapped_error;
	trapped_error = 0;
	return ret;
}
