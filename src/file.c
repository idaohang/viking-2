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

#include "viking.h"

#include "gpx.h"

#include <string.h>
#include <stdlib.h>

#define TEST_BOOLEAN(str) (! ((str)[0] == '\0' || (str)[0] == '0' || (str)[0] == 'n' || (str)[0] == 'N' || (str)[0] == 'f' || (str)[0] == 'F') )
#define VIK_MAGIC "#VIK"
#define GPX_MAGIC "<?xm"
#define VIK_MAGIC_LEN 4

#ifdef WINDOWS
#define FILE_SEP '\\'
#else
#define FILE_SEP '/'
#endif

typedef struct _Stack Stack;

struct _Stack {
  Stack *under;
  gpointer *data;
};

static void pop(Stack **stack) {
  Stack *tmp = (*stack)->under;
  g_free ( *stack );
  *stack = tmp;
}

static void push(Stack **stack)
{
  Stack *tmp = g_malloc ( sizeof ( Stack ) );
  tmp->under = *stack;
  *stack = tmp;
}

static gboolean check_magic ( FILE *f, const gchar *magic_number )
{
  gchar magic[VIK_MAGIC_LEN];
  gboolean rv = FALSE;
  gint8 i;
  if ( fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
      strncmp(magic, magic_number, sizeof(magic)) == 0 )
    rv = TRUE;
  for ( i = sizeof(magic)-1; i >= 0; i-- ) /* the ol' pushback */
    ungetc(magic[i],f);
  return rv;
}


static gboolean str_starts_with ( const gchar *haystack, const gchar *needle, guint16 len_needle, gboolean must_be_longer )
{
  if ( strlen(haystack) > len_needle - (!must_be_longer) && strncasecmp ( haystack, needle, len_needle ) == 0 )
    return TRUE;
  return FALSE;
}

static guint16 layer_type_from_string ( const gchar *str )
{
  guint8 i;
  for ( i = 0; i < VIK_LAYER_NUM_TYPES; i++ )
    if ( strcasecmp ( str, vik_layer_get_interface(i)->name ) == 0 )
      return i;
  return -1;
}

static void write_layer_params_and_data ( VikLayer *l, FILE *f )
{
  VikLayerParam *params = vik_layer_get_interface(l->type)->params;
  VikLayerFuncGetParam get_param = vik_layer_get_interface(l->type)->get_param;

  fprintf ( f, "name=%s\n", l->name ? l->name : "" );
  if ( !l->visible )
    fprintf ( f, "visible=f\n" );

  if ( params && get_param )
  {
    VikLayerParamData data;
    guint16 i, params_count = vik_layer_get_interface(l->type)->params_count;
    for ( i = 0; i < params_count; i++ )
    {
      data = get_param(l,i);
      fprintf ( f, "%s=", params[i].name );
      switch ( params[i].type )
      {
        case VIK_LAYER_PARAM_DOUBLE: fprintf ( f, "%f\n", data.d ); break;
        case VIK_LAYER_PARAM_UINT: fprintf ( f, "%d\n", data.u ); break;
        case VIK_LAYER_PARAM_INT: fprintf ( f, "%d\n", data.i ); break;
        case VIK_LAYER_PARAM_BOOLEAN: fprintf ( f, "%c\n", data.b ? 't' : 'f' ); break;
        case VIK_LAYER_PARAM_STRING: fprintf ( f, "%s\n", data.s ); break;
        case VIK_LAYER_PARAM_COLOR: fprintf ( f, "#%.2x%.2x%.2x\n", (int)(data.c.red/256),(int)(data.c.green/256),(int)(data.c.blue/256)); break;
      }
    }
  }
  if ( vik_layer_get_interface(l->type)->write_file_data )
  {
    fprintf ( f, "\n\n~LayerData\n" );
    vik_layer_get_interface(l->type)->write_file_data ( l, f );
    fprintf ( f, "~EndLayerData\n" );
  }
  /* foreach param:
     write param, and get_value, etc.
     then run layer data, and that's it.
  */
}

static void file_write ( VikAggregateLayer *top, FILE *f, gpointer vp )
{
  Stack *stack = NULL;
  VikLayer *current_layer;
  struct LatLon ll;
  gchar *modestring;

  push(&stack);
  stack->data = (gpointer) vik_aggregate_layer_get_children(VIK_AGGREGATE_LAYER(top));
  stack->under = NULL;

  /* crazhy CRAZHY */
  vik_coord_to_latlon ( vik_viewport_get_center ( VIK_VIEWPORT(vp) ), &ll );

  switch ( vik_viewport_get_drawmode ( VIK_VIEWPORT(vp) ) ) {
    case VIK_VIEWPORT_DRAWMODE_UTM: modestring = "utm"; break;
    case VIK_VIEWPORT_DRAWMODE_EXPEDIA: modestring = "expedia"; break;
    case VIK_VIEWPORT_DRAWMODE_GOOGLE: modestring = "google"; break;
    case VIK_VIEWPORT_DRAWMODE_KH: modestring = "kh"; break;
    default: modestring = "mercator";
  }

  fprintf ( f, "#VIKING GPS Data file http://gpsmaps.org/viking/\n\nxmpp=%f\nympp=%f\nlat=%f\nlon=%f\nmode=%s\ncolor=%s\n",
      vik_viewport_get_xmpp ( VIK_VIEWPORT(vp) ), vik_viewport_get_ympp ( VIK_VIEWPORT(vp) ), ll.lat, ll.lon,
      modestring, vik_viewport_get_background_color(VIK_VIEWPORT(vp)) );

  if ( ! VIK_LAYER(top)->visible )
    fprintf ( f, "visible=f\n" );

  while (stack && stack->data)
  {
    current_layer = VIK_LAYER(((GList *)stack->data)->data);
    fprintf ( f, "\n~Layer %s\n", vik_layer_get_interface(current_layer->type)->name );
    write_layer_params_and_data ( current_layer, f );
    if ( current_layer->type == VIK_LAYER_AGGREGATE && !vik_aggregate_layer_is_empty(VIK_AGGREGATE_LAYER(current_layer)) )
    {
      push(&stack);
      stack->data = (gpointer) vik_aggregate_layer_get_children(VIK_AGGREGATE_LAYER(current_layer));
    }
    else
    {
      stack->data = (gpointer) ((GList *)stack->data)->next;
      fprintf ( f, "~EndLayer\n\n" );
      while ( stack && (!stack->data) )
      {
        pop(&stack);
        if ( stack )
        {
          stack->data = (gpointer) ((GList *)stack->data)->next;
          fprintf ( f, "~EndLayer\n\n" );
        }
      }
    }
  }
/*
  get vikaggregatelayer's children (?)
  foreach write ALL params,
  then layer data (IF function exists)
  then endlayer

  impl:
  stack of layers (LIST) we are working on
  when layer->next == NULL ...
  we move on.
*/
}

static void file_read ( VikAggregateLayer *top, FILE *f, gpointer vp )
{
  Stack *stack;
  struct LatLon ll = { 0.0, 0.0 };
  gchar buffer[4096];
  gchar *line;
  guint16 len;
  long line_num = 0;

  VikLayerParam *params; /* for current layer, so we don't have to keep on looking up interface */
  guint8 params_count;

  push(&stack);
  stack->under = NULL;
  stack->data = (gpointer) top;

  while ( fgets ( buffer, 4096, f ) )
  {
    line_num++;

    line = buffer;
    while ( *line == ' ' || *line =='\t' )
      line++;

    if ( line[0] == '#' )
      continue;
    

    len = strlen(line);
    if ( len > 0 && line[len-1] == '\n' )
      line[--len] = '\0';
    if ( len > 0 && line[len-1] == '\r' )
      line[--len] = '\0';

    if ( len == 0 )
      continue;


    if ( line[0] == '~' )
    {
      line++; len--;
      if ( *line == '\0' )
        continue;
      else if ( str_starts_with ( line, "Layer ", 6, TRUE ) )
      {
        if ( ( ! stack->data ) || VIK_LAYER(stack->data)->type != VIK_LAYER_AGGREGATE )
        {
          g_warning ( "Line %ld: Layer command inside non-Aggregate Layer", line_num );
          push(&stack); /* inside INVALID layer */
          stack->data = NULL;
          continue;
        }
        else
        {
          gint16 type = layer_type_from_string ( line+6 );
          push(&stack);
          if ( type == -1 )
          {
            g_warning ( "Line %ld: Unknown type %s\n", line_num, line+6 );
            stack->data = NULL;
          }
          else
          {
            stack->data = (gpointer) vik_layer_create ( type, vp, NULL, FALSE );
            params = vik_layer_get_interface(type)->params;
            params_count = vik_layer_get_interface(type)->params_count;
          }
        }
      }
      else if ( str_starts_with ( line, "EndLayer", 8, FALSE ) )
      {
        if ( stack->under == NULL )
          g_warning ( "Line %ld: Mismatched ~EndLayer command", line_num );
        else
        {
          if ( stack->data && stack->under->data )
          {
            vik_aggregate_layer_add_layer ( VIK_AGGREGATE_LAYER(stack->under->data), VIK_LAYER(stack->data) );
            vik_layer_post_read ( VIK_LAYER(stack->data), vp );
          }
          pop(&stack);
        }
      }
      else if ( str_starts_with ( line, "LayerData", 9, FALSE ) )
      {
        if ( stack->data && vik_layer_get_interface(VIK_LAYER(stack->data)->type)->read_file_data )
          vik_layer_get_interface(VIK_LAYER(stack->data)->type)->read_file_data ( VIK_LAYER(stack->data), f );
          /* must read until hits ~EndLayerData */

        else
        { /* simply skip layer data over */
          while ( fgets ( buffer, 4096, f ) )
          {
            line_num++;

            line = buffer;

            len = strlen(line);
            if ( len > 0 && line[len-1] == '\n' )
              line[--len] = '\0';
            if ( len > 0 && line[len-1] == '\r' )
              line[--len] = '\0';
            if ( strcasecmp ( line, "~EndLayerData" ) == 0 )
              break;
          }
          continue;
        }
      }
      else
      {
        g_warning ( "Line %ld: Unknown tilde command", line_num );
      }
    }
    else
    {
      gint32 eq_pos = -1;
      guint16 i;
      if ( ! stack->data )
        continue;

      for ( i = 0; i < len; i++ )
        if ( line[i] == '=' )
          eq_pos = i;

      if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "xmpp", eq_pos ) == 0) /* "hard coded" params: global & for all layer-types */
        vik_viewport_set_xmpp ( VIK_VIEWPORT(vp), strtod ( line+5, NULL ) );
      else if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "ympp", eq_pos ) == 0)
        vik_viewport_set_ympp ( VIK_VIEWPORT(vp), strtod ( line+5, NULL ) );
      else if ( stack->under == NULL && eq_pos == 3 && strncasecmp ( line, "lat", eq_pos ) == 0 )
        ll.lat = strtod ( line+4, NULL );
      else if ( stack->under == NULL && eq_pos == 3 && strncasecmp ( line, "lon", eq_pos ) == 0 )
        ll.lon = strtod ( line+4, NULL );
      else if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "mode", eq_pos ) == 0 && strcasecmp ( line+5, "utm" ) == 0)
        vik_viewport_set_drawmode ( VIK_VIEWPORT(vp), VIK_VIEWPORT_DRAWMODE_UTM);
      else if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "mode", eq_pos ) == 0 && strcasecmp ( line+5, "expedia" ) == 0)
        vik_viewport_set_drawmode ( VIK_VIEWPORT(vp), VIK_VIEWPORT_DRAWMODE_EXPEDIA );
      else if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "mode", eq_pos ) == 0 && strcasecmp ( line+5, "google" ) == 0)
        vik_viewport_set_drawmode ( VIK_VIEWPORT(vp), VIK_VIEWPORT_DRAWMODE_GOOGLE );
      else if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "mode", eq_pos ) == 0 && strcasecmp ( line+5, "kh" ) == 0)
        vik_viewport_set_drawmode ( VIK_VIEWPORT(vp), VIK_VIEWPORT_DRAWMODE_KH );
      else if ( stack->under == NULL && eq_pos == 4 && strncasecmp ( line, "mode", eq_pos ) == 0 && strcasecmp ( line+5, "mercator" ) == 0)
        vik_viewport_set_drawmode ( VIK_VIEWPORT(vp), VIK_VIEWPORT_DRAWMODE_MERCATOR );
      else if ( stack->under == NULL && eq_pos == 5 && strncasecmp ( line, "color", eq_pos ) == 0 )
        vik_viewport_set_background_color ( VIK_VIEWPORT(vp), line+6 );
      else if ( stack->under && eq_pos == 4 && strncasecmp ( line, "name", eq_pos ) == 0 )
        vik_layer_rename ( VIK_LAYER(stack->data), line+5 );
      else if ( eq_pos == 7 && strncasecmp ( line, "visible", eq_pos ) == 0 )
        VIK_LAYER(stack->data)->visible = TEST_BOOLEAN(line+8);
      else if ( eq_pos != -1 && stack->under )
      {
        gboolean found_match = FALSE;
        /* go thru layer params. if len == eq_pos && starts_with jazz, set it. */
        /* also got to check for name and visible. */

        if ( ! params )
        {
          g_warning ( "Line %ld: No options for this kind of layer", line_num );
          continue;
        }

        for ( i = 0; i < params_count; i++ )
          if ( strlen(params[i].name) == eq_pos && strncasecmp ( line, params[i].name, eq_pos ) == 0 )
          {
            VikLayerParamData x;
            line += eq_pos+1;
            switch ( params[i].type )
            {
              case VIK_LAYER_PARAM_DOUBLE: x.d = strtod(line, NULL); break;
              case VIK_LAYER_PARAM_UINT: x.u = strtoul(line, NULL, 10); break;
              case VIK_LAYER_PARAM_INT: x.i = strtol(line, NULL, 10); break;
              case VIK_LAYER_PARAM_BOOLEAN: x.b = TEST_BOOLEAN(line); break;
              case VIK_LAYER_PARAM_COLOR: memset(&(x.c), 0, sizeof(x.c)); /* default: black */
                                          gdk_color_parse ( line, &(x.c) ); break;
              default: x.s = line;
            }
            vik_layer_set_param ( VIK_LAYER(stack->data), i, x, vp );
            found_match = TRUE;
            break;
          }
        if ( ! found_match )
          g_warning ( "Line %ld: Unknown parameter", line_num );
      }
      else
        g_warning ( "Line %ld: Invalid parameter or parameter outside of layer.", line_num );
    }
/* could be:
[Layer Type=Bla]
[EndLayer]
[LayerData]
name=this
#comment
*/
  }

  while ( stack )
  {
    if ( stack->under && stack->under->data && stack->data )
    {
      vik_aggregate_layer_add_layer ( VIK_AGGREGATE_LAYER(stack->under->data), VIK_LAYER(stack->data) );
      vik_layer_post_read ( VIK_LAYER(stack->data), vp );
    }
    pop(&stack);
  }

  if ( ll.lat != 0.0 || ll.lon != 0.0 )
    vik_viewport_set_center_latlon ( VIK_VIEWPORT(vp), &ll );

  if ( ( ! VIK_LAYER(top)->visible ) && VIK_LAYER(top)->realized )
    vik_treeview_item_set_visible ( VIK_LAYER(top)->vt, &(VIK_LAYER(top)->iter), FALSE ); 
}

/*
read thru file
if "[Layer Type="
  push(&stack)
  new default layer of type (str_to_type) (check interface->name)
if "[EndLayer]"
  VikLayer *vl = stack->data;
  pop(&stack);
  vik_aggregate_layer_add_layer(stack->data, vl);
if "[LayerData]"
  vik_layer_data ( VIK_LAYER_DATA(stack->data), f, vp );

*/

/* ---------------------------------------------------- */

static FILE *xfopen ( const char *fn, const char *mode )
{
  if ( strcmp(fn,"-") == 0 )
    return stdin;
  else
    return fopen(fn, "r");
}

static void xfclose ( FILE *f )
{
  if ( f != stdin && f != stdout )
    fclose ( f );
}

/* 0 on failure, 1 on success (vik file) 2 on success (other file) */
gshort a_file_load ( VikAggregateLayer *top, gpointer vp, const gchar *filename )
{
  FILE *f = xfopen ( filename, "r" );

  g_assert ( vp );

  if ( ! f )
    return 0;

  if ( check_magic ( f, VIK_MAGIC ) )
  {
    file_read ( top, f, vp );
    if ( f != stdin )
      xfclose(f);
    return 1;
  }
  else
  {
    VikCoord new_center;
    VikLayer *vtl = vik_layer_create ( VIK_LAYER_TRW, vp, NULL, FALSE );
    vik_layer_rename ( vtl, a_file_basename ( filename ) );

    if ( check_magic ( f, GPX_MAGIC ) )
      a_gpx_read_file ( VIK_TRW_LAYER(vtl), f );
    else
     a_gpspoint_read_file ( VIK_TRW_LAYER(vtl), f );

    vik_layer_post_read ( vtl, vp );

    vik_aggregate_layer_add_layer ( top, vtl );

    if ( vik_trw_layer_find_center ( VIK_TRW_LAYER(vtl), &new_center ) )
      vik_viewport_set_center_coord ( VIK_VIEWPORT(vp), &new_center );
    xfclose(f);
    return 2;
  }
}

gboolean a_file_save ( VikAggregateLayer *top, gpointer vp, const gchar *filename )
{
  FILE *f = fopen(filename, "w");

  if ( ! f )
    return FALSE;

  file_write ( top, f, vp );

  fclose(f);

  return TRUE;
}


const gchar *a_file_basename ( const gchar *filename )
{
  const gchar *t = filename + strlen(filename) - 1;
  while ( --t > filename )
    if ( *(t-1) == FILE_SEP )
      break;
  if ( t >= filename )
    return t;
  return filename;
}

gboolean a_file_export ( VikTrwLayer *vtl, const gchar *filename, gshort file_type )
{
  FILE *f = fopen ( filename, "w" );
  if ( f )
  {
    if ( file_type == FILE_TYPE_GPSMAPPER )
      a_gpsmapper_write_file ( vtl, f );
    else if ( file_type == FILE_TYPE_GPX )
      a_gpx_write_file ( vtl, f );
    else
      a_gpspoint_write_file ( vtl, f );
    fclose ( f );
    return TRUE;
  }
  return FALSE;
}