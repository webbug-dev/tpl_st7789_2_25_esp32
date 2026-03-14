#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#define PIN_SCLK 12
#define PIN_MOSI 11
#define PIN_CS   8
#define PIN_RST  10
#define PIN_DC   9
#define PIN_BLK  13
#define LCD_H_RES 284
#define LCD_V_RES 76
#define FRAMEBUFFER_V_SIZE 19 // 1/4 of screen

LV_IMAGE_DECLARE(stalin_sheet);
LV_FONT_DECLARE(lv_font_main);

#define FRAME_W 42 
#define FRAME_CNT 8

static esp_lcd_panel_handle_t panel_handle = NULL;

static uint32_t my_tick_get_cb(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void stalin_sync_walk_cb(void * obj, int32_t x_pos) {
    lv_obj_t *cont = (lv_obj_t *)obj;
    lv_obj_t *img = lv_obj_get_child(cont, 0);
    static int32_t last_x = 0;
    static int dir_offset = 0;
    lv_obj_set_x(cont, x_pos);
    if (x_pos > last_x) dir_offset = 0;
    else if (x_pos < last_x) dir_offset = 8;
    int frame_idx = (abs(x_pos) / 6) % FRAME_CNT; 
    lv_image_set_offset_x(img, -((frame_idx + dir_offset) * FRAME_W));
    last_x = x_pos;
}

void create_stalin_walk(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0); 
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, FRAME_W, 76);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);    
    lv_obj_set_style_bg_opa(cont, 0, 0); 
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(cont, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *img = lv_image_create(cont);
    lv_image_set_src(img, &stalin_sheet);
    lv_obj_align(img, LV_ALIGN_TOP_LEFT, 0, 0); 
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, cont);
    lv_anim_set_values(&a, 0, LCD_H_RES - FRAME_W); 
    lv_anim_set_duration(&a, 3500);
    lv_anim_set_playback_duration(&a, 3500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, stalin_sync_walk_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

void display_task(void *pvParameters) {
    gpio_set_direction(PIN_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BLK, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCLK, .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * FRAMEBUFFER_V_SIZE * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC, .cs_gpio_num = PIN_CS,
        .pclk_hz = 40 * 1000 * 1000, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_invert_color(panel_handle, false);
    esp_lcd_panel_swap_xy(panel_handle, true);
    esp_lcd_panel_mirror(panel_handle, true, false);
    esp_lcd_panel_set_gap(panel_handle, 18, 82); 
    esp_lcd_panel_disp_on_off(panel_handle, true);

    lv_init();
    lv_tick_set_cb(my_tick_get_cb);

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    /* Apply font to perf monitor */
    lv_obj_t * sys_layer = lv_display_get_layer_sys(disp);
    for(uint32_t i = 0; i < lv_obj_get_child_count(sys_layer); i++) {
        lv_obj_set_style_text_font(lv_obj_get_child(sys_layer, i), &lv_font_main, 0);
    }

    esp_lcd_panel_io_callbacks_t io_callbacks = { .on_color_trans_done = notify_lvgl_flush_ready };
    esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, disp);

    /* Buffer size increased to 40 lines for efficiency */
    uint16_t *buf = heap_caps_malloc(LCD_H_RES * 40 * sizeof(uint16_t), MALLOC_CAP_DMA);
    lv_display_set_buffers(disp, buf, NULL, LCD_H_RES * 40 * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    create_stalin_walk(lv_screen_active());

    while (1) {
        /* LVGL 9 dynamic sleep handling */
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms < 1) sleep_ms = 1;
        if (sleep_ms > 10) sleep_ms = 10; // Don't sleep too long to keep animation smooth
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

void app_main(void) {
    /* Priority 5 is high enough. Ensure it's not fighting with system idle tasks. */
    xTaskCreatePinnedToCore(display_task, "display_task", 8192, NULL, 5, NULL, 1);
}