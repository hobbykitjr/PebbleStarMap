/**
 * Pixel Stars — Compass Star Map for Pebble Round 2
 * Target: gabbro (260x260, round, compass)
 *
 * Planisphere-style star chart. Center = zenith, edge = horizon.
 * Rotates with compass heading. Shows bright stars, constellations,
 * and cardinal directions. Time-aware (stars move with Earth's rotation).
 */

#include <pebble.h>
#include <stdlib.h>

// ============================================================================
// MATH HELPERS
// ============================================================================
#define PI 3.14159265f
#define DEG2RAD(d) ((d)*PI/180.0f)
#define RAD2DEG(r) ((r)*180.0f/PI)
#define HOURS2RAD(h) ((h)*PI/12.0f)

// Fixed-point trig using Pebble's lookup tables
static float psin(float deg) { return (float)sin_lookup(DEG_TO_TRIGANGLE((int)deg)) / (float)TRIG_MAX_RATIO; }
static float pcos(float deg) { return (float)cos_lookup(DEG_TO_TRIGANGLE((int)deg)) / (float)TRIG_MAX_RATIO; }

// Safe asin/acos approximations using atan2_lookup
static float pasin(float x) {
  if(x > 1.0f) x = 1.0f;
  if(x < -1.0f) x = -1.0f;
  // asin(x) = atan2(x, sqrt(1-x*x))
  float y = 1.0f - x*x;
  if(y < 0) y = 0;
  // Simple sqrt approximation (Newton's method, 3 iterations)
  float s = (y > 0) ? y : 0.001f;
  s = 0.5f * (s + y/s);
  s = 0.5f * (s + y/s);
  s = 0.5f * (s + y/s);
  int32_t angle = atan2_lookup((int)(x*TRIG_MAX_RATIO), (int)(s*TRIG_MAX_RATIO));
  return (float)angle * 360.0f / (float)TRIG_MAX_ANGLE;
}

static float pacos(float x) {
  return 90.0f - pasin(x);
}

// ============================================================================
// STAR CATALOG
// ============================================================================
typedef struct {
  float ra_h;    // Right ascension in hours (0-24)
  float dec_d;   // Declination in degrees (-90 to +90)
  int mag;       // Magnitude class: 0=brightest, 1=bright, 2=medium
  const char *name;
} Star;

// Constellation lines: pairs of star indices
typedef struct { int a, b; } ConLine;

// ~30 bright stars visible from mid-northern latitudes
static const Star s_stars[] = {
  // Name stars (bright, labeled)
  { 2.53,  89.26, 1, "Polaris"},     // 0
  { 6.75, -16.72, 0, "Sirius"},      // 1
  {14.26,  19.18, 0, "Arcturus"},     // 2
  {18.62,  38.78, 0, "Vega"},         // 3
  { 5.28,  46.00, 0, "Capella"},      // 4
  { 5.24,  -8.20, 0, "Rigel"},        // 5
  { 7.66,   5.23, 0, "Procyon"},      // 6
  { 5.92,   7.41, 0, "Betelgeuse"},   // 7
  {19.85,   8.87, 0, "Altair"},       // 8
  { 4.60,  16.51, 1, "Aldebaran"},    // 9
  {16.49, -26.43, 0, "Antares"},      // 10
  {13.42, -11.16, 1, "Spica"},        // 11
  { 7.76,  28.03, 1, "Pollux"},       // 12
  {20.69,  45.28, 0, "Deneb"},        // 13
  {10.14,  11.97, 1, "Regulus"},       // 14
  // Big Dipper
  {11.06,  61.75, 1, NULL},  // 15 Dubhe
  {11.03,  56.38, 1, NULL},  // 16 Merak
  {11.90,  53.69, 2, NULL},  // 17 Phecda
  {12.26,  57.03, 2, NULL},  // 18 Megrez
  {12.90,  55.96, 1, NULL},  // 19 Alioth
  {13.40,  54.93, 1, NULL},  // 20 Mizar
  {13.79,  49.31, 1, NULL},  // 21 Alkaid
  // Orion belt
  { 5.42,   6.35, 1, NULL},  // 22 Bellatrix
  { 5.53,  -0.30, 1, NULL},  // 23 Mintaka
  { 5.60,  -1.20, 1, NULL},  // 24 Alnilam
  { 5.68,  -1.94, 1, NULL},  // 25 Alnitak
  { 5.80,  -9.67, 1, NULL},  // 26 Saiph
  // Cassiopeia
  { 0.15,  59.15, 1, NULL},  // 27 Caph
  { 0.68,  56.54, 1, NULL},  // 28 Schedar
  { 0.95,  60.72, 1, NULL},  // 29 Navi
  { 1.36,  60.24, 2, NULL},  // 30 Ruchbah
  { 1.91,  63.67, 2, NULL},  // 31 Segin
};
#define NUM_STARS (int)(sizeof(s_stars)/sizeof(s_stars[0]))

// Constellation lines
static const ConLine s_lines[] = {
  // Big Dipper (bowl + handle)
  {15,16},{16,17},{17,18},{18,15},  // Bowl
  {18,19},{19,20},{20,21},          // Handle
  // Orion
  {4,22},{22,23},{23,7},            // Shoulders to belt-ish
  {7,25},{25,26},{26,5},            // Belt area to feet
  {5,23},{23,24},{24,25},           // Belt
  // Cassiopeia (W shape)
  {27,28},{28,29},{29,30},{30,31},
  // Summer Triangle
  {3,13},{13,8},{8,3},
};
#define NUM_LINES (int)(sizeof(s_lines)/sizeof(s_lines[0]))

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_win;
static Layer *s_canvas;
static float s_heading = 0;        // Compass heading in degrees
static bool s_compass_ok = false;
static float s_lat = 40.5;         // Observer latitude (PA area default)
static float s_lon = -76.0;       // Observer longitude
static bool s_show_names = true;   // Show star names
static int s_zoom = 0;            // 0=full sky, 1=2x, 2=4x
static const float s_zoom_fov[] = {90.0f, 45.0f, 22.5f};  // Degrees from center to edge

// ============================================================================
// ASTRONOMY
// ============================================================================

// Compute Local Sidereal Time in degrees
static float local_sidereal_time(float lon_deg) {
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if(!lt) return 0;

  // Estimate UTC offset from longitude (rough: 1 hour per 15°)
  int utc_off = -(int)(lon_deg / 15.0f);
  int y=lt->tm_year+1900, m=lt->tm_mon+1, d=lt->tm_mday;
  int h=lt->tm_hour+utc_off, mn=lt->tm_min, s=lt->tm_sec;
  while(h>=24) { h-=24; d++; }
  while(h<0) { h+=24; d--; }
  float ut = h + mn/60.0f + s/3600.0f;

  // Days since J2000.0
  float jd = 367*y - (int)(7*(y+(int)((m+9)/12))/4) + (int)(275*m/9) + d + 1721013.5f + ut/24.0f;
  float d0 = jd - 2451545.0f;

  // Greenwich Sidereal Time in degrees
  float gst = 280.46061837f + 360.98564736629f * d0;
  // Normalize
  gst = gst - (int)(gst/360.0f)*360.0f;
  if(gst < 0) gst += 360.0f;

  // Local Sidereal Time
  float lst = gst + lon_deg;
  lst = lst - (int)(lst/360.0f)*360.0f;
  if(lst < 0) lst += 360.0f;
  return lst;
}

// Convert RA/Dec to Altitude/Azimuth
static void radec_to_altaz(float ra_h, float dec_d, float lst_deg, float lat_d,
                           float *alt_out, float *az_out) {
  float ha = lst_deg - ra_h * 15.0f;  // Hour angle in degrees
  float ha_r = DEG2RAD(ha);
  float dec_r = DEG2RAD(dec_d);
  float lat_r = DEG2RAD(lat_d);

  // Altitude
  float sin_alt = psin(dec_d)*psin(lat_d) + pcos(dec_d)*pcos(lat_d)*pcos(ha);
  // Clamp
  if(sin_alt > 1.0f) sin_alt = 1.0f;
  if(sin_alt < -1.0f) sin_alt = -1.0f;
  float alt = pasin(sin_alt);

  // Azimuth
  float cos_az = (psin(dec_d) - psin(alt)*psin(lat_d)) / (pcos(alt)*pcos(lat_d) + 0.0001f);
  if(cos_az > 1.0f) cos_az = 1.0f;
  if(cos_az < -1.0f) cos_az = -1.0f;
  float az = pacos(cos_az);
  if(psin(ha) > 0) az = 360.0f - az;

  *alt_out = alt;
  *az_out = az;
}

// Project alt/az to screen coordinates (stereographic from zenith)
static bool project(float alt, float az, float heading, int cx, int cy, int radius,
                    int *sx, int *sy) {
  if(alt < -5) return false;  // Below horizon

  float fov = s_zoom_fov[s_zoom];

  // Angular distance from the center of view (heading direction at altitude ~45°)
  // For zoomed view: center on the direction we're facing at ~45° altitude
  float center_alt = (s_zoom == 0) ? 90.0f : 45.0f;  // Full sky: zenith center, zoom: mid-sky

  // Distance from center of projection
  float dalt = center_alt - alt;
  float daz = az - heading;
  // Normalize daz to -180..180
  while(daz > 180) daz -= 360;
  while(daz < -180) daz += 360;

  // Simple angular distance for zoom clipping
  if(s_zoom > 0) {
    float ang_dist = dalt*dalt + daz*daz*pcos(alt)*pcos(alt);
    if(ang_dist > fov*fov*1.5f) return false;  // Outside FOV
  }

  float r;
  if(s_zoom == 0) {
    // Full sky: zenith at center, horizon at edge
    r = (90.0f - alt) / 90.0f * radius;
  } else {
    // Zoomed: map FOV to full radius
    r = dalt / fov * radius;
  }

  float theta = daz + 180.0f;
  if(s_zoom == 0) theta = az - heading + 180.0f;

  float px_x, px_y;
  if(s_zoom == 0) {
    px_x = r * psin(theta);
    px_y = -r * pcos(theta);
  } else {
    // Zoomed: daz maps to horizontal, dalt maps to vertical
    px_x = daz / fov * radius;
    px_y = dalt / fov * radius;
  }

  *sx = cx + (int)px_x;
  *sy = cy + (int)px_y;

  // Clip to screen bounds
  if(*sx < 0 || *sx >= cx*2 || *sy < 0 || *sy >= cy*2) return false;
  return true;
}

// ============================================================================
// DRAWING
// ============================================================================
static void canvas_proc(Layer *l, GContext *ctx) {
  GRect b = layer_get_bounds(l);
  int w = b.size.w, h = b.size.h;
  int cx = w/2, cy = h/2;
  int radius = w/2 - 12;  // Leave room for labels

  // Dark sky background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Horizon circle
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorFromHEX(0x003366));
  #else
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  #endif
  graphics_draw_circle(ctx, GPoint(cx, cy), radius);

  // Compute sidereal time using observer longitude
  float lst = local_sidereal_time(s_lon);

  // Pre-compute star screen positions
  int star_x[NUM_STARS], star_y[NUM_STARS];
  bool star_vis[NUM_STARS];
  for(int i=0; i<NUM_STARS; i++) {
    float alt, az;
    radec_to_altaz(s_stars[i].ra_h, s_stars[i].dec_d, lst, s_lat, &alt, &az);
    star_vis[i] = project(alt, az, s_heading, cx, cy, radius, &star_x[i], &star_y[i]);
  }

  // Draw constellation lines
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorFromHEX(0x4488AA));
  #else
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  #endif
  graphics_context_set_stroke_width(ctx, s_zoom > 0 ? 2 : 1);
  for(int i=0; i<NUM_LINES; i++) {
    int a=s_lines[i].a, b2=s_lines[i].b;
    if(star_vis[a] && star_vis[b2]) {
      graphics_draw_line(ctx, GPoint(star_x[a],star_y[a]),
                              GPoint(star_x[b2],star_y[b2]));
    }
  }

  // Draw stars
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  for(int i=0; i<NUM_STARS; i++) {
    if(!star_vis[i]) continue;
    int x=star_x[i], y=star_y[i];

    // Star dot — size by magnitude, bigger when zoomed
    int r;
    int boost = s_zoom;  // 0, 1, or 2 extra pixels
    switch(s_stars[i].mag) {
      case 0: r=3+boost; break;
      case 1: r=2+boost; break;
      default: r=1+boost; break;
    }

    #ifdef PBL_COLOR
    if(s_stars[i].mag==0) graphics_context_set_fill_color(ctx, GColorYellow);
    else graphics_context_set_fill_color(ctx, GColorWhite);
    #else
    graphics_context_set_fill_color(ctx, GColorWhite);
    #endif
    graphics_fill_circle(ctx, GPoint(x, y), r);

    // Star name
    if(s_show_names && s_stars[i].name && s_stars[i].mag <= 1) {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorFromHEX(0x88AACC));
      #else
      graphics_context_set_text_color(ctx, GColorLightGray);
      #endif
      graphics_draw_text(ctx, s_stars[i].name, f_sm,
        GRect(x+4, y-8, 70, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
  }

  // Cardinal direction markers around the rim
  const char *dirs[] = {"N","E","S","W"};
  float dir_az[] = {0, 90, 180, 270};
  GFont f_dir = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  for(int i=0; i<4; i++) {
    float theta = dir_az[i] - s_heading + 180.0f;
    int dx = cx + (int)((radius+8) * psin(theta));
    int dy = cy - (int)((radius+8) * pcos(theta));
    #ifdef PBL_COLOR
    GColor dc = (i==0) ? GColorRed : GColorWhite;
    graphics_context_set_text_color(ctx, dc);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, dirs[i], f_dir,
      GRect(dx-8, dy-10, 16, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // Center crosshair (zenith marker)
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorFromHEX(0x333355));
  #else
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  #endif
  graphics_draw_line(ctx, GPoint(cx-5,cy), GPoint(cx+5,cy));
  graphics_draw_line(ctx, GPoint(cx,cy-5), GPoint(cx,cy+5));

  // Compass heading text at top
  if(s_compass_ok) {
    char hbuf[16];
    int hdg = (int)s_heading;
    const char *dir = (hdg>=338||hdg<23)?"N":(hdg<68)?"NE":(hdg<113)?"E":
                      (hdg<158)?"SE":(hdg<203)?"S":(hdg<248)?"SW":
                      (hdg<293)?"W":"NW";
    snprintf(hbuf, sizeof(hbuf), "%s %d°", dir, hdg);
    graphics_context_set_text_color(ctx, GColorWhite);
    GFont f_hd = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_draw_text(ctx, hbuf, f_hd,
      GRect(0, 4, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else {
    graphics_context_set_text_color(ctx, GColorWhite);
    GFont f_hd = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_draw_text(ctx, "Calibrating...", f_hd,
      GRect(0, 4, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // Zoom indicator at bottom
  if(s_zoom > 0) {
    char zbuf[4];
    snprintf(zbuf, sizeof(zbuf), "%dx", 1 << s_zoom);
    graphics_context_set_text_color(ctx, GColorWhite);
    GFont f_z = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_draw_text(ctx, zbuf, f_z,
      GRect(0, h-18, w, 16), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// COMPASS
// ============================================================================
static void compass_handler(CompassHeadingData heading_data) {
  if(heading_data.compass_status == CompassStatusDataInvalid) {
    s_compass_ok = false;
  } else {
    // CalibrationNeeded or Calibrated — both have usable data
    s_compass_ok = true;
    s_heading = (float)TRIGANGLE_TO_DEG((int)heading_data.magnetic_heading);
  }
  if(s_canvas) layer_mark_dirty(s_canvas);
}

// ============================================================================
// BUTTONS
// ============================================================================
static void select_click(ClickRecognizerRef ref, void *ctx) {
  s_show_names = !s_show_names;
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  if(s_zoom < 2) s_zoom++;
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  if(s_zoom > 0) s_zoom--;
  if(s_canvas) layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  window_stack_pop(true);
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ============================================================================
// APPMESSAGE (receive location from phone)
// ============================================================================
static void inbox_cb(DictionaryIterator *it, void *c) {
  Tuple *t;
  t = dict_find(it, MESSAGE_KEY_LAT);
  if(t) s_lat = (float)t->value->int32 / 100.0f;
  t = dict_find(it, MESSAGE_KEY_LON);
  if(t) s_lon = (float)t->value->int32 / 100.0f;
  if(s_canvas) layer_mark_dirty(s_canvas);
}

// ============================================================================
// TICK (for star movement)
// ============================================================================
static void tick_cb(struct tm *t, TimeUnits u) {
  if(s_canvas) layer_mark_dirty(s_canvas);
}

// ============================================================================
// WINDOW
// ============================================================================
static void win_load(Window *w) {
  Layer *wl = window_get_root_layer(w);
  GRect b = layer_get_bounds(wl);
  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, canvas_proc);
  layer_add_child(wl, s_canvas);
  window_set_click_config_provider(w, click_config);

  // Start compass (update every degree change)
  compass_service_set_heading_filter(TRIG_MAX_ANGLE / 360);
  compass_service_subscribe(compass_handler);

  // Also update every minute for star movement
  tick_timer_service_subscribe(MINUTE_UNIT, tick_cb);
}

static void win_unload(Window *w) {
  compass_service_unsubscribe();
  tick_timer_service_unsubscribe();
  if(s_canvas) { layer_destroy(s_canvas); s_canvas = NULL; }
}

// ============================================================================
// LIFECYCLE
// ============================================================================
static void init(void) {
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_window_handlers(s_win, (WindowHandlers){.load=win_load, .unload=win_unload});
  app_message_register_inbox_received(inbox_cb);
  app_message_open(64, 64);
  window_stack_push(s_win, true);
}

static void deinit(void) {
  window_destroy(s_win);
}

int main(void) { init(); app_event_loop(); deinit(); return 0; }
