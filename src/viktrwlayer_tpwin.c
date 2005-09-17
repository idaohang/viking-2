/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <gtk/gtk.h>
#include <time.h>

#include "coords.h"
#include "vikcoord.h"
#include "viktrack.h"
#include "viktrwlayer_tpwin.h"
#include "vikwaypoint.h"
#include "dialog.h"

#define SET_BUTTON_SENSITIVE(tpwin,num,sens) gtk_widget_set_sensitive ( GTK_WIDGET(g_list_nth_data((tpwin->buttons),(num))), (sens));

struct _VikTrwLayerTpwin {
  GtkDialog parent;
  GtkSpinButton *lat, *lon, *alt;
  GtkLabel *track_name, *ts, *localtime, *diff_dist, *diff_time, *diff_speed;
  GList *buttons;
  VikTrackpoint *cur_tp;
  gboolean cur_tp_is_endpoint; /* for join */

  gboolean sync_to_tp_block;
};

GType vik_trw_layer_tpwin_get_type (void)
{
  static GType tpwin_type = 0;

  if (!tpwin_type)
  {
    static const GTypeInfo tpwin_info = 
    {
      sizeof (VikTrwLayerTpwinClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      NULL, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (VikTrwLayerTpwin),
      0,
      NULL /* instance init */
    };
    tpwin_type = g_type_register_static ( GTK_TYPE_DIALOG, "VikTrwLayerTpwin", &tpwin_info, 0 );
  }

  return tpwin_type;
}

static void tpwin_sync_ll_to_tp ( VikTrwLayerTpwin *tpwin )
{
  if ( tpwin->cur_tp && (!tpwin->sync_to_tp_block) )
  {
    struct LatLon ll;
    VikCoord coord;
    ll.lat = gtk_spin_button_get_value ( tpwin->lat );
    ll.lon = gtk_spin_button_get_value ( tpwin->lon );
    vik_coord_load_from_latlon ( &coord, tpwin->cur_tp->coord.mode, &ll );

    /* don't redraw unless we really have to */
    if ( vik_coord_diff(&(tpwin->cur_tp->coord), &coord) > 0.05 ) /* may not be exact due to rounding */
    {
      tpwin->cur_tp->coord = coord;
      gtk_dialog_response ( GTK_DIALOG(tpwin), VIK_TRW_LAYER_TPWIN_DATA_CHANGED );
    }
  }
}

static void tpwin_sync_alt_to_tp ( VikTrwLayerTpwin *tpwin )
{
  if ( tpwin->cur_tp && (!tpwin->sync_to_tp_block) )
    tpwin->cur_tp->altitude = gtk_spin_button_get_value ( tpwin->alt );
}

VikTrwLayerTpwin *vik_trw_layer_tpwin_new ( GtkWindow *parent )
{
  static gchar *left_label_texts[] = { "<b>Part of Track:</b>", "<b>Latitude:</b>",
                                      "<b>Longitude:</b>", "<b>Altitude:</b>", "<b>Timestamp:</b>", "<b>Time:</b>" };
  static gchar *right_label_texts[] = { "<b>Distance Difference:</b>",
                                      "<b>Time Difference:</b>", "<b>\"Speed\" Between:</b>" };

  VikTrwLayerTpwin *tpwin = VIK_TRW_LAYER_TPWIN ( g_object_new ( VIK_TRW_LAYER_TPWIN_TYPE, NULL ) );
  GtkWidget *main_hbox, *left_vbox, *right_vbox;
  GtkWidget *diff_left_vbox, *diff_right_vbox;
  

  gtk_window_set_transient_for ( GTK_WINDOW(tpwin), parent );
  gtk_window_set_title ( GTK_WINDOW(tpwin), "Trackpoint" );

  gtk_dialog_add_buttons ( GTK_DIALOG(tpwin),
      GTK_STOCK_CLOSE, VIK_TRW_LAYER_TPWIN_CLOSE,
      GTK_STOCK_DELETE, VIK_TRW_LAYER_TPWIN_DELETE,
      "Split Here", VIK_TRW_LAYER_TPWIN_SPLIT,
      "Join With Last", VIK_TRW_LAYER_TPWIN_JOIN,
      GTK_STOCK_GO_BACK, VIK_TRW_LAYER_TPWIN_BACK,
      GTK_STOCK_GO_FORWARD, VIK_TRW_LAYER_TPWIN_FORWARD,
      NULL );
  tpwin->buttons = gtk_container_get_children(GTK_CONTAINER(GTK_DIALOG(tpwin)->action_area));

  /* main track info */
  left_vbox = a_dialog_create_label_vbox ( left_label_texts, sizeof(left_label_texts) / sizeof(left_label_texts[0]) );

  tpwin->track_name = GTK_LABEL(gtk_label_new(NULL));
  tpwin->ts = GTK_LABEL(gtk_label_new(NULL));
  tpwin->localtime = GTK_LABEL(gtk_label_new(NULL));

  tpwin->lat = GTK_SPIN_BUTTON(gtk_spin_button_new( GTK_ADJUSTMENT(gtk_adjustment_new (
                                 0, -90, 90, 0.00005, 0.01, 0 )), 0.00005, 6));
  tpwin->lon = GTK_SPIN_BUTTON(gtk_spin_button_new( GTK_ADJUSTMENT(gtk_adjustment_new (
                                 0, -180, 180, 0.00005, 0.01, 0 )), 0.00005, 6));

  g_signal_connect_swapped ( G_OBJECT(tpwin->lat), "value-changed", G_CALLBACK(tpwin_sync_ll_to_tp), tpwin );
  g_signal_connect_swapped ( G_OBJECT(tpwin->lon), "value-changed", G_CALLBACK(tpwin_sync_ll_to_tp), tpwin );

  tpwin->alt = GTK_SPIN_BUTTON(gtk_spin_button_new( GTK_ADJUSTMENT(gtk_adjustment_new (
                                 0, -1000, 25000, 10, 100, 0 )), 10, 2));

  g_signal_connect_swapped ( G_OBJECT(tpwin->alt), "value-changed", G_CALLBACK(tpwin_sync_alt_to_tp), tpwin );

  right_vbox = gtk_vbox_new( TRUE, 3 );
  gtk_box_pack_start ( GTK_BOX(right_vbox), GTK_WIDGET(tpwin->track_name), FALSE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(right_vbox), GTK_WIDGET(tpwin->lat), FALSE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(right_vbox), GTK_WIDGET(tpwin->lon), FALSE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(right_vbox), GTK_WIDGET(tpwin->alt), FALSE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(right_vbox), GTK_WIDGET(tpwin->ts), FALSE, TRUE, 5 );
  gtk_box_pack_start ( GTK_BOX(right_vbox), GTK_WIDGET(tpwin->localtime), FALSE, TRUE, 5 );

  /* diff info */
  diff_left_vbox = a_dialog_create_label_vbox ( right_label_texts, sizeof(right_label_texts) / sizeof(right_label_texts[0]) );

  tpwin->diff_dist = GTK_LABEL(gtk_label_new(NULL));
  tpwin->diff_time = GTK_LABEL(gtk_label_new(NULL));
  tpwin->diff_speed = GTK_LABEL(gtk_label_new(NULL));

  diff_right_vbox = gtk_vbox_new ( TRUE, 3 );
  gtk_box_pack_start ( GTK_BOX(diff_right_vbox), GTK_WIDGET(tpwin->diff_dist), FALSE, TRUE, 5 );
  gtk_box_pack_start ( GTK_BOX(diff_right_vbox), GTK_WIDGET(tpwin->diff_time), FALSE, TRUE, 5 );
  gtk_box_pack_start ( GTK_BOX(diff_right_vbox), GTK_WIDGET(tpwin->diff_speed), FALSE, TRUE, 5 );

  main_hbox = gtk_hbox_new( TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(main_hbox), left_vbox, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(main_hbox), right_vbox, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(main_hbox), diff_left_vbox, TRUE, TRUE, 0 );
  gtk_box_pack_start ( GTK_BOX(main_hbox), diff_right_vbox, TRUE, TRUE, 0 );

  gtk_box_pack_start ( GTK_BOX(GTK_DIALOG(tpwin)->vbox), main_hbox, FALSE, FALSE, 0 );

  tpwin->cur_tp = NULL;

  return tpwin;
}

void vik_trw_layer_tpwin_set_empty ( VikTrwLayerTpwin *tpwin )
{
  gtk_label_set_text ( tpwin->track_name, NULL );
  gtk_label_set_text ( tpwin->ts, NULL );
  gtk_label_set_text ( tpwin->localtime, NULL );

  gtk_widget_set_sensitive ( GTK_WIDGET(tpwin->lat), FALSE );
  gtk_widget_set_sensitive ( GTK_WIDGET(tpwin->lon), FALSE );
  gtk_widget_set_sensitive ( GTK_WIDGET(tpwin->alt), FALSE );

  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_SPLIT, FALSE );
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_DELETE, FALSE );
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_FORWARD, FALSE );
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_BACK, FALSE );
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_JOIN, FALSE );
  gtk_label_set_text ( tpwin->diff_dist, NULL );
  gtk_label_set_text ( tpwin->diff_time, NULL );
  gtk_label_set_text ( tpwin->diff_speed, NULL );
  tpwin->cur_tp = NULL;
}

void vik_trw_layer_tpwin_disable_join ( VikTrwLayerTpwin *tpwin )
{
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_JOIN, FALSE );
}

void vik_trw_layer_tpwin_set_tp ( VikTrwLayerTpwin *tpwin, GList *tpl, gchar *track_name )
{
  static char tmp_str[25];
  static struct LatLon ll;
  VikTrackpoint *tp = VIK_TRACKPOINT(tpl->data);

  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_DELETE, TRUE );

  /* We can only split up a track if it's not an endpoint. Makes sense to me. */
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_SPLIT, tpl->next && tpl->prev );

  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_FORWARD, (gboolean) tpl->next );
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_BACK, (gboolean) tpl->prev );

  /* we can only join tracks if there was a last tp, the last tp was an endpoint, _AND_ this tp is an endpoint */
  SET_BUTTON_SENSITIVE ( tpwin, VIK_TRW_LAYER_TPWIN_JOIN, tpwin->cur_tp && tpwin->cur_tp_is_endpoint && (!(tpl->next && tpl->prev)) );

  gtk_widget_set_sensitive ( GTK_WIDGET(tpwin->lat), TRUE );
  gtk_widget_set_sensitive ( GTK_WIDGET(tpwin->lon), TRUE );

  gtk_label_set_text ( tpwin->track_name, track_name );

  tpwin->sync_to_tp_block = TRUE; /* don't update while setting data. */

  vik_coord_to_latlon ( &(tp->coord), &ll );
  gtk_spin_button_set_value ( tpwin->lat, ll.lat );
  gtk_spin_button_set_value ( tpwin->lon, ll.lon );
  gtk_spin_button_set_value ( tpwin->alt, tp->altitude );

  tpwin->sync_to_tp_block = FALSE; /* don't update whlie setting data. */


  if ( tp->has_timestamp )
  {
    g_snprintf ( tmp_str, sizeof(tmp_str), "%ld", tp->timestamp );
    gtk_label_set_text ( tpwin->ts, tmp_str );
    g_snprintf ( tmp_str, MIN(25,sizeof(tmp_str)), "%s", ctime(&(tp->timestamp)) );
    /* max. len of 25 will snip off newline, which is good since it messes stuff up */
    gtk_label_set_text ( tpwin->localtime, tmp_str );
  }
  else
  {
    gtk_label_set_text (tpwin->ts, NULL );
    gtk_label_set_text (tpwin->localtime, NULL );
  }

  if ( tpwin->cur_tp )
  {
    g_snprintf ( tmp_str, sizeof(tmp_str), "%.3f m", vik_coord_diff(&(tp->coord), &(tpwin->cur_tp->coord)));
    gtk_label_set_text ( tpwin->diff_dist, tmp_str );
    if ( tp->has_timestamp && tpwin->cur_tp->has_timestamp )
    {
      g_snprintf ( tmp_str, sizeof(tmp_str), "%ld s", tp->timestamp - tpwin->cur_tp->timestamp);
      gtk_label_set_text ( tpwin->diff_time, tmp_str );
      if ( tp->timestamp == tpwin->cur_tp->timestamp )
        gtk_label_set_text ( tpwin->diff_speed, "--" );
      else
      {
        g_snprintf ( tmp_str, sizeof(tmp_str), "%.2f m/s", vik_coord_diff(&(tp->coord), &(tpwin->cur_tp->coord)) / ABS(tp->timestamp - tpwin->cur_tp->timestamp) );
        gtk_label_set_text ( tpwin->diff_speed, tmp_str );
      }
    }
    else
    {
      gtk_label_set_text ( tpwin->diff_time, NULL );
      gtk_label_set_text ( tpwin->diff_speed, NULL );
    }
  }


  tpwin->cur_tp = tp;
  tpwin->cur_tp_is_endpoint = ! (tpl->next && tpl->prev);
}

void vik_trw_layer_tpwin_set_track_name ( VikTrwLayerTpwin *tpwin, const gchar *track_name )
{
  gtk_label_set_text ( tpwin->track_name, track_name );
}