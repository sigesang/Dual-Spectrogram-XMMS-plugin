/*
  Dual Spectrogram v1.2.1
 -----------------------
  dual spectral histogram plugin for XMMS

  by Joakim 'basemetal' Elofsson
*/

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmms/plugin.h>
#include <xmms/configfile.h>
#include <unistd.h>

#include "bg-def.xpm"
#include "dspectogram_mini.xpm"

#define THIS_IS "Dual Spectogram 1.2.1"

#define NUM_BANDS 48

#define CONFIG_SECTION "Dual Spectogram"

/* THEMEDIR set at maketime */
#define THEME_DEFAULT_STR ""
#define THEME_DEFAULT_PATH THEMEDIR

/*  */
#define FSEL_ALWAYS_DEFAULT_PATH 
/* analyzer */
#define AWIDTH 128
#define AHEIGHT 48
/* window */
#define TOP_BORDER 14
#define BOTTOM_BORDER 6
#define SIDE_BORDER 7
#define WINWIDTH 275
#define WINHEIGHT AHEIGHT+TOP_BORDER+BOTTOM_BORDER

/* used for nonlinj freq axis */
/* exp(($i+1)/48*log(256))-1  for using the 256point data on 48points */
static int xscl48[49]={ 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 
        6, 6, 7, 9, 10, 11, 13, 14, 16, 19, 21, 24, 27, 31, 34, 39, 
        44, 49, 56, 62, 70, 79, 89, 100, 113, 126, 142, 160, 180, 202, 227, 254,
	256};

extern GtkWidget *mainwin; /* xmms mainwin */
extern GList *dock_window_list; /* xmms dockwinlist*/

static GtkWidget *window = NULL;
static GtkWidget *drwarea = NULL;
static GtkWidget *win_about = NULL;
static GtkWidget *win_conf = NULL;
static GtkWidget *lbl_dbrange = NULL;
static GtkWidget *hscale_dbrange = NULL;
static GtkWidget *fsel = NULL;
static GtkWidget *etry_theme = NULL;
static GtkWidget *btn_snapmainwin = NULL;

static GdkBitmap *mask = NULL;

static GdkPixmap *bg_pixmap = NULL;
static GdkPixmap *pixmap = NULL;

static GdkGC *gc = NULL;

static gfloat *fdata[2];  /* current temp */
static gfloat *hfdata[2]; /* history */

/* configvars */
typedef struct {
  gboolean freq_nonlinj;
  gboolean amp_gain;
  int      db_scale_factor;
  char     *skin_xpm;
  gint      pos_x;
  gint      pos_y;
  gboolean rel_main;
} DSpecgrCfg;

static DSpecgrCfg Cfg={FALSE, FALSE, 20, NULL, -1, -1, 0};

extern GList *dock_add_window(GList *, GtkWidget *);
extern gboolean dock_is_moving(GtkWidget *);
extern void dock_move_motion(GtkWidget *,GdkEventMotion *);
extern void dock_move_press(GList *, GtkWidget *, GdkEventButton *, gboolean);
extern void dock_move_release(GtkWidget *);


static void dspecgr_about();
static void dspecgr_config();
static void dspecgr_init(void);
static void dspecgr_cleanup(void);
static void dspecgr_render_freq(gint16 data[2][256]);
static void dspecgr_config_read();
static GtkWidget* dspecgr_create_menu(void);

static void create_fileselection (void);

VisPlugin dspecgr_vp = {
	NULL, NULL, 0,
	THIS_IS,
	0, /* pcm channels */
	2, /* freq channels */
	dspecgr_init, 
	dspecgr_cleanup,
	dspecgr_about,
	dspecgr_config,
	NULL,
	NULL,
	NULL,
	NULL, 
	dspecgr_render_freq /* render_freq */
};

VisPlugin *get_vplugin_info (void) {
  return &dspecgr_vp;
}

static void dspecgr_destroy_cb (GtkWidget *w,gpointer data) {
  dspecgr_vp.disable_plugin(&dspecgr_vp);
}

static void dspecgr_set_theme() {
  GdkColor color;
  GdkGC *gc2 = NULL;

  if ((Cfg.skin_xpm != NULL) && (strcmp(Cfg.skin_xpm, THEME_DEFAULT_STR) != 0))
    bg_pixmap = gdk_pixmap_create_from_xpm(window->window, &mask, NULL, Cfg.skin_xpm);
  if (bg_pixmap == NULL)
    bg_pixmap = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL, bg_def_xpm);  

  gc2 = gdk_gc_new(mask);
  color.pixel = 1;
  gdk_gc_set_foreground(gc2, &color);
  gdk_draw_rectangle(mask, gc2, TRUE, SIDE_BORDER, TOP_BORDER, AWIDTH, AHEIGHT);
  gdk_draw_rectangle(mask, gc2, TRUE, WINWIDTH-SIDE_BORDER-AWIDTH, TOP_BORDER, AWIDTH, AHEIGHT);
  color.pixel = 0;
  gdk_gc_set_foreground(gc2, &color);
  gdk_draw_line(mask, gc2, WINWIDTH, 0 ,WINWIDTH , WINHEIGHT-1);
  gtk_widget_shape_combine_mask(window, mask, 0, 0);
  gdk_gc_destroy(gc2);

  gdk_draw_pixmap(pixmap, gc , bg_pixmap,
		  0, 0, 0, 0, WINWIDTH, WINHEIGHT);
  gdk_window_clear(drwarea->window);
}

static gint dspecgr_mousebtnrel_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_BUTTON_RELEASE) {
    if (event->button == 1) {
      if (dock_is_moving(window)) {
	dock_move_release(window);
      }
      if ((event->x > (WINWIDTH - TOP_BORDER)) &&
	 (event->y < TOP_BORDER)) { //topright corner
	dspecgr_vp.disable_plugin(&dspecgr_vp);
      }
    }
  }
  
  return TRUE;
}

static gint dspecgr_mousemove_cb(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  if (dock_is_moving(window)) {
    dock_move_motion(window, event);
  }

  return TRUE;
}

static gint dspecgr_mousebtn_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  if (event->type == GDK_BUTTON_PRESS) {
    if ((event->button == 1) &&
       (event->x < (WINWIDTH - TOP_BORDER)) &&
       (event->y <= TOP_BORDER)) { //topright corner
      dock_move_press(dock_window_list, window, event, FALSE);
    }
    
    if (event->button == 3) {
      gtk_menu_popup ((GtkMenu *)data, NULL, NULL, NULL, NULL, 
                            event->button, event->time);
    }
  }

  return TRUE;
}

static void dspecgr_set_icon (GtkWidget *win)
{
  static GdkPixmap *icon;
  static GdkBitmap *mask;
  Atom icon_atom;
  glong data[2];
  
  if (!icon)
    icon = gdk_pixmap_create_from_xpm_d (win->window, &mask, 
					 &win->style->bg[GTK_STATE_NORMAL], 
					 dspectogram_mini_xpm);
  data[0] = GDK_WINDOW_XWINDOW(icon);
  data[1] = GDK_WINDOW_XWINDOW(mask);
  
  icon_atom = gdk_atom_intern ("KWM_WIN_ICON", FALSE);
  gdk_property_change (win->window, icon_atom, icon_atom, 32,
		       GDK_PROP_MODE_REPLACE, (guchar *)data, 2);
}

static void dspecgr_init (void) {
  GdkColor color;
  GtkWidget *menu;

  if(window) return;

  dspecgr_config_read();

  fdata[0]=(gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  fdata[1]=(gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  hfdata[0]=(gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  hfdata[1]=(gfloat *) calloc(NUM_BANDS, sizeof(gfloat));
  if(!fdata[0] || !fdata[1] || !hfdata[0] || !hfdata[1])
    return;

  window = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_window_set_title(GTK_WINDOW(window), THIS_IS);
  gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
  gtk_window_set_wmclass(GTK_WINDOW(window), 
			 "XMMS_Player", "DualSpectralizer");
  gtk_widget_set_usize(window, WINWIDTH, WINHEIGHT);
  gtk_widget_set_events(window, GDK_BUTTON_MOTION_MASK | 
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
  gtk_widget_realize(window);
  dspecgr_set_icon(window);
  gdk_window_set_decorations(window->window, 0);

  if (Cfg.pos_x != -1)
    gtk_widget_set_uposition (window, Cfg.pos_x, Cfg.pos_y);

  menu = dspecgr_create_menu();

  gtk_signal_connect(GTK_OBJECT(window),"destroy",
		     GTK_SIGNAL_FUNC(dspecgr_destroy_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(window), "destroy",
		     GTK_SIGNAL_FUNC(gtk_widget_destroyed), &window);

  gtk_signal_connect(GTK_OBJECT(window), "button_press_event",
		     GTK_SIGNAL_FUNC(dspecgr_mousebtn_cb), (gpointer) menu);
  gtk_signal_connect(GTK_OBJECT(window), "button_release_event",
		     GTK_SIGNAL_FUNC(dspecgr_mousebtnrel_cb), NULL);
  gtk_signal_connect(GTK_OBJECT(window), "motion_notify_event",
		     GTK_SIGNAL_FUNC(dspecgr_mousemove_cb), NULL);

  gc = gdk_gc_new(window->window);
  gdk_color_black(gdk_colormap_get_system(), &color);
  gdk_gc_set_foreground(gc, &color);

  pixmap = gdk_pixmap_new(window->window, WINWIDTH, WINHEIGHT,
			  gdk_visual_get_best_depth());
  gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, WINWIDTH, WINHEIGHT);

  drwarea = gtk_drawing_area_new();
  gtk_widget_show (drwarea);
  gtk_container_add (GTK_CONTAINER (window), drwarea);
  gtk_drawing_area_size((GtkDrawingArea *) drwarea, WINWIDTH, WINHEIGHT);
  gdk_window_set_back_pixmap(drwarea->window, pixmap, 0);
  gdk_window_clear(drwarea->window);

  dspecgr_set_theme();
  gtk_widget_show(window);

  if (!g_list_find(dock_window_list, window)) {
    dock_add_window(dock_window_list, window);
  }
}

static void dspecgr_config_read () {
  ConfigFile *cfg;
  gchar *filename, *themefile = NULL;

  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  if ((cfg = xmms_cfg_open_file(filename)) != NULL) {
    xmms_cfg_read_boolean(cfg, CONFIG_SECTION, "amp_gain", &Cfg.amp_gain);
    xmms_cfg_read_int(cfg, CONFIG_SECTION, 
		      "db_scale_factor", &Cfg.db_scale_factor);
    xmms_cfg_read_boolean(cfg, CONFIG_SECTION, "freq_nonlinj", &Cfg.freq_nonlinj);
    xmms_cfg_read_string(cfg, CONFIG_SECTION, "skin_xpm", &themefile);
    if (themefile)
      Cfg.skin_xpm = g_strdup(themefile);

    xmms_cfg_read_int(cfg, CONFIG_SECTION, "pos_x", &Cfg.pos_x);
    xmms_cfg_read_int(cfg, CONFIG_SECTION, "pos_y", &Cfg.pos_y);
    xmms_cfg_free(cfg);
  }
  g_free(filename);
}

static void dspecgr_config_write () {
  ConfigFile *cfg;
  gchar *filename;
  if(Cfg.pos_x!=-1 && window != NULL)
    gdk_window_get_position(window->window, &Cfg.pos_x, &Cfg.pos_y);
 
  filename = g_strconcat(g_get_home_dir(), "/.xmms/config", NULL);
  if((cfg = xmms_cfg_open_file(filename)) != NULL) {
    xmms_cfg_write_boolean(cfg, CONFIG_SECTION, "amp_gain", Cfg.amp_gain);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, 
		      "db_scale_factor", Cfg.db_scale_factor);
    xmms_cfg_write_boolean(cfg, CONFIG_SECTION, "freq_nonlinj", Cfg.freq_nonlinj);
    xmms_cfg_write_string(cfg, CONFIG_SECTION, "skin_xpm",
			  (Cfg.skin_xpm != NULL) ? Cfg.skin_xpm : THEME_DEFAULT_STR);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "pos_x", Cfg.pos_x);
    xmms_cfg_write_int(cfg, CONFIG_SECTION, "pos_y", Cfg.pos_y);
    xmms_cfg_write_file(cfg, filename);
    xmms_cfg_free(cfg);
  }
  g_free(filename);
}

static void dspecgr_cleanup(void) {
  dspecgr_config_write();

  if (g_list_find(dock_window_list, window)) {
    g_list_remove(dock_window_list, window); 
  }

  if (win_about) gtk_widget_destroy(win_about);
  if (win_conf)  gtk_widget_destroy(win_conf);
  if (window)    gtk_widget_destroy(window);
  if (fsel)      gtk_widget_destroy(fsel);
  if (gc)           { gdk_gc_unref(gc); gc = NULL; }
  if (bg_pixmap)    { gdk_pixmap_unref(bg_pixmap); bg_pixmap = NULL; }
  if (pixmap) { gdk_pixmap_unref(pixmap); pixmap = NULL; }
  if (fdata[0])  free(fdata[0]); 
  if (fdata[1])  free(fdata[1]);
  if (hfdata[0]) free(hfdata[0]);
  if (hfdata[1]) free(hfdata[1]);
  if (Cfg.skin_xpm) g_free(Cfg.skin_xpm);
}

static void dspecgr_render_freq(gint16 data[2][256]) {
  int i, a, b;
  static gint16 r,l;
  gfloat yr, yl, *pr,*pl;
  gfloat *oldyr,*oldyl;
  gint16 *data_r,*data_l;

  if (!window)
    return;

  /* messaround with pointers a bit.. for speedup..*/
  oldyl = hfdata[0];
  oldyr = hfdata[1];
  pl = fdata[0];
  pr = fdata[1];
  data_l = data[0];
  data_r = data[1];

  for (i = 0; i < NUM_BANDS; i++) {
    /* convert 256point integerdata to 48point float data (range 0.0-1.0) 
       linjear or nonlinjear */
    if (Cfg.freq_nonlinj) {
      /* nonlinjear */
      a = xscl48[i]; b = 1; yr = 0.0; yl = 0.0;
      do {
	yl += (float) *(data_l + a) / 32678.0;
	yr += (float) *(data_r + a) / 32678.0;
	a++; b++;
      } while ( a < xscl48[i+1] );
      if ( b>1 ) { yr /= b; yl /= b; }
    } else {
      /* linjear */
      yl = (float)( *(data_r + 2*i) + *(data_r + 2*i + 1) ) / 65536.0;
      yr = (float)( *(data_l + 2*i) + *(data_l + 2*i + 1) ) / 65536.0;
      a = i;
    }
    
    /* calc 3dbgain for data  */
    if ( Cfg.amp_gain ) {
      /* really the right thing at all?.. looks good on screen.. but*/
      /* I'm really not sure abaut the correctness of this...*/
      yl *= sqrt((a+1)*22.05/256); // 22.05 is samplefreq/2/1000... is not always right
      yr *= sqrt((a+1)*22.05/256); // but.. very common (44100kHz)
    }

    *pl = yl; *pr = yr;
    oldyl++; oldyr++;
    pl++; pr++;
  }

  GDK_THREADS_ENTER();

  pl = fdata[0];
  pr = fdata[1];
  /* scroll */
  gdk_draw_pixmap(pixmap, gc, pixmap,
		  1 + SIDE_BORDER, TOP_BORDER,
		  SIDE_BORDER, TOP_BORDER, 
		  AWIDTH - 1, AHEIGHT);  
  gdk_draw_pixmap(pixmap, gc, pixmap,
		  WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER,
		  1 + WINWIDTH - SIDE_BORDER - AWIDTH, TOP_BORDER,
		  AWIDTH - 1, AHEIGHT);

  for (i = 0; i < NUM_BANDS; i++) {
    r = (*pr != 0) ? WINHEIGHT + Cfg.db_scale_factor * log10(*pr) : 1;
    l = (*pl != 0) ? WINHEIGHT + Cfg.db_scale_factor * log10(*pl) : 1;
    if (r < 1) r = 1;
    if (l < 1) l = 1;
    gdk_draw_pixmap(pixmap, gc, bg_pixmap, WINWIDTH+1, WINHEIGHT- 1 - l,
		    SIDE_BORDER + AWIDTH - 1, AHEIGHT + TOP_BORDER - 1 - i, 1, 1);
    gdk_draw_pixmap(pixmap, gc, bg_pixmap, WINWIDTH+1, WINHEIGHT- 1 - r,
		    WINWIDTH - SIDE_BORDER - AWIDTH, AHEIGHT + TOP_BORDER - 1 - i, 1, 1);
    pl++; pr++;
  }
  gdk_window_clear(drwarea->window);
  GDK_THREADS_LEAVE();
  return;			
}

/* ************************* */
/* aboutwindow callbacks     */
static void on_btn_about_close_clicked (GtkButton *button, gpointer user_data) {
  gtk_widget_destroy(win_about);
  win_about=NULL;
}

/* ************************* */
/* configwindow callbacks    */
static void on_ckbtn_gain_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.amp_gain = gtk_toggle_button_get_active(togglebutton);
}

static void on_rdbtn_freqscl_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  Cfg.freq_nonlinj = gtk_toggle_button_get_active((GtkToggleButton *) user_data);
}

static void on_btn_snapmainwin_clicked (GtkButton *button, gpointer user_data) {
  gint x, y, h, w;
  if (mainwin != NULL) {
    gdk_window_get_position(mainwin->window, &x, &y);
    gdk_window_get_size(mainwin->window, &w, &h);
    if (window)
      gdk_window_move(window->window, x, y+h);
    if (gtk_toggle_button_get_active((GtkToggleButton *) user_data)) {
      Cfg.pos_x = x;
      Cfg.pos_y = y+h;
    }
  }
}

static void on_ckbtn_rcoords_toggled (GtkToggleButton *togglebutton, gpointer user_data) {
  if (gtk_toggle_button_get_active(togglebutton)) {
    gdk_window_get_position(window->window, &Cfg.pos_x, &Cfg.pos_y);
  } else {
    Cfg.pos_x = -1;
  }
}

static void on_confbtn_close_clicked (GtkButton *button, gpointer user_data) {
  dspecgr_config_write();
  gtk_widget_destroy(win_conf);
  win_conf = NULL;
}

static void on_adj_dbrange_value_changed (GtkAdjustment *adjustment, 
					  gpointer user_data) {
  char txt[100];
  Cfg.db_scale_factor = adjustment->value;
  sprintf(txt, "Range is %4.2f db", 68.0*20.0/(float)Cfg.db_scale_factor);
  gtk_label_set_text((GtkLabel *)lbl_dbrange, txt);
}

static void on_etry_theme_changed (GtkEditable *editable, gpointer user_data) {
  g_free(Cfg.skin_xpm);
  Cfg.skin_xpm = g_strdup(gtk_entry_get_text((GtkEntry *) editable));
  if (window) 
    dspecgr_set_theme();
}

/* ************************* */
/* fileselect callbacks     */
static void on_btn_theme_clicked (GtkButton *button, gpointer user_data) {
  if(fsel == NULL)
    create_fileselection();
  gtk_widget_show(fsel);
}

static void on_btn_fsel_cancel_clicked (GtkButton *button, gpointer user_data) {
  gtk_widget_destroy(fsel);
  fsel = NULL;
}

static void on_btn_fsel_ok_clicked (GtkButton *button, gpointer user_data) {
  gchar *fname;
  fname=gtk_file_selection_get_filename((GtkFileSelection *) fsel);
  gtk_entry_set_text((GtkEntry *) etry_theme, fname);
  gtk_widget_destroy(fsel);
  fsel = NULL;
}

#define ABOUT_MARGIN 10
#define ABOUT_WIDTH 300
#define ABOUT_HEIGHT 150

/* ****                                            */
/* creates aboutwindow if not present and shows it */
static void dspecgr_about(void) {
  GtkWidget *vb_main;
  GtkWidget *frm;
  GtkWidget *lbl_author;
  GtkWidget *btn_about_close;

  if(win_about)
    return;

  win_about = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_widget_realize(win_about);
  gtk_window_set_title (GTK_WINDOW (win_about), "About");
  gtk_signal_connect(GTK_OBJECT(win_about), "destroy", 
		     GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		     &win_about);

  vb_main = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (win_about), vb_main);
  gtk_widget_show (vb_main);

  frm = gtk_frame_new(THIS_IS);
  gtk_box_pack_start (GTK_BOX (vb_main), frm, TRUE, TRUE, 0);
  gtk_widget_set_usize (frm, ABOUT_WIDTH - ABOUT_WIDTH * 2, ABOUT_HEIGHT - ABOUT_MARGIN * 2);
  gtk_container_set_border_width (GTK_CONTAINER (frm), ABOUT_MARGIN);
  gtk_widget_show (frm);

  lbl_author = gtk_label_new ("plugin for XMMS\n"
			      "made by Joakim Elofsson\n"
			      "joakim.elofsson@home.se\n"
			      "   http://www.shell.linux.se/bm/   ");
  gtk_container_add (GTK_CONTAINER (frm), lbl_author);
  gtk_widget_show (lbl_author);

  btn_about_close = gtk_button_new_with_label ("Close");
  gtk_box_pack_start (GTK_BOX (vb_main), btn_about_close, FALSE, FALSE, 0);
  gtk_widget_show (btn_about_close);

  gtk_signal_connect (GTK_OBJECT (btn_about_close), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_about_close_clicked),
                      GTK_OBJECT(win_about));

  gtk_widget_show (win_about);
}
                                              
/* ****                                             */
/* creates configwindow if not present and shows it */
static void dspecgr_config (void) {
  gchar txt[40];
  GtkWidget *vb_main;
  GtkWidget *nb_main;
  GtkWidget *frm_freq;
  GtkWidget *vb_freqaxis;
  GSList *vb_freqaxis_group = NULL;
  GtkWidget *rdbtn_linj;
  GtkWidget *rdbtn_nonlinj;
  GtkWidget *lbl_freq;
  GtkWidget *frm_amp;
  GtkWidget *fixed;
  GtkWidget *ckbtn_gain;
  GtkWidget *lbl_amp;
  GtkWidget *btn_close;
  GtkWidget *vb_misc;
  GtkWidget *frm_misc;
  GtkWidget *vb_miscwin;

  GtkWidget *ckbtn_rcoords;
  GtkWidget *frm_theme;
  GtkWidget *hb_theme;

  GtkWidget *btn_theme;
  GtkWidget *lbl_misc;

  GtkObject *adj_dbrange;

  if(win_conf)
    return;

  if(Cfg.skin_xpm == NULL) /* if config never read */
    dspecgr_config_read();  

  win_conf = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_title (GTK_WINDOW (win_conf), "Config - " THIS_IS);
  gtk_signal_connect(GTK_OBJECT (win_conf), "destroy", 
		     GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		     &win_conf);

  vb_main = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_main);
  gtk_container_add (GTK_CONTAINER (win_conf), vb_main);

  nb_main = gtk_notebook_new ();
  gtk_widget_show (nb_main);
  gtk_box_pack_start (GTK_BOX (vb_main), nb_main, TRUE, TRUE, 0);

  frm_freq = gtk_frame_new ("Frequency axis");
  gtk_widget_show (frm_freq);
  gtk_container_add (GTK_CONTAINER (nb_main), frm_freq);

  vb_freqaxis = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_freqaxis);
  gtk_container_add (GTK_CONTAINER (frm_freq), vb_freqaxis);

  rdbtn_linj = gtk_radio_button_new_with_label (vb_freqaxis_group, "linjear frequency axis");
  vb_freqaxis_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_linj));
  gtk_widget_show (rdbtn_linj);
  gtk_box_pack_start (GTK_BOX (vb_freqaxis), rdbtn_linj, FALSE, FALSE, 0);

  rdbtn_nonlinj = gtk_radio_button_new_with_label (vb_freqaxis_group, "nonlinjear frequency axis");
  vb_freqaxis_group = gtk_radio_button_group (GTK_RADIO_BUTTON (rdbtn_nonlinj));
  gtk_widget_show (rdbtn_nonlinj);
  gtk_box_pack_start (GTK_BOX (vb_freqaxis), rdbtn_nonlinj, FALSE, FALSE, 0);
  gtk_toggle_button_set_active((GtkToggleButton *) rdbtn_nonlinj, Cfg.freq_nonlinj);

  lbl_freq = gtk_label_new ("Frequency");
  gtk_widget_show (lbl_freq);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main), gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb_main), 0), lbl_freq);

  frm_amp = gtk_frame_new ("Amplitude axis");
  gtk_widget_show (frm_amp);
  gtk_container_add (GTK_CONTAINER (nb_main), frm_amp);

  fixed = gtk_fixed_new ();
  gtk_widget_show (fixed);
  gtk_container_add (GTK_CONTAINER (frm_amp), fixed);

  ckbtn_gain = gtk_check_button_new_with_label ("gain 3db per octave");
  gtk_toggle_button_set_active((GtkToggleButton *) ckbtn_gain, Cfg.amp_gain);
  gtk_widget_show (ckbtn_gain);  
  gtk_fixed_put (GTK_FIXED (fixed), ckbtn_gain, 0, 40);
  gtk_widget_set_usize (ckbtn_gain, 152, 16);

  sprintf(txt, "logaritmic range is %4.2f db", 68.0*20.0/(float)Cfg.db_scale_factor);
  lbl_dbrange = gtk_label_new (txt);
  gtk_widget_show (lbl_dbrange);
  gtk_fixed_put (GTK_FIXED (fixed), lbl_dbrange, 96, 0);
  gtk_widget_set_usize (lbl_dbrange, 160, 16);
  gtk_misc_set_alignment (GTK_MISC (lbl_dbrange), 0.0, 0.5);

  adj_dbrange = gtk_adjustment_new (Cfg.db_scale_factor, 12, 26, 1, 0, 0);
  hscale_dbrange = gtk_hscale_new (GTK_ADJUSTMENT (adj_dbrange));
  gtk_widget_show (hscale_dbrange);
  gtk_fixed_put (GTK_FIXED (fixed), hscale_dbrange, 96, 16);
  gtk_widget_set_usize (hscale_dbrange, 160, 16);
  gtk_scale_set_draw_value (GTK_SCALE (hscale_dbrange), FALSE);
  gtk_scale_set_digits (GTK_SCALE (hscale_dbrange), 0);

  lbl_amp = gtk_label_new ("Amplitude");
  gtk_widget_show (lbl_amp);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main),
			      gtk_notebook_get_nth_page (GTK_NOTEBOOK (nb_main), 1), lbl_amp);

  vb_misc = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_misc);
  gtk_container_add (GTK_CONTAINER (nb_main), vb_misc);

  frm_misc = gtk_frame_new ("Window");
  gtk_widget_show (frm_misc);
  gtk_box_pack_start (GTK_BOX (vb_misc), frm_misc, FALSE, FALSE, 0);

  vb_miscwin = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vb_miscwin);
  gtk_container_add (GTK_CONTAINER (frm_misc), vb_miscwin);

  btn_snapmainwin = gtk_button_new_with_label ("Snap below mainwindow");
  gtk_widget_show (btn_snapmainwin);
  gtk_box_pack_start (GTK_BOX (vb_miscwin), btn_snapmainwin, FALSE, FALSE, 0);

  ckbtn_rcoords = gtk_check_button_new_with_label ("Remember possision");
  gtk_toggle_button_set_active((GtkToggleButton *) ckbtn_rcoords, (Cfg.pos_x!=-1)?TRUE:FALSE);
  gtk_widget_show (ckbtn_rcoords);
  gtk_box_pack_start (GTK_BOX (vb_miscwin), ckbtn_rcoords, FALSE, FALSE, 0);

  frm_theme = gtk_frame_new ("Theme");
  gtk_widget_show (frm_theme);
  gtk_box_pack_start (GTK_BOX (vb_misc), frm_theme, TRUE, TRUE, 0);

  hb_theme = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hb_theme);
  gtk_container_add (GTK_CONTAINER (frm_theme), hb_theme);

  etry_theme = gtk_entry_new ();
  gtk_widget_show (etry_theme);
  gtk_box_pack_start (GTK_BOX (hb_theme), etry_theme, TRUE, TRUE, 0);
  gtk_entry_set_editable (GTK_ENTRY (etry_theme), TRUE);
  gtk_entry_set_text((GtkEntry *) etry_theme, 
		     Cfg.skin_xpm ? Cfg.skin_xpm : THEME_DEFAULT_STR);

  btn_theme = gtk_button_new_with_label ("Choose Theme");
  gtk_widget_show (btn_theme);
  gtk_box_pack_start (GTK_BOX (hb_theme), btn_theme, FALSE, FALSE, 0);

  lbl_misc = gtk_label_new ("Misc");
  gtk_widget_show (lbl_misc);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (nb_main),
			      gtk_notebook_get_nth_page(GTK_NOTEBOOK (nb_main), 2), lbl_misc);
  btn_close = gtk_button_new_with_label ("Close");
  gtk_widget_show (btn_close);
  gtk_box_pack_start (GTK_BOX (vb_main), btn_close, FALSE, FALSE, 0);

  gtk_signal_connect (GTK_OBJECT (ckbtn_rcoords), "toggled",
                      GTK_SIGNAL_FUNC (on_ckbtn_rcoords_toggled),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (rdbtn_linj), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_freqscl_toggled),
                      (gpointer) rdbtn_nonlinj);
  gtk_signal_connect (GTK_OBJECT (rdbtn_nonlinj), "toggled",
                      GTK_SIGNAL_FUNC (on_rdbtn_freqscl_toggled),
                      (gpointer) rdbtn_nonlinj);
  gtk_signal_connect (GTK_OBJECT (btn_snapmainwin), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_snapmainwin_clicked),
                      (gpointer) ckbtn_rcoords);
  gtk_signal_connect (GTK_OBJECT (ckbtn_gain), "toggled",
                      GTK_SIGNAL_FUNC (on_ckbtn_gain_toggled),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_close), "clicked",
                      GTK_SIGNAL_FUNC (on_confbtn_close_clicked),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_theme), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_theme_clicked),
		      NULL);
  gtk_signal_connect (GTK_OBJECT (etry_theme), "changed",
                      GTK_SIGNAL_FUNC (on_etry_theme_changed),
		      NULL);
  gtk_signal_connect (adj_dbrange, "value-changed",
                      GTK_SIGNAL_FUNC (on_adj_dbrange_value_changed),
                      NULL);

  gtk_widget_show(win_conf);
}

void create_fileselection (void) {
  GtkWidget *btn_fsel_cancel;
  GtkWidget *btn_fsel_ok;
  gchar *themefile = NULL;

  fsel = gtk_file_selection_new ("Choose file");
  gtk_object_set_data (GTK_OBJECT (fsel), "fsel", fsel);
  gtk_container_set_border_width (GTK_CONTAINER (fsel), 5);

  btn_fsel_ok = GTK_FILE_SELECTION (fsel)->ok_button;
  gtk_object_set_data (GTK_OBJECT (fsel), "btn_fsel_ok", btn_fsel_ok);
  gtk_widget_show (btn_fsel_ok);
  GTK_WIDGET_SET_FLAGS (btn_fsel_ok, GTK_CAN_DEFAULT);

  btn_fsel_cancel = GTK_FILE_SELECTION (fsel)->cancel_button;
  gtk_object_set_data (GTK_OBJECT (fsel), 
		       "btn_fsel_cancel", btn_fsel_cancel);
  gtk_widget_show (btn_fsel_cancel);
  GTK_WIDGET_SET_FLAGS (btn_fsel_cancel, GTK_CAN_DEFAULT);

#ifndef FSEL_ALWAYS_DEFAULT_PATH
  themefile = Cfg.skin_xpm;
  if (!themefile && (strcmp(Cfg.skin_xpm, THEME_DEFAULT_STR) == 0))
    themefile = (THEME_DEFAULT_PATH);
#else
  themefile = (THEME_DEFAULT_PATH);
#endif

  gtk_file_selection_set_filename((GtkFileSelection *) fsel, themefile);

  gtk_signal_connect (GTK_OBJECT (btn_fsel_cancel), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_fsel_cancel_clicked),
		      NULL);
  gtk_signal_connect (GTK_OBJECT (btn_fsel_ok), "clicked",
                      GTK_SIGNAL_FUNC (on_btn_fsel_ok_clicked),
		      NULL);
}

void on_item_close_activate(GtkMenuItem *menuitem, gpointer data)
{
  dspecgr_vp.disable_plugin(&dspecgr_vp);
}

void on_item_about_activate(GtkMenuItem *menuitem, gpointer data)
{
  dspecgr_about();
}

void on_item_conf_activate(GtkMenuItem *menuitem, gpointer data)
{
  dspecgr_config();
}

GtkWidget* dspecgr_create_menu(void)
{
  GtkWidget *menu;
  GtkAccelGroup *m_acc;
  
  GtkWidget *sep;
  GtkWidget *item_close;
  GtkWidget *item_about;
  GtkWidget *item_conf;

  menu = gtk_menu_new();
  m_acc = gtk_menu_ensure_uline_accel_group(GTK_MENU(menu));

  item_about = gtk_menu_item_new_with_label("About " THIS_IS);
  gtk_widget_show(item_about);
  gtk_container_add (GTK_CONTAINER(menu), item_about);

  sep = gtk_menu_item_new ();
  gtk_widget_show(sep);
  gtk_container_add (GTK_CONTAINER(menu), sep);
  gtk_widget_set_sensitive(sep, FALSE);

  item_conf = gtk_menu_item_new_with_label("Config");
  gtk_widget_show(item_conf);
  gtk_container_add (GTK_CONTAINER(menu), item_conf);

  item_close = gtk_menu_item_new_with_label("Close");
  gtk_widget_show(item_close);
  gtk_container_add (GTK_CONTAINER(menu), item_close);

  gtk_signal_connect(GTK_OBJECT(item_close), "activate",
		     GTK_SIGNAL_FUNC(on_item_close_activate), NULL);

  gtk_signal_connect(GTK_OBJECT(item_about), "activate",
		     GTK_SIGNAL_FUNC(on_item_about_activate), NULL);

  gtk_signal_connect(GTK_OBJECT(item_conf), "activate",
		     GTK_SIGNAL_FUNC(on_item_conf_activate), NULL);

  return menu;
}

