// Standard includes
#include "pebble.h"

#define TOTAL_IMAGE_SLOTS 6

#define NUMBER_OF_IMAGES 10

// These images are 38 x 62 pixels (i.e. a quarter of the display),
// black and white with the digit character centered in the image.
// (As generated by the `fonttools/font2png.py` script.)
const int IMAGE_RESOURCE_IDS[NUMBER_OF_IMAGES] = {
      RESOURCE_ID_IMAGE_NUM_0, RESOURCE_ID_IMAGE_NUM_1, RESOURCE_ID_IMAGE_NUM_2,
      RESOURCE_ID_IMAGE_NUM_3, RESOURCE_ID_IMAGE_NUM_4, RESOURCE_ID_IMAGE_NUM_5,
      RESOURCE_ID_IMAGE_NUM_6, RESOURCE_ID_IMAGE_NUM_7, RESOURCE_ID_IMAGE_NUM_8,
      RESOURCE_ID_IMAGE_NUM_9
};

const int SMALL_IMAGE_RESOURCE_IDS[NUMBER_OF_IMAGES] = {
      RESOURCE_ID_IMAGE_NUM_S_0, RESOURCE_ID_IMAGE_NUM_S_1, RESOURCE_ID_IMAGE_NUM_S_2,
      RESOURCE_ID_IMAGE_NUM_S_3, RESOURCE_ID_IMAGE_NUM_S_4, RESOURCE_ID_IMAGE_NUM_S_5,
      RESOURCE_ID_IMAGE_NUM_S_6, RESOURCE_ID_IMAGE_NUM_S_7, RESOURCE_ID_IMAGE_NUM_S_8,
      RESOURCE_ID_IMAGE_NUM_S_9
};

const int CONN_RESOURCE_IDS[2] = {
      RESOURCE_ID_CONN, RESOURCE_ID_DISC
};

const int BATT_RESOURCE_IDS[12] = {
      RESOURCE_ID_BATT_0, RESOURCE_ID_BATT_10, RESOURCE_ID_BATT_20, 
      RESOURCE_ID_BATT_30, RESOURCE_ID_BATT_40, RESOURCE_ID_BATT_50,
      RESOURCE_ID_BATT_60, RESOURCE_ID_BATT_70, RESOURCE_ID_BATT_80,
      RESOURCE_ID_BATT_90, RESOURCE_ID_BATT_100, RESOURCE_ID_BATT_CH  
};

static GBitmap *numbers[NUMBER_OF_IMAGES];
static GBitmap *s_numbers[NUMBER_OF_IMAGES];
static GBitmap *conns[2];
static GBitmap *batteries[12];

static const char* const DOWS[] = {
  "DOMENICA", "LUNEDÌ", "MARTEDÌ", "MERCOLEDÌ", "GIOVEDÌ", "VENERDÌ", "SABATO" 
};

static const char* const DOWS_SHORT[] = {
  "DOM", "LUN", "MAR", "MER", "GIO", "VEN", "SAB" 
};

static const char* const MONTHS[] = {
  "GEN", "FEB", "MAR", "APR", "MAG", "GIU", "LUG", "AGO", "SET", "OTT", "NOV", "DIC" 
};

static const char* const ONES[] = {
  "ZERO", "UNO", "DUE", "TRE", "QUATTRO", "CINQUE", "SEI", "SETTE", "OTTO", "NOVE"
};

static const char* const TEENS[] = {
  "DIECI", "UNDICI", "DODICI", "TREDICI", "QUATTORDICI", "QUINDICI", "SEDICI", "DICIASSETTE", "DICIOTTO", "DICIANNOVE"
};

static const char* const TENS[] = {
  "VENTI", "TRENTA", "QUARANTA", "CINQUANTA"
};

static const int position[TOTAL_IMAGE_SLOTS][2][2] = {
  {{12,20},{38,62}},
  {{50,20},{38,62}},
  {{12,86},{38,62}},
  {{50,86},{38,62}},
  {{107,22},{12,20}},
  {{120,22},{12,20}}
};

static const int layers[3][2][2] = {
  {{100, 52}, {36, 120}},
  {{104, 128}, {12, 17}},
  {{119, 128}, {12, 17}}
};

#define EMPTY_SLOT -1

static int image_slot_state[TOTAL_IMAGE_SLOTS] = {EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT};

static Layer *window_layer;
static BitmapLayer *image_containers[TOTAL_IMAGE_SLOTS];
static BitmapLayer *battery_layer;
static BitmapLayer *conn_layer;
static TextLayer *status_layer;
static Window *window; 
static char battery_text[10] = "          ";
static char date_text[24]    = "                        ";
  
static void do_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
  
  for (int i = 0; i < NUMBER_OF_IMAGES; i++) {
    gbitmap_destroy(numbers[i]);
    gbitmap_destroy(s_numbers[i]);
  }
  
  for (int i = 0; i < 2; i++) {
    gbitmap_destroy(conns[i]);
  }
  
  for (int i = 0; i < 12; i++) {
    gbitmap_destroy(batteries[i]);
  }
  
  text_layer_destroy(status_layer);
  window_destroy(window);
}

static void handle_battery(BatteryChargeState charge_state) {
  static bool swc = true;
  if (charge_state.is_plugged) {
    if (charge_state.is_charging) {
      bitmap_layer_set_bitmap(battery_layer, batteries[11]);
    } else {
        swc = !swc;
        bitmap_layer_set_bitmap(battery_layer, swc?batteries[11]:batteries[0]);
    }
  } else {
    snprintf(battery_text, 4, "%d", charge_state.charge_percent);
    int ch = charge_state.charge_percent / 10;
    bitmap_layer_set_bitmap(battery_layer, batteries[ch]);
  }
}

void load_digit_image_into_slot(int slot_number, int digit_value) {
  image_slot_state[slot_number] = digit_value;
  if (slot_number>3) {
    bitmap_layer_set_bitmap(image_containers[slot_number], s_numbers[digit_value]);
  } else {
    bitmap_layer_set_bitmap(image_containers[slot_number], numbers[digit_value]);
  }
}

void display_value(unsigned short value, unsigned short row_number, bool show_first_leading_zero) {
  value = value % 100; 
  
  for (int column_number = 1; column_number >= 0; column_number--) {
    int slot_number = (row_number * 2) + column_number;
    if (!((value == 0) && (column_number == 0) && !show_first_leading_zero)) {
      load_digit_image_into_slot(slot_number, value % 10);
    }
    value = value / 10;
  }
}

unsigned short get_display_hour(unsigned short hour) {
  if (clock_is_24h_style()) {
    return hour;
  }
  unsigned short display_hour = hour % 12;
  return display_hour ? display_hour : 12;
}

void update_date(struct tm *tick_time) {
  snprintf(date_text, 24, "%s\n%02u\n%s\n%u", DOWS_SHORT[tick_time->tm_wday], tick_time->tm_mday, MONTHS[tick_time->tm_mon], (1900 + tick_time->tm_year));
}

static void handle_bluetooth(bool connected) {
  bitmap_layer_set_bitmap(conn_layer, conns[connected?0:1]);
}

static void handle_second_tick(struct tm* tick_time, TimeUnits units_changed) {
  static int day_m = 40;
  if (day_m != tick_time->tm_mday) {
    day_m = tick_time->tm_mday;
    update_date(tick_time);
    text_layer_set_text(status_layer, date_text);
  }
  display_value(get_display_hour(tick_time->tm_hour), 0, true);
  display_value(tick_time->tm_min, 1, true);  
  display_value(tick_time->tm_sec, 2, true);
  handle_battery(battery_state_service_peek());
}

static void do_init(void) {
  window = window_create();

  window_stack_push(window, true);
  window_layer = window_get_root_layer(window);
  
  status_layer = text_layer_create(GRect(layers[0][0][0],layers[0][0][1],layers[0][1][0],layers[0][1][1]));
  text_layer_set_text_alignment(status_layer, GTextAlignmentCenter);
  text_layer_set_text(status_layer, "");
  
  for (int i = 0; i < NUMBER_OF_IMAGES; i++) {
    numbers[i] = gbitmap_create_with_resource(IMAGE_RESOURCE_IDS[i]);
    s_numbers[i] = gbitmap_create_with_resource(SMALL_IMAGE_RESOURCE_IDS[i]);
  }
  
  for (int i = 0; i < 2; i++) {
    conns[i] = gbitmap_create_with_resource(CONN_RESOURCE_IDS[i]);
  }
  
  for (int i = 0; i < 12; i++) {
    batteries[i] = gbitmap_create_with_resource(BATT_RESOURCE_IDS[i]);
  }
  
  conn_layer = bitmap_layer_create(
    GRect(layers[1][0][0],layers[1][0][1],layers[1][1][0],layers[1][1][1]));
  bitmap_layer_set_bitmap(conn_layer, conns[0]);
  
  battery_layer = bitmap_layer_create(
    GRect(layers[2][0][0],layers[2][0][1],layers[2][1][0],layers[2][1][1]));
  bitmap_layer_set_bitmap(battery_layer, batteries[0]);
  
  for (int slot_number = 0; slot_number < TOTAL_IMAGE_SLOTS; slot_number++) {
    image_containers[slot_number] = bitmap_layer_create(
        GRect(position[slot_number][0][0], position[slot_number][0][1], position[slot_number][1][0], position[slot_number][1][1]));
    
    load_digit_image_into_slot(slot_number, 0);
    layer_add_child(window_layer, bitmap_layer_get_layer(image_containers[slot_number]));
  }
  
    time_t now = time(NULL);
  struct tm *current_time = localtime(&now);
  
  handle_second_tick(current_time, SECOND_UNIT);
  handle_bluetooth(bluetooth_connection_service_peek());
  
  tick_timer_service_subscribe(SECOND_UNIT, &handle_second_tick);
  battery_state_service_subscribe(&handle_battery);
  bluetooth_connection_service_subscribe(&handle_bluetooth);
  
  layer_add_child(window_layer, text_layer_get_layer(status_layer));
  
  window_set_background_color(window, GColorWhite);
  text_layer_set_text_color(status_layer, GColorBlack);
  text_layer_set_background_color(status_layer, GColorWhite);
  text_layer_set_font(status_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ROBOTO_BOLD_16)));

  layer_add_child(window_layer, bitmap_layer_get_layer(conn_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(battery_layer));
}

int main(void) {
  do_init();
  app_event_loop();
  do_deinit();
}
