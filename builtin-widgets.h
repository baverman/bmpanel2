#ifndef BMPANEL2_BUILTIN_WIDGETS_H 
#define BMPANEL2_BUILTIN_WIDGETS_H

#include "gui.h"
#include "widget-utils.h"
#include "array.h"

/**************************************************************************
  Taskbar
**************************************************************************/

struct taskbar_task {
	char *name;
	cairo_surface_t *icon;
	Window win;
	int desktop;
	int x;
	int w;
};

struct taskbar_state {
	struct triple_image background;
	struct text_info font;
};

struct taskbar_theme {
	struct taskbar_state idle;
	struct taskbar_state pressed;
	cairo_surface_t *default_icon;
	int icon_offset[2];

	cairo_surface_t *separator;
};

struct taskbar_widget {
	struct taskbar_theme theme;
	DECLARE_ARRAY(struct taskbar_task, tasks);
	Window active;
	int desktop;

	Window dnd_win;
};

#define TASKBAR_TASK_LEFT_SIDE 0
#define TASKBAR_TASK_RIGHT_SIDE 1

int get_taskbar_task_at(struct widget *tb, int x, int y, int *side_out);

/**************************************************************************
  Clock
**************************************************************************/

struct clock_theme {
	struct triple_image background;
	struct text_info font;
	int text_spacing;
	int spacing;
	char *time_format;
};

struct clock_widget {
	struct clock_theme theme;
};

#endif /* BMPANEL2_BUILTIN_WIDGETS_H */
