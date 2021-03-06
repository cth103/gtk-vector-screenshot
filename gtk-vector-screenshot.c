/*
 * © 2011 Joachim Breitner <mail@joachim-breitner.de>
 *
 *   This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


// For strchrnul
#define _GNU_SOURCE

#include <math.h>
#include <string.h>
#include <libgen.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <cairo-pdf.h>
#include <cairo-svg.h>
#include <cairo-ps.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif /* MAX */
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif /* MIN */

gchar *filename;
const gchar *type;

GdkAtom pdfscreenshot_atom;
char *supported_str = "supported";

XErrorHandler old_handler = (XErrorHandler) 0;

/*
 * This function handles the file type combo box callback in the Save As
 * dialogue. I would have expected that such functionality is already provided
 * by gtk...
 */
void pdfscreenshot_type_selected(GtkComboBox *format_combo, GtkFileChooser *chooser) {
    // The id is known to be the file extension.
    const char *the_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(format_combo));

    // Update filter, to only display appropriate files.
    GtkFileFilter *filter = gtk_file_filter_new();
    char *pattern = g_strdup_printf("*.%s", the_id);
    gtk_file_filter_add_pattern(filter, pattern);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), filter);

    // Update filename extension in the chooser
    gchar *filename = gtk_file_chooser_get_filename(chooser);
    if (filename != NULL) {
        // strdup result from basename as we are going to modify it.
        char *base = g_strdup(basename(filename));
        // Remove extension from string, if there is one.
        *(strchrnul(base, '.')) = '\0';
        // Assemble and set new suggested filename
        char *new_filename = g_strdup_printf("%s.%s", base, the_id);
        gtk_file_chooser_set_current_name(chooser, new_filename);
        g_free(filename);
        g_free(base);
        g_free(new_filename);
    }
}

/*
 * This function draws the main window in the preview pane of the Save As
 * dialogue. Not very useful, but we do it because we can.
 */
void pdfscreenshot_draw_preview(GtkWidget *widget, cairo_t *cr, gpointer window) {

    int draw_width = gtk_widget_get_allocated_width(widget);
    int draw_height = gtk_widget_get_allocated_height(widget);
    int win_width = gtk_widget_get_allocated_width(window);
    int win_height = gtk_widget_get_allocated_height(window);

    gtk_widget_set_size_request(widget, MIN(win_width, 500), MIN(win_height, 300));

    double scale = fmin(1, fmax(1.0 * draw_width / win_width, 1.0 * draw_height / win_height));

    cairo_scale(cr, scale, scale);

    cairo_translate(cr, (draw_width - scale * win_width) / 2,
                    (draw_height - scale * win_height) / 2);

    gtk_widget_draw(window, cr);
}

void draw_pointer(cairo_t* cr, float x, float y, float size, float red, float green, float blue)
{
    static float const coords[] = {
	    0, 0.820,
	    0.175, 0.662,
	    0.366, 1.000,
	    0.522, 0.905,
	    0.350, 0.595,
	    0.596, 0.588,
    };

    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_move_to(cr, 0, 0);
    for (int i = 0; i < (sizeof(coords) / sizeof(float)); i += 2) {
	    cairo_line_to(cr, coords[i] * size, coords[i+1] * size);
    }
    cairo_close_path(cr);
    cairo_set_line_width(cr, 3);
    cairo_set_source_rgb(cr, red, green, blue);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_fill(cr);
    cairo_restore(cr);
}

/*
 * The main routine of saving a vector screenshot. Surprisingly simple. It is
 * parametized by the cairo surface creating function, as they all have the
 * same signature.
 */
void pdfscreenshot_draw_to_vector(GtkWidget *widget, const gchar *filename,
                                  cairo_surface_t *create_surface(const char *, double, double)) {
    cairo_surface_t *surface =
            create_surface(filename,
                           1.0 * gtk_widget_get_allocated_width(widget),
                           1.0 * gtk_widget_get_allocated_height(widget));
    cairo_t *cr = cairo_create(surface);
    gtk_widget_draw(widget, cr);

    /* Find where the mouse pointer was */

    int dummy;
    unsigned int dummyU;
    Window dummyW;
    Window selected_window;

    XQueryPointer(gdk_x11_get_default_xdisplay(),
        gdk_x11_get_default_root_xwindow(),
        &dummyW, &selected_window, &dummy, &dummy, &dummy, &dummy, &dummyU);

    selected_window = Find_Client(gdk_x11_get_default_xdisplay(),
        gdk_x11_get_default_root_xwindow(),
        selected_window);

    int x, y;
    XQueryPointer(gdk_x11_get_default_xdisplay(),
	selected_window,
	&dummyW, &dummyW, &dummy, &dummy, &x, &y, &dummyU);

    draw_pointer(cr, x - 0.5, y - 0.8, 18, 0.8, 0.8, 0.8);
    draw_pointer(cr, x, y, 16, 1, 1, 1);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

/*
 * Like above, but for PNG. Needs slightly different order of cairo function calls.
 */
void pdfscreenshot_draw_to_png(GtkWidget *widget, const gchar *filename) {
    cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32,
            gtk_widget_get_allocated_width(widget),
            gtk_widget_get_allocated_height(widget));
    cairo_t *cr = cairo_create(surface);
    gtk_widget_draw(widget, cr);

    cairo_destroy(cr);
    cairo_surface_write_to_png(surface, filename);
    cairo_surface_destroy(surface);
}

/*
 * Called when the main button is pressed. Finds the main window, displays the
 * file chooser and saves the screenshot.
 */
void
pdfscreenshot_take_shot(GtkWindow *window) {
    // Set up the file chooser
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
            "Save vector screenshot",
            window,
            GTK_FILE_CHOOSER_ACTION_SAVE,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
            NULL);

    // Suggested file name derived from the application name.
    char *filename = g_strdup_printf("%s.pdf", g_get_application_name());
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (chooser), filename);

    // Some generally useful setup for a Save As dialogue
    gtk_window_set_transient_for(GTK_WINDOW(chooser), GTK_WINDOW(window));
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER (chooser), TRUE);

    // The combo box that selects the desired file type. We (ab)use the
    // id field for the file extension.
    GtkWidget *format_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(format_combo),
                              "pdf", "Save as PDF (*.pdf)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(format_combo),
                              "svg", "Save as SVG (*.svg)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(format_combo),
                              "ps", "Save as PostScript (*.ps)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(format_combo),
                              "png", "Save as PNG (*.png)");
    // When this changes, we call pdfscreenshot_type_selected.
    g_signal_connect(GTK_COMBO_BOX(format_combo), "changed",
                     G_CALLBACK(pdfscreenshot_type_selected), chooser);
    // Default to PDF
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(format_combo), type);

    // The framed drawing area is used for a preview of the window, as a gimmick.
    GtkWidget *drawing_area = gtk_drawing_area_new();
    g_signal_connect (G_OBJECT(drawing_area), "draw",
                      G_CALLBACK(pdfscreenshot_draw_preview), window);

    int win_width = gtk_widget_get_allocated_width(GTK_WIDGET(window));
    int win_height = gtk_widget_get_allocated_height(GTK_WIDGET(window));
    gtk_widget_set_size_request(drawing_area, MIN(win_width, 500), MIN(win_height, 300));

    GtkWidget *frame = gtk_aspect_frame_new("Preview", 0.5, 0, 1, TRUE);
    gtk_container_add(GTK_CONTAINER(frame), drawing_area);

    // Shove both widgets in a vbox as the “extra” of the file chooser dialogue
    GtkWidget *vbox = gtk_vbox_new(FALSE, 8);
    gtk_box_pack_start(GTK_BOX(vbox), format_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
    gtk_widget_show_all(vbox);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(chooser), vbox);

    // Run the dialogue and act if OK was pressed
    if (1) {
        // Read the filename and selected file type
        pdfscreenshot_draw_to_vector(GTK_WIDGET(window), filename,
                                     cairo_pdf_surface_create);
    }

    gtk_widget_destroy(chooser);
}

gboolean
pdfscreenshot_take_shot_soon(gpointer window) {
    pdfscreenshot_take_shot(GTK_WINDOW(window));
    return FALSE;
}

/*
 * Ignore all BadWindow errors.
 */
int
silent_error_handler(Display *display, XErrorEvent *error) {
    if (error->error_code != BadWindow) {
        return old_handler(display, error);
    }
    return 0;
}

GdkFilterReturn
pdfscreenshot_event_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data) {
    XEvent *ev = (XEvent *) xevent;

    if (ev->type == MapNotify) {
        XTextProperty supported;
        GdkDisplay *display = gdk_x11_lookup_xdisplay(ev->xmap.display);

        XStringListToTextProperty(&supported_str, 1, &supported);

        if (display)
            gdk_x11_display_error_trap_push(display);
        else
            gdk_error_trap_push();
        XSetTextProperty(ev->xmap.display,
                         ev->xmap.window,
                         &supported,
                         gdk_x11_atom_to_xatom(pdfscreenshot_atom));
        if (display)
            gdk_x11_display_error_trap_pop_ignored(display);
        else
            gdk_error_trap_pop_ignored();
    } else if (ev->type == ClientMessage &&
               ev->xclient.message_type == gdk_x11_atom_to_xatom(pdfscreenshot_atom)) {
        if (event->any.window != NULL) {
            GtkWindow *gwin;
            gdk_window_get_user_data(event->any.window, (gpointer *) &gwin);
            //printf("Taking shot of XWindow 0x%lx, GtkWindow %p\n", ev->xclient.window, gwin);
            g_idle_add(pdfscreenshot_take_shot_soon, gwin);
        } else {
            g_warning("Got a GTK_VECTOR_SCREENSHOT XClientMessage, but window 0x%lx is not known to me.",
                      ev->xclient.window);
        }
    }

    return GDK_FILTER_CONTINUE;
}


/*
 * Module setup function
 */
int
gtk_module_init(gint argc, char *argv[]) {
    filename = NULL;
    type = "pdf";

    pdfscreenshot_atom = gdk_atom_intern("GTK_VECTOR_SCREENSHOT", FALSE);

    gdk_window_add_filter(NULL, pdfscreenshot_event_filter, NULL);

    return FALSE;
}


