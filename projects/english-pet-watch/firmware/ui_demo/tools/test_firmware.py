#!/usr/bin/env python3
"""主机回归：编译源码中的原函数，模拟事件；不连接或烧录设备。"""
import ast
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / 'main'
CJSON = ROOT / 'managed_components/espressif__cjson/cJSON'


def function(file, signature):
    source = (MAIN / file).read_text()
    start = source.index(signature + '\n{')
    return source[start:source.index('\n}', start) + 2] + '\n'


def run_c(source, json=False):
    with tempfile.TemporaryDirectory(prefix='epw-test-') as tmp:
        path = Path(tmp) / 'test.c'
        path.write_text(source)
        command = ['cc', '-std=c11', '-I', str(MAIN), str(path), '-o', str(Path(tmp) / 'test')]
        if json:
            command += ['-DCJSON_NESTING_LIMIT=16', '-I', str(CJSON), str(CJSON / 'cJSON.c')]
        command += ['-lm']
        subprocess.run(command, check=True)
        subprocess.run([str(Path(tmp) / 'test')], check=True)


class FirmwareTests(unittest.TestCase):
    def test_normal_boot_has_no_icon_benchmark(self):
        source = (MAIN / "main.c").read_text()
        self.assertNotIn("icon_bench", source)
        self.assertNotIn("ui_debug_icon_", (MAIN / "ui.c").read_text())


    def test_power_registers_and_boot_hold(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_RST_POWERON 1
#define ESP_LOGI(...) ((void)0)
#define AXP_REG_IRQ_STATUS1 0x49
#define AXP_REG_IRQ_ENABLE1 0x41
#define AXP_IRQ_PWRKEY_MASK 0x0F
#define pdMS_TO_TICKS(x) (x)
static uint8_t regs[256];
static int fault,reason=1,offs;
static int64_t us,release_at=-1;
static int reg_read(uint8_t r,uint8_t *v,size_t n){
    if(fault) return -2;
    if(r==0x49 && release_at>=0 && us>=release_at) regs[r]|=1;
    *v=regs[r];return 0;
}
static int reg_write(uint8_t r,uint8_t v){
    if(fault) return -2;
    if(r==0x49) regs[r]&=~v;else regs[r]=v;
    if(r==0x10 && (v&1))offs++;
    return 0;
}
static int esp_reset_reason(void){return reason;}
static int64_t esp_timer_get_time(void){return us;}
static void vTaskDelay(int ms){us+=(int64_t)ms*1000;}
'''
        header = (MAIN / 'power.h').read_text()
        source += header[header.index('enum {'):header.index('};') + 2]
        for signature in ('esp_err_t power_key_read(uint8_t *events)',
                          'esp_err_t power_key_configure(void)',
                          'esp_err_t power_shutdown(void)',
                          'esp_err_t power_boot_wait(void)'):
            source += function('power.c', signature)
        source += r'''
int main(void){
    regs[0x22]=0xFF;regs[0x27]=0xFF;regs[0x41]=0x80;
    assert(power_key_configure()==0);
    assert(regs[0x22]==0xFD && regs[0x27]==0xFC && regs[0x41]==0x8F);
    uint8_t ev=255;regs[0x49]=0xAB;
    assert(power_key_read(&ev)==0 && ev==0x0B && regs[0x49]==0xA0);
    fault=1;assert(power_key_read(&ev)==-2 && ev==0);fault=0;
    regs[0x10]=0xA0;assert(power_shutdown()==0 && regs[0x10]==0xA1);
    offs=0;regs[0x20]=1;regs[0x49]=0;us=0;
    assert(power_boot_wait()==0 && us>=2872000 && offs==0);
    us=0;release_at=1000000;regs[0x49]=0;
    assert(power_boot_wait()==ESP_FAIL && offs==1 && us<2872000);
    release_at=-1;regs[0x49]=0;us=0;offs=0;reason=2;
    assert(power_boot_wait()==0 && us==0 && offs==0);
    reason=1;regs[0x20]=4;assert(power_boot_wait()==0 && us==0);
    fault=1;assert(power_boot_wait()==-2 && offs==0);
}
'''
        run_c(source)

    def test_power_key_three_seconds_once_and_no_short_after_hold(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
static bool s_pwr_down,s_pwr_fired;
static uint32_t s_pwr_started,now;
static uint8_t events;
static int err,busy,opened,shorts;
static int power_key_read(uint8_t *v){*v=events;events=0;return err;}
static int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
static int bsp_display_lock(int t){return busy;}
static void bsp_display_unlock(void){}
static void ui_power_menu_open(void){opened++;}
static void toggle_apps(const char *s){shorts++;}
'''
        header = (MAIN / 'power.h').read_text()
        source += header[header.index('enum {'):header.index('};') + 2]
        source += function('button.c', 'static void pwr_key_poll(void)')
        source += r'''
static void poll(uint32_t t,uint8_t e){now=t;events=e;pwr_key_poll();}
int main(void){
    poll(100,2);poll(3099,0);assert(opened==0 && shorts==0);
    poll(3100,0);poll(10000,0);assert(opened==1);
    poll(10100,9);assert(shorts==0 && !s_pwr_down);
    poll(11000,2);poll(11200,9);assert(shorts==1 && opened==1);
    poll(12000,2);err=-1;poll(12500,0);err=0;poll(18000,0);assert(opened==1);
    poll(UINT32_MAX-1000,2);poll(3999,0);assert(opened==2);
    poll(4000,1);poll(5000,3);poll(11000,0);assert(opened==2);
    poll(12000,2);busy=-1;poll(17000,0);assert(opened==2);
    busy=0;poll(17100,0);assert(opened==3);
}
'''
        run_c(source)

    def test_power_slide_requires_full_drag_and_release(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef int lv_obj_t;
typedef int lv_indev_t;
typedef int lv_event_code_t;
typedef struct {int code;} lv_event_t;
typedef struct {int x,y;} lv_point_t;
typedef struct {void *obj;int end;void (*exec)(void *,int32_t);} lv_anim_t;
#define POWER_KNOB_MIN 4
#define POWER_KNOB_MAX 224
#define LV_ABS abs
#define LV_OPA_COVER 255
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_EVENT_PRESSED 1
#define LV_EVENT_PRESSING 2
#define LV_EVENT_RELEASED 3
#define LV_EVENT_PRESS_LOST 4
static bool s_power_pending,s_power_dragging,waking;
static lv_point_t s_power_press,point;
static int knob,text,*s_power_knob=&knob,*s_power_slide_text=&text,position=4;
static int submitted,stopped,ended,reset;
static lv_indev_t input;
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &input;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){*p=point;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static bool ui_activity(void){bool w=waking;waking=false;return w;}
static void lv_indev_wait_release(lv_indev_t *i){}
static void lv_obj_set_x(void *o,int x){position=x;}
static int lv_obj_get_x(void *o){return position;}
static void lv_obj_set_style_opa(void *o,int a,int b){}
static void lv_obj_add_flag(void *o,int f){}
static void lv_label_set_text(void *o,const char *t){}
static void watch_timer_reset(void){reset++;}
static void nursery_checkin_pause(void){ended++;}
static void nursery_stop(void){stopped++;}
static void button_request_shutdown(void){submitted++;}
static void lv_anim_delete(void *o,void (*cb)(void *,int32_t)){}
static void lv_anim_init(lv_anim_t *a){}
static void lv_anim_set_var(lv_anim_t *a,void *o){a->obj=o;}
static void lv_anim_set_values(lv_anim_t *a,int from,int to){a->end=to;}
static void lv_anim_set_duration(lv_anim_t *a,int t){}
static void lv_anim_path_ease_out(void){}
static void lv_anim_set_path_cb(lv_anim_t *a,void (*cb)(void)){}
static void lv_anim_set_exec_cb(lv_anim_t *a,void (*cb)(void *,int32_t)){a->exec=cb;}
static void lv_anim_start(lv_anim_t *a){a->exec(a->obj,a->end);}
'''
        source += function('ui.c', 'static void power_knob_x_cb(void *obj, int32_t x)')
        source += function('ui.c', 'static void power_slide_cb(lv_event_t *e)')
        source += r'''
static void event(int code,int x,int y){lv_event_t e={code};point=(lv_point_t){x,y};power_slide_cb(&e);}
int main(void){
    event(1,120,230);event(2,220,230);event(3,220,230);assert(submitted==0 && position==4);
    event(1,120,230);event(2,360,230);event(4,360,230);assert(submitted==0 && position==4);
    event(1,120,230);event(2,360,300);event(3,360,230);assert(submitted==0 && position==4);
    waking=true;event(1,120,230);event(2,360,230);event(3,360,230);assert(submitted==0);
    event(1,120,230);event(3,360,230);assert(submitted==0);
    event(1,120,230);event(2,360,230);assert(submitted==0 && position==224);
    event(3,360,230);assert(submitted==1 && stopped==1 && ended==1 && reset==1);
    event(3,360,230);assert(submitted==1);
}
'''
        run_c(source)

    def test_nursery_left_drawer_drag_and_cancel(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef int lv_obj_t;
typedef int lv_indev_t;
typedef struct {int x,y;} lv_point_t;
typedef int lv_event_code_t;
typedef struct {int code;} lv_event_t;
typedef struct {int end; void (*done)(void *);} lv_anim_t;
#define SCREEN_W 466
#define NUR_DRAG_TH 16
#define LV_ABS abs
#define LV_EVENT_PRESSED 1
#define LV_EVENT_PRESSING 2
#define LV_EVENT_RELEASED 3
#define LV_EVENT_PRESS_LOST 4
static bool s_screen_off,s_nursery_open,s_center_open,s_center_drag_engaged;
static bool s_nur_dragging,s_nur_drag_engaged,busy;
static int s_nur_press_x,s_nur_press_y,s_nur_reveal=-SCREEN_W,s_nur_snap_ext;
static lv_point_t point;
static lv_obj_t obj,*s_nursery=&obj;
static lv_indev_t input;
static int target,shown_x,begin;
static void *completed;
static int lv_event_get_code(lv_event_t *e){return e->code;}
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &input;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){*p=point;}
static bool page_anim_busy(void){return busy;}
static bool pages_busy_except_center(void){return false;}
static lv_obj_t *nursery_drawer_obj(void){return &obj;}
static void lv_obj_set_x(lv_obj_t *o,int x){shown_x=x;}
static void nursery_drawer_begin(void){begin++;}
static void home_snap_begin(void){begin++;}
static void anim_nur_open_x_cb(void *o,int32_t x){}
static void anim_nur_close_x_cb(void *o,int32_t x){}
static void nursery_open_done(void *a){}
static void nursery_open_abort_done(void *a){}
static void nursery_close_done(void *a){}
static void nursery_close_abort_done(void *a){}
static void lv_anim_delete(void *o,void (*cb)(void *,int32_t)){}
static void lv_anim_init(lv_anim_t *a){a->end=0;a->done=0;}
static void lv_anim_set_var(lv_anim_t *a,void *o){}
static void lv_anim_set_values(lv_anim_t *a,int from,int to){a->end=to;}
static void lv_anim_set_duration(lv_anim_t *a,int t){}
static void lv_anim_path_ease_out(void){}
static void lv_anim_path_ease_in(void){}
static void lv_anim_set_path_cb(lv_anim_t *a,void (*p)(void)){}
static void lv_anim_set_completed_cb(lv_anim_t *a,void (*cb)(void *)){a->done=cb;}
static void lv_anim_set_exec_cb(lv_anim_t *a,void (*cb)(void *,int32_t)){}
static void lv_anim_start(lv_anim_t *a){target=a->end;completed=a->done;}
static void page_anim_lock(int ms){}
'''
        for name in ('nursery_set_reveal', 'nursery_set_reveal_real',
                     'home_nursery_drag_cb', 'nursery_page_drag_cb'):
            arg = 'int x' if name.startswith('nursery_set') else 'lv_event_t *e'
            source += function('ui.c', f'static void {name}({arg})')
        source += r'''
static void home(int code,int x,int y){lv_event_t e={code};point=(lv_point_t){x,y};home_nursery_drag_cb(&e);}
static void page(int code,int x,int y){lv_event_t e={code};point=(lv_point_t){x,y};nursery_page_drag_cb(&e);}
int main(void){
    home(1,100,100);home(2,240,110);
    assert(s_nur_drag_engaged && s_nur_reveal==-326 && shown_x==-326);
    home(3,240,110);assert(target==0 && completed==nursery_open_done);
    s_nur_reveal=-466;home(1,100,100);home(2,140,100);home(4,140,100);
    assert(target==-466 && completed==nursery_open_abort_done);
    home(1,240,100);home(2,100,100);assert(!s_nur_drag_engaged);home(3,100,100);
    home(1,100,100);home(2,130,240);assert(!s_nur_drag_engaged);home(3,130,240);
    s_center_drag_engaged=true;home(1,100,100);home(2,250,100);
    assert(!s_nur_drag_engaged);home(3,250,100);s_center_drag_engaged=false;
    s_nursery_open=true;s_nur_reveal=0;
    page(1,300,100);page(2,160,100);assert(s_nur_reveal==-140);
    page(3,160,100);assert(target==-466 && completed==nursery_close_done);
    s_nur_reveal=0;page(1,300,100);page(2,260,100);page(4,260,100);
    assert(target==0 && completed==nursery_close_abort_done);
    page(1,100,100);page(2,250,100);assert(!s_nur_drag_engaged);page(3,250,100);
    nursery_set_reveal(-999);assert(s_nur_reveal==-466);
    nursery_set_reveal(999);assert(s_nur_reveal==0);
    nursery_set_reveal_real(-999);assert(s_nur_reveal==-466);
    nursery_set_reveal_real(999);assert(s_nur_reveal==0);
}
'''
        run_c(source)

    def test_apps_pan_all_directions_and_hit_testing(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
typedef struct {int x,y;} lv_point_t;
typedef int lv_obj_t;
typedef int lv_indev_t;
typedef int lv_event_code_t;
typedef struct {int code;} lv_event_t;
typedef struct {void (*cb)(void *,int32_t);} lv_anim_t;
#define CENTER_X 233
#define CENTER_Y 233
#define APP_COUNT 6
#define APP_ICON_D 120
#define APP_ROOT_W APP_ICON_D
#define APP_ROOT_H 120
#define APP_ICON_MIN_D 64
#define APP_GRID_STEP 136
#define APP_LABEL_W 112
#define APP_PAN_LIMIT APP_GRID_STEP
#define APP_PAN_OVERSHOOT 28
#define APP_GRID_DRAG_TH 16
#define LV_EVENT_PRESSED 1
#define LV_EVENT_PRESSING 2
#define LV_EVENT_RELEASED 3
#define LV_EVENT_PRESS_LOST 4
#define LV_UNUSED(x) ((void)(x))
static lv_obj_t apps, *s_apps=&apps;
static lv_point_t s_app_pan,s_app_pan_start,s_app_settle_from,s_app_settle_to,s_app_press,point;
static lv_obj_t icon_objects[APP_COUNT],*s_app_icons[APP_COUNT];
static bool s_app_drag_active,s_app_drag_moved,busy;
typedef int lv_draw_buf_t;
static lv_draw_buf_t *s_app_art[APP_COUNT];
static lv_obj_t *s_app_labels[APP_COUNT];
static const char *s_app_names[APP_COUNT]={"pet","music","status","wifi","settings","timer"};
static int opened=-1,layouts,sources;
static void app_art_resize(int i,int size){}
static void lv_label_set_text(lv_obj_t *o,const char *t){}
static void lv_image_set_src(lv_obj_t *o,void *src){sources++;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h){}

static int lv_event_get_code(lv_event_t *e){return e->code;}
static lv_indev_t input;
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &input;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){*p=point;}
static bool page_anim_busy(void){return busy;}
static void app_open_index(int i){opened=i;}
static void lv_anim_delete(void *obj,void (*cb)(void *,int32_t)){}
static void lv_anim_init(lv_anim_t *a){}
static void lv_anim_set_var(lv_anim_t *a,void *v){}
static void lv_anim_set_values(lv_anim_t *a,int x,int y){}
static void lv_anim_set_duration(lv_anim_t *a,int ms){}
static void lv_anim_path_ease_out(void){}
static void lv_anim_set_path_cb(lv_anim_t *a,void (*cb)(void)){}
static void lv_anim_set_exec_cb(lv_anim_t *a,void (*cb)(void *,int32_t)){a->cb=cb;}
static void lv_anim_start(lv_anim_t *a){a->cb(s_apps,256);}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){layouts++;}
static void lv_obj_set_style_transform_scale_x(lv_obj_t *o,int s,int p){}
static void lv_obj_set_style_transform_scale_y(lv_obj_t *o,int s,int p){}
'''
        for signature in ('static lv_point_t app_icon_center(int i)',
                          'static int app_icon_size(lv_point_t center)',
                          'static void apps_layout_icons(void)',
                          'static int app_pan_bound(int value, bool elastic)',
                          'static void app_pan_anim_cb(void *var, int32_t value)',
                          'static void apps_settle_bounds(void)',
                          'static void apps_open_at(lv_point_t p)',
                          'static void apps_touch_cb(lv_event_t *e)'):
            source += function('ui.c', signature)
        source += r'''
static void event(int code,int x,int y){lv_event_t e={code};point=(lv_point_t){x,y};apps_touch_cb(&e);}
int main(void){
    for(int i=0;i<APP_COUNT;i++)s_app_icons[i]=&icon_objects[i];
    const int directions[][2]={{60,0},{-60,0},{0,60},{0,-60},{60,60},{60,-60},{-60,60},{-60,-60}};
    for(int i=0;i<8;i++){
        s_app_pan=(lv_point_t){0,0};opened=-1;
        event(LV_EVENT_PRESSED,233,233);
        event(LV_EVENT_PRESSING,233+directions[i][0],233+directions[i][1]);
        assert(s_app_pan.x==directions[i][0] && s_app_pan.y==directions[i][1]);
        event(LV_EVENT_RELEASED,233+directions[i][0],233+directions[i][1]);
        assert(opened==-1 && !s_app_drag_active);
        assert(s_app_pan.x==directions[i][0] && s_app_pan.y==directions[i][1]);
    }
    assert(layouts>0);
    for(int sign=-1;sign<=1;sign+=2){
        s_app_pan=(lv_point_t){0,0};
        event(LV_EVENT_PRESSED,233,233);
        event(LV_EVENT_PRESSING,233+sign*300,233+sign*300);
        assert(s_app_pan.x==sign*(APP_PAN_LIMIT+APP_PAN_OVERSHOOT) && s_app_pan.y==sign*(APP_PAN_LIMIT+APP_PAN_OVERSHOOT));
        event(LV_EVENT_RELEASED,233+sign*300,233+sign*300);
        assert(s_app_pan.x==sign*APP_PAN_LIMIT && s_app_pan.y==sign*APP_PAN_LIMIT);
    }
    s_app_pan=(lv_point_t){30,-20};
    for(int i=0;i<APP_COUNT;i++){
        opened=-1;lv_point_t p=app_icon_center(i);
        event(LV_EVENT_PRESSED,p.x,p.y);event(LV_EVENT_RELEASED,p.x,p.y);assert(opened==i);
        opened=-1;
        int label_y=p.y+app_icon_size(p)/2+14;
        event(LV_EVENT_PRESSED,p.x,label_y);event(LV_EVENT_RELEASED,p.x,label_y);assert(opened==i);
    }
    opened=-1;event(LV_EVENT_PRESSED,20,20);event(LV_EVENT_RELEASED,20,20);assert(opened==-1);
    lv_point_t p=app_icon_center(0);
    event(LV_EVENT_PRESSED,p.x,p.y);event(LV_EVENT_PRESS_LOST,p.x,p.y);assert(opened==-1);
    busy=true;event(LV_EVENT_PRESSED,p.x,p.y);event(LV_EVENT_RELEASED,p.x,p.y);assert(opened==-1);
    busy=false;
    event(LV_EVENT_PRESSED,p.x+100,p.y);event(LV_EVENT_RELEASED,p.x,p.y);assert(opened==-1);
    s_app_pan=(lv_point_t){0,0};assert(app_icon_size(app_icon_center(0))==APP_ICON_D);
    assert(app_icon_size(app_icon_center(1))<APP_ICON_D);
    assert(app_icon_size((lv_point_t){1000,1000})==APP_ICON_MIN_D);
    s_app_art[0]=(lv_draw_buf_t *)&apps;
    apps_layout_icons();int before=sources;apps_layout_icons();assert(sources==before);
    s_app_pan=(lv_point_t){80,80};apps_layout_icons();assert(sources==before);
    int last=APP_ICON_D;
    for(int x=0;x<400;x++){
        int size=app_icon_size((lv_point_t){CENTER_X+x,CENTER_Y});
        assert(size<=last && last-size<=1 && size>=APP_ICON_MIN_D);last=size;
    }
    return 0;
}
'''
        run_c(source)

    def test_timer_alarm_wakes_and_swallows_active_touch(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int lv_timer_t;typedef int lv_indev_t;
#define LV_UNUSED(x) ((void)(x))
static bool due,s_tools_open,s_app_drag_active=true;
static int s_tools_kind,wakes,waits,refreshes;
static lv_indev_t device,*input=&device;
static bool watch_system_tick(void){bool value=due;due=false;return value;}
static void ui_activity(void){wakes++;}
static lv_indev_t *bsp_display_get_input_dev(void){return input;}
static void lv_indev_wait_release(lv_indev_t *i){waits++;}
static void tools_open(int kind){assert(wakes>0);s_tools_open=true;s_tools_kind=kind;}
static void tools_refresh(void){refreshes++;}
'''
        source += function('ui.c', 'static void tools_tick_cb(lv_timer_t *timer)')
        source += r'''
int main(void){
    tools_tick_cb(NULL);assert(wakes==0 && refreshes==0);
    due=true;tools_tick_cb(NULL);
    assert(wakes==1 && waits==1 && !s_app_drag_active && s_tools_kind==1 && refreshes==1);
    tools_tick_cb(NULL);assert(wakes==1 && waits==1 && refreshes==2);
    input=NULL;due=true;tools_tick_cb(NULL);assert(wakes==2 && waits==1);return 0;
}
'''
        run_c(source)

    def test_rgb565_continuous_resampling_stride_edges_and_reuse(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define APP_ICON_D 120
#define LV_COLOR_FORMAT_RGB565 1
#define LV_STRIDE_AUTO 0
typedef struct {struct{int w,h,stride;}header;uint8_t *data;}lv_draw_buf_t;
static lv_draw_buf_t *s_app_art[6],*s_app_scaled[6];
static int s_app_pixel_size[6],icon,*s_app_icons[6],sources,drops;
static lv_draw_buf_t *lv_draw_buf_reshape(lv_draw_buf_t *b,int cf,int w,int h,int stride){
    if(w>120)return 0;b->header.w=w;b->header.h=h;b->header.stride=(w*2+3)&~3;return b;
}
static void lv_image_cache_drop(lv_draw_buf_t *b){drops++;}
static void lv_image_set_src(int *o,lv_draw_buf_t *b){sources++;}
'''
        for signature in ('static uint16_t app_mix_rgb565(uint16_t a, uint16_t b, unsigned weight)',
                          'static void app_resize_rgb565(const lv_draw_buf_t *src, lv_draw_buf_t *dst, int size)',
                          'static void app_art_resize(int i, int size)'):
            source += function('ui.c', signature)
        source += r'''
int main(void){
    assert(app_mix_rgb565(0,65535,0)==0);
    assert(app_mix_rgb565(0,65535,256)==65535);
    uint16_t input[4][6];uint8_t output[32];
    memset(input,0xFF,sizeof(input));memset(output,0xA5,sizeof(output));
    for(int y=0;y<4;y++)for(int x=0;x<4;x++)input[y][x]=(x*8<<11)|(y*16<<5);
    lv_draw_buf_t src={{4,4,12},(uint8_t *)input},dst={{3,3,8},output};
    app_resize_rgb565(&src,&dst,3);
    assert(*(uint16_t *)(output+8+2)==((12<<11)|(24<<5)));
    assert(*(uint16_t *)output==0 && *(uint16_t *)(output+16+4)==((24<<11)|(48<<5)));
    for(int y=0;y<3;y++)assert(output[y*8+6]==0xA5 && output[y*8+7]==0xA5);
    for(int i=24;i<32;i++)assert(output[i]==0xA5);
    static uint16_t large_src[120][122];static uint8_t large_dst[28808];
    for(int y=0;y<120;y++)for(int x=0;x<122;x++)large_src[y][x]=0xFFFF;
    src=(lv_draw_buf_t){{120,120,244},(uint8_t *)large_src};
    dst.data=large_dst;s_app_art[0]=&src;s_app_scaled[0]=&dst;s_app_icons[0]=&icon;
    for(int size=64;size<=120;size++){
        memset(large_dst,0xA5,sizeof(large_dst));
        app_art_resize(0,size);assert(s_app_pixel_size[0]==size);
        for(int y=0;y<size;y++){
            uint16_t *row=(uint16_t *)(large_dst+y*dst.header.stride);
            for(int x=0;x<size;x++)assert(row[x]==0xFFFF);
            if(size%2)assert(large_dst[y*dst.header.stride+size*2]==0xA5);
        }
        assert(large_dst[size*dst.header.stride]==0xA5);
        int before=sources;app_art_resize(0,size);assert(sources==before && drops==sources);
    }
    s_app_scaled[0]=0;int before=sources;app_art_resize(0,80);assert(sources==before);
}
'''
        run_c(source)

    def test_app_cache_uses_psram_and_handles_failures(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int lv_color_format_t;
typedef int lv_obj_t;
typedef int lv_draw_buf_t;
typedef struct {void *(*buf_malloc_cb)(size_t,int);void (*buf_free_cb)(void *);} lv_draw_buf_handlers_t;
#define LV_UNUSED(x) ((void)(x))
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_COLOR_FORMAT_RGB565 7
#define LV_STRIDE_AUTO 0
#define LV_RESULT_OK 0
static int caps,inits,destroys;static bool allocation_fails,capture_fails;
static lv_draw_buf_t buffer;
static void *heap_caps_malloc(size_t n,int flags){caps=flags;return allocation_fails?NULL:&buffer;}
static void heap_caps_free(void *p){}
static void lv_draw_buf_init_with_default_handlers(lv_draw_buf_handlers_t *h){inits++;}
static void lv_obj_update_layout(lv_obj_t *o){}
static int lv_obj_get_width(lv_obj_t *o){return 120;}
static int lv_obj_get_height(lv_obj_t *o){return 120;}
static lv_draw_buf_t *lv_draw_buf_create_ex(lv_draw_buf_handlers_t *h,int w,int height,int cf,int stride){
    assert(w==120 && height==120 && cf==LV_COLOR_FORMAT_RGB565);
    assert(h->buf_free_cb==heap_caps_free);return h->buf_malloc_cb(28800,cf);
}
static int lv_snapshot_take_to_draw_buf(lv_obj_t *o,int cf,lv_draw_buf_t *buf){return capture_fails?1:0;}
static void lv_draw_buf_destroy(lv_draw_buf_t *buf){destroys++;}
'''
        source += function('ui.c', 'static void *app_art_alloc(size_t size, lv_color_format_t cf)')
        source += function('ui.c', 'static lv_draw_buf_t *app_art_snapshot(lv_obj_t *root)')
        source += r'''
int main(void){
    lv_obj_t root=0;allocation_fails=true;assert(app_art_snapshot(&root)==NULL);
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) && destroys==0);
    allocation_fails=false;capture_fails=true;assert(app_art_snapshot(&root)==NULL && destroys==1);
    capture_fails=false;assert(app_art_snapshot(&root)==&buffer && inits==1);return 0;
}
'''
        run_c(source)

    def test_only_applications_are_in_launcher(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
enum {APP_PET,APP_NURSERY,APP_STATUS,APP_WIFI,APP_SETTINGS,APP_TIMER};
#define UI_PET_HAPPY 1
static int opened=-1;static bool busy;
static bool page_anim_busy(void){return busy;}
static void ui_pet_set_state(int state,void *text){opened=APP_PET;}
static void nursery_open(void){opened=APP_NURSERY;}
static void status_open(void){opened=APP_STATUS;}
static void wifi_open(void){opened=APP_WIFI;}
static void tools_open(int kind){opened=kind?APP_TIMER:APP_SETTINGS;}
#define NULL ((void *)0)
'''
        source += function('ui.c', 'static void app_open_index(int idx)')
        source += r'''
int main(void){
    for(int i=0;i<6;i++){app_open_index(i);assert(opened==i);}
    opened=-1;app_open_index(6);assert(opened==-1);
    busy=true;app_open_index(0);assert(opened==-1);return 0;
}
'''
        run_c(source)

    def test_timer_monotonic_pause_resume_and_one_shot_alarm(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "watch_system.h"
#define ESP_LOGI(...) ((void)0)
static int64_t now_us,s_remaining_us,s_deadline_us;
static uint32_t s_preset_s=60;
static watch_timer_state_t s_timer_state;
static int64_t esp_timer_get_time(void){return now_us;}
void watch_timer_reset(void);
'''
        # The core header only needs ESP's error type on the host.
        source = source.replace('#include "watch_system.h"',
            'typedef enum {WATCH_TIMER_IDLE,WATCH_TIMER_RUNNING,WATCH_TIMER_PAUSED,WATCH_TIMER_FINISHED} watch_timer_state_t;')
        for signature in ('bool watch_timer_select(uint32_t seconds)',
                          'uint32_t watch_timer_remaining(void)', 'watch_timer_state_t watch_timer_state(void)',
                          'bool watch_system_tick(void)', 'void watch_timer_start(void)',
                          'void watch_timer_pause(void)', 'void watch_timer_reset(void)'):
            source += function('watch_system.c', signature)
        source += r'''
int main(void){
    uint32_t presets[]={60,180,300,600,1800,3600};
    assert(!watch_timer_select(0) && !watch_timer_select(59));
    for(int i=0;i<6;i++){
        watch_timer_reset();assert(watch_timer_select(presets[i]));
        assert(watch_timer_remaining()==presets[i]);watch_timer_start();
        assert(!watch_timer_select(60));
        now_us=s_deadline_us-1;assert(watch_timer_remaining()==1 && !watch_system_tick());
        now_us++;assert(watch_system_tick());assert(!watch_system_tick());
        assert(watch_timer_state()==WATCH_TIMER_FINISHED && watch_timer_remaining()==0);
    }
    watch_timer_reset();assert(watch_timer_select(60));watch_timer_start();
    now_us+=1500000;watch_timer_pause();assert(watch_timer_remaining()==59);
    assert(!watch_timer_select(180));now_us+=90000000;assert(watch_timer_remaining()==59);
    watch_timer_start();assert(s_deadline_us-now_us==58500000);
    now_us=s_deadline_us;watch_timer_pause();assert(watch_system_tick());
    watch_timer_start();assert(watch_timer_remaining()==60);
    watch_timer_reset();now_us+=1000000000;assert(!watch_system_tick());
    watch_timer_reset();assert(watch_timer_select(60));now_us=0;watch_timer_start();
    now_us=1500000;watch_timer_pause();assert(watch_timer_state()==WATCH_TIMER_PAUSED);
    assert(watch_timer_remaining()==59 && !watch_timer_select(180));
    now_us=500000000;assert(watch_timer_remaining()==59 && !watch_system_tick());
    watch_timer_start();assert(s_deadline_us==558500000);
    watch_timer_reset();now_us+=1000000000;assert(!watch_system_tick());
    watch_timer_start();now_us=s_deadline_us;watch_timer_pause();assert(watch_system_tick());
    return 0;
}
'''
        run_c(source)

    def test_idle_setting_persistence_failure_and_validation(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define NVS_READWRITE 1
#define NVS_READONLY 0
static uint32_t s_idle_ms=30000,saved;
static int failure,writes;
static int nvs_open(const char *n,int mode,int *h){*h=1;return failure==1?2:0;}
static int nvs_set_u32(int h,const char *key,uint32_t v){writes++;saved=v;return failure==2?2:0;}
static int nvs_commit(int h){return failure==3?2:0;}
static int nvs_get_u32(int h,const char *key,uint32_t *v){*v=saved;return 0;}
static void nvs_close(int h){}
'''
        for signature in ('static bool idle_timeout_valid(uint32_t timeout)', 'void watch_system_init(void)',
                          'uint32_t watch_idle_timeout_ms(void)', 'esp_err_t watch_set_idle_timeout_ms(uint32_t timeout)'):
            source += function('watch_system.c', signature)
        source += r'''
int main(void){
    saved=120000;watch_system_init();assert(watch_idle_timeout_ms()==120000);
    saved=7;watch_system_init();assert(watch_idle_timeout_ms()==120000);
    saved=0;watch_system_init();assert(watch_idle_timeout_ms()==0);
    s_idle_ms=30000;
    assert(watch_idle_timeout_ms()==30000);
    assert(watch_set_idle_timeout_ms(1)==ESP_ERR_INVALID_ARG && writes==0);
    assert(watch_set_idle_timeout_ms(30000)==0 && writes==0);
    uint32_t values[]={15000,60000,120000,300000,0,30000};
    for(int i=0;i<6;i++){assert(watch_set_idle_timeout_ms(values[i])==0);assert(saved==values[i]);}
    for(failure=1;failure<=3;failure++){
        assert(watch_set_idle_timeout_ms(60000)!=0);assert(watch_idle_timeout_ms()==30000);
    }
    return 0;
}
'''
        run_c(source)

    def test_font_literals_with_urls_and_comments(self):
        from check_font_coverage import literals_cjk, font_codepoints
        with tempfile.TemporaryDirectory(prefix='epw-font-test-') as tmp:
            path = Path(tmp) / 'sample.c'
            path.write_text(r'''
// 注释不可收录 "假字"
const char *url = "https://example.com";
const char *text = "中央气象台";
const char *slashes = "甲/*乙*/丙//丁\\\"戊";
/* "错字" */ const char *last = "萧山";
''')
            self.assertEqual(literals_cjk(path), set('中央气象台甲乙丙丁戊萧山'))
        weather_text = set('萧山')
        self.assertTrue(weather_text <= (literals_cjk(MAIN / 'weather.c') |
                                         literals_cjk(MAIN / 'ui.c')))
        glyphs = font_codepoints(MAIN / 'assets/epw_cjk_20.c')
        self.assertTrue({ord(c) for c in weather_text} <= glyphs)

    def test_home_snapshot_reuse_and_failure_fallback(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {bool hidden;} lv_obj_t;
typedef int lv_draw_buf_t;
#define LV_COLOR_FORMAT_RGB565 1
#define LV_RESULT_OK 0
#define LV_OBJ_FLAG_HIDDEN 1
#define ESP_LOGW(...) ((void)0)
static lv_obj_t home, image = {.hidden=true};
static lv_obj_t *s_home=&home, *s_home_snap_img=&image;
static lv_draw_buf_t buffer, *s_home_snap;
static bool s_home_snap_showing, s_home_snap_valid, s_home_snap_dirty=true;
static bool allocation_fails, render_fails;
static int allocations, captures, sources;
static lv_draw_buf_t *lv_snapshot_create_draw_buf(lv_obj_t *o, int fmt) {
    allocations++; return allocation_fails ? NULL : &buffer;
}
static int lv_snapshot_take_to_draw_buf(lv_obj_t *o, int fmt, lv_draw_buf_t *buf) {
    captures++; return render_fails ? 1 : LV_RESULT_OK;
}
static void lv_image_cache_drop(lv_draw_buf_t *buf) {}
static void lv_image_set_src(lv_obj_t *o, lv_draw_buf_t *buf) {sources++;}
static void lv_obj_add_flag(lv_obj_t *o,int flag) {o->hidden=true;}
static void lv_obj_remove_flag(lv_obj_t *o,int flag) {o->hidden=false;}
'''
        source += function('ui.c', 'static void home_snap_refresh(void)')
        source += function('ui.c', 'static void home_snap_begin(void)')
        source += function('ui.c', 'static void home_snap_end(void)')
        source += r'''
int main(void) {
    home_snap_begin();
    assert(home.hidden && !image.hidden && s_home_snap_showing);
    assert(allocations==1 && captures==1 && sources==1);
    home_snap_begin(); assert(allocations==1 && captures==1);
    home_snap_end(); assert(!home.hidden && image.hidden && !s_home_snap_showing);
    s_home_snap_dirty=true;home_snap_begin(); assert(allocations==1 && captures==1);
    s_home_snap_dirty=true; home_snap_refresh(); assert(captures==1);
    home_snap_end(); home_snap_refresh(); assert(captures==2 && !s_home_snap_dirty);
    render_fails=true; s_home_snap_dirty=true; home_snap_refresh();
    int failed_captures=captures;home_snap_refresh();assert(captures==failed_captures);
    home_snap_begin(); assert(!home.hidden && image.hidden && !s_home_snap_showing);
    render_fails=false; allocation_fails=true; s_home_snap=NULL;
    home_snap_begin(); assert(!home.hidden && image.hidden && !s_home_snap_showing);
    allocation_fails=false; home_snap_begin(); assert(s_home_snap_showing);
    home_snap_end(); home_snap_end(); assert(!home.hidden && image.hidden);
    return 0;
}
'''
        run_c(source)

    def test_mimi_stays_awake_and_other_pages_keep_idle_timeout(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define LV_UNUSED(x) ((void)(x))
#define LV_INDEV_STATE_PRESSED 1
#define UI_IDLE_TIMEOUT_MS timeout
static uint32_t timeout=15000,now,s_last_activity;
static bool s_pet_open=true,s_screen_off,s_center_dragging;
static int sleeps,wakes,pressed;
typedef int lv_timer_t;
typedef int lv_indev_t;
static lv_indev_t input;
static uint32_t lv_tick_get(void){return now;}
static uint32_t lv_tick_elaps(uint32_t t){return now-t;}
static lv_indev_t *bsp_display_get_input_dev(void){return &input;}
static int lv_indev_get_state(lv_indev_t *p){return pressed;}
static void screen_wake(void){wakes++;s_screen_off=false;}
static void screen_sleep(void){sleeps++;s_screen_off=true;}
static bool pages_busy_except_center(void){return false;}
static bool page_anim_busy(void){return false;}
static void home_snap_refresh(void){}
'''
        source += function('ui.c', 'static void idle_timer_cb(lv_timer_t *timer)')
        source += r'''
int main(void){
    for(now=0;now<=300000;now+=100)idle_timer_cb(NULL);
    assert(sleeps==0 && !s_screen_off && timeout==15000);
    s_pet_open=false;now=s_last_activity+14999;idle_timer_cb(NULL);assert(sleeps==0);
    now++;idle_timer_cb(NULL);assert(sleeps==1 && s_screen_off);
    pressed=1;idle_timer_cb(NULL);assert(wakes==1 && !s_screen_off);
    pressed=0;timeout=0;now+=100000;idle_timer_cb(NULL);assert(sleeps==1);
}
'''
        run_c(source)

    def test_wake_consumes_press_and_restores_brightness(self):
        source = r'''#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef int lv_obj_t;
typedef int lv_indev_t;
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_INDEV_STATE_PRESSED 1
#define ESP_LOGI(...) ((void)0)
static bool s_screen_off = true, s_torch_open;
static lv_obj_t wake_obj, *s_wake_layer = &wake_obj;
static uint32_t s_last_activity;
static int pressed = 1, waits, hides, brightness, applies;
static lv_indev_t input;
static lv_indev_t *bsp_display_get_input_dev(void) {return &input;}
static int lv_indev_get_state(lv_indev_t *i) {return pressed;}
static void lv_indev_wait_release(lv_indev_t *i) {waits++;}
static void lv_obj_add_flag(lv_obj_t *o, int flag) {hides++;}
static void bsp_display_brightness_set(int value) {brightness = value;}
static void brightness_apply(void) {applies++;}
static void home_snap_refresh(void) {}
static uint32_t lv_tick_get(void) {return 123;}
'''
        source += function('ui.c', 'static void screen_wake(void)')
        source += r'''
int main(void) {
    screen_wake();
    assert(!s_screen_off && waits == 1 && hides == 1 && applies == 1);
    assert(s_last_activity == 123);
    screen_wake(); assert(waits == 1 && applies == 1);
    s_screen_off = true; s_torch_open = true; pressed = 0;
    screen_wake(); assert(waits == 1 && applies == 2);
    return 0;
}
'''
        run_c(source)

    def test_brightness_writes_use_shared_policy(self):
        source = (ROOT / 'main' / 'ui.c').read_text()
        self.assertEqual(source.count('bsp_display_brightness_set('), 1)
        self.assertIn('bsp_display_brightness_set(brightness_level());', source)

    def test_sleep_brightness_lock_and_restore(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {bool hidden;} lv_obj_t;
static bool s_screen_off, s_sleep_mode, s_torch_open, s_home_snap_dirty;
static int s_brightness=80, actual_brightness, refreshes;
static int s_slider_kind;
#define SLIDER_BRIGHT 0
#define SLIDER_VOLUME 1
static int nursery_volume;
static int nursery_get_volume(void) {return nursery_volume;}
static void nursery_set_volume(int v) {nursery_volume=v;}
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_OPA_COVER 255
#define LV_OPA_30 76
#define COL_TEXT 1
#define COL_SURF 2
#define COL_SLEEP_ACTIVE 3
#define COL_SURF_HI 4
#define ESP_LOGI(...) ((void)0)
#define lv_color_hex(x) (x)
static lv_obj_t deco, time_obj, tile, off_icon, on_icon={.hidden=true};
static lv_obj_t *s_dial_deco=&deco, *s_time=&time_obj, *s_cc_sleep=&tile;
static lv_obj_t *s_cc_sleep_off=&off_icon, *s_cc_sleep_on=&on_icon;
static bool s_center_snap_valid=true;
static void bsp_display_brightness_set(int value) {actual_brightness=value;}
static void slider_refresh(void) {refreshes++;}
static void lv_obj_add_flag(lv_obj_t *o,int flag) {o->hidden=true;}
static void lv_obj_remove_flag(lv_obj_t *o,int flag) {o->hidden=false;}
static void lv_obj_set_style_text_color(lv_obj_t *o,int color,int part) {}
static void lv_obj_set_style_border_opa(lv_obj_t *o,int opa,int part) {}
static void lv_obj_set_style_bg_color(lv_obj_t *o,int color,int part) {}
static void center_snap_invalidate(void) {s_center_snap_valid=false;}
static void center_snap_ensure(void) {s_center_snap_valid=true;}
static void home_snap_refresh(void) {}
'''
        for signature in ('static int brightness_level(void)', 'static void brightness_apply(void)',
                          'static void slider_apply_value(int v)', 'static void sleep_mode_apply(bool on)'):
            source += function('ui.c', signature)
        source += r'''
int main(void) {
    brightness_apply(); assert(actual_brightness==80);
    sleep_mode_apply(true);
    assert(actual_brightness==10 && s_brightness==80 && s_sleep_mode);
    assert(deco.hidden && off_icon.hidden && !on_icon.hidden && s_home_snap_dirty);
    slider_apply_value(95); assert(s_brightness==80 && actual_brightness==10);
    s_torch_open=true; brightness_apply(); assert(actual_brightness==10);
    s_screen_off=true; brightness_apply(); assert(actual_brightness==0);
    s_screen_off=false; s_torch_open=false; brightness_apply(); assert(actual_brightness==10);
    sleep_mode_apply(false);
    assert(actual_brightness==80 && !s_sleep_mode);
    assert(!deco.hidden && !off_icon.hidden && on_icon.hidden);
    s_torch_open=true; brightness_apply(); assert(actual_brightness==100);
    s_torch_open=false; slider_apply_value(35); assert(actual_brightness==35);
    sleep_mode_apply(true); sleep_mode_apply(false); assert(actual_brightness==35);
    slider_apply_value(0); assert(actual_brightness==10);
    s_screen_off=true; sleep_mode_apply(true); assert(actual_brightness==0);
    return 0;
}
'''
        run_c(source)

    def test_weather_validation(self):
        source = r'''#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#define WEATHER_MAX_AGE 7200
'''
        source += '#include "weather.h"\n'
        source += function('weather.c', 'static epw_weather_kind_t weather_kind(const char *info)')
        source += function('weather.c', 'static epw_weather_kind_t forecast_kind(const cJSON *data, time_t now)')
        source += function('weather.c', 'static bool parse_weather(const char *body, time_t now, float *temperature, time_t *published, epw_weather_kind_t *kind)')
        source += r'''
static bool check(const char *code, const char *date, const char *temp, time_t now) {
    char body[512];
    snprintf(body, sizeof(body), "{\"data\":{\"real\":{\"station\":{\"code\":\"%s\"},\"publish_time\":\"%s\",\"weather\":{\"temperature\":%s}}}}", code, date, temp);
    float t = 0; time_t p = 0; epw_weather_kind_t kind;
    bool ok = parse_weather(body, now, &t, &p, &kind);
    if (ok) { assert(fabsf(t-13.4f) < 0.01f); assert(p <= now); }
    return ok;
}
int main(void) {
    setenv("TZ", "CST-8", 1); tzset();
    struct tm now_tm = {.tm_year=126, .tm_mon=10, .tm_mday=17, .tm_hour=11};
    time_t now = mktime(&now_tm);
    assert(check("wdcXE", "2026-11-17 10:40", "13.4", now));
    assert(!check("HIieJ", "2026-11-17 10:40", "13.4", now));
    assert(!check("wdcXE", "2026-11-17 07:00", "13.4", now));
    assert(!check("wdcXE", "2026-11-17 12:00", "13.4", now));
    assert(!check("wdcXE", "2026-02-30 10:40", "13.4", now));
    assert(!check("wdcXE", "2026-11-17 10:40", "9999", now));
    assert(!check("wdcXE", "2026-11-17 10:40", "\"13.4\"", now));
    float t; time_t p; epw_weather_kind_t kind;
    assert(!parse_weather("{}", now, &t, &p, &kind));
    assert(!parse_weather("{", now, &t, &p, &kind));
    assert(weather_kind("晴") == EPW_WEATHER_SUNNY);
    assert(weather_kind("多云") == EPW_WEATHER_PARTLY_CLOUDY);
    assert(weather_kind("多云转晴") == EPW_WEATHER_CLEARING);
    assert(weather_kind("晴转多云") == EPW_WEATHER_PARTLY_CLOUDY);
    assert(weather_kind("小雨") == EPW_WEATHER_RAIN);
    assert(weather_kind("雷阵雨") == EPW_WEATHER_STORM);
    assert(weather_kind("雨夹雪") == EPW_WEATHER_SNOW);
    assert(weather_kind("雾") == EPW_WEATHER_FOG);
    assert(weather_kind("-") == EPW_WEATHER_UNKNOWN);
    assert(weather_kind(NULL) == EPW_WEATHER_UNKNOWN);
    const char *forecast = "{\"predict\":{\"station\":{\"code\":\"wdcXE\"},\"publish_time\":\"2026-11-17 08:00\",\"detail\":[{\"date\":\"2026-11-17\",\"day\":{\"weather\":{\"info\":\"小雨\"}},\"night\":{\"weather\":{\"info\":\"晴\"}}}]}}";
    cJSON *data = cJSON_Parse(forecast); assert(data);
    assert(forecast_kind(data, now) == EPW_WEATHER_RAIN);
    assert(forecast_kind(data, now+8*3600) == EPW_WEATHER_SUNNY);
    assert(forecast_kind(data, now+86400) == EPW_WEATHER_UNKNOWN);
    cJSON *real = cJSON_Parse("{\"station\":{\"code\":\"wdcXE\"},\"publish_time\":\"2026-11-17 10:40\",\"weather\":{\"temperature\":13.4,\"info\":\"-\"}}");
    cJSON_AddItemToObject(data, "real", real);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    char *body = cJSON_PrintUnformatted(root);
    assert(parse_weather(body, now, &t, &p, &kind) && kind == EPW_WEATHER_RAIN);
    free(body);
    cJSON *weather = cJSON_GetObjectItemCaseSensitive(real, "weather");
    cJSON_ReplaceItemInObjectCaseSensitive(weather, "info", cJSON_CreateString("晴"));
    body = cJSON_PrintUnformatted(root);
    assert(parse_weather(body, now, &t, &p, &kind) && kind == EPW_WEATHER_SUNNY);
    free(body);
    cJSON_Delete(root);
    return 0;
}
'''
        run_c(source, json=True)

    def test_repeated_mimi_left_swipe_keeps_current_session(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#define UI_PET_IDLE 0
#define UI_PET_THINKING 1
#define EPW_WIFI_CONNECTED 1
static bool active,ready=true;
static int starts,updates,state,network=EPW_WIFI_CONNECTED;
static bool voice_client_is_active(void){return active;}
static bool voice_client_set_active(bool value){starts++;if(ready)active=value;return ready;}
static void pet_apply(int value,const char *text){updates++;state=value;}
static int wifi_get_state(void){return network;}
'''
        source += function('ui.c', 'static void pet_begin_chat(void)')
        source += r'''
int main(void){
    network=0;pet_begin_chat();assert(!active && starts==0 && updates==1 && state==UI_PET_IDLE);
    network=EPW_WIFI_CONNECTED;updates=0;
    pet_begin_chat();assert(active && starts==1 && updates==1);
    for(int i=0;i<20;i++)pet_begin_chat();
    assert(starts==1 && updates==1);
    active=false;pet_begin_chat();assert(starts==2 && updates==2 && active);
    active=false;ready=false;pet_begin_chat();assert(!active && state==UI_PET_IDLE);
}
'''
        run_c(source)

    def test_protocol_and_session(self):
        self.assertTrue((CJSON / 'cJSON.c').exists(), '先运行 idf.py build 获取依赖')
        source = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "ws_message.h"
static atomic_bool s_requested, s_ready, s_connected, s_hello, s_listening, s_reply_active, s_have_reply;
static atomic_uint s_progress_tick;
static unsigned s_tts_packets,s_tts_bytes;
#define ESP_LOGI(...) ((void)0)
static unsigned xTaskGetTickCount(void){return 100;}

static atomic_uint s_epoch;
static unsigned s_session_epoch;
static char s_session_id[96], reply[512], sent[512];
static int sends;
static void pet_set(const char *emotion, const char *text) {
    if (text) snprintf(reply, sizeof(reply), "%s", text);
}
static bool send_json(const char *text) {
    sends++; snprintf(sent, sizeof(sent), "%s", text); return true;
}
'''
        for signature in ('static bool session_current(void)', 'bool voice_client_is_active(void)',
                          'bool voice_client_set_active(bool active)', 'static void send_listen_start(void)',
                          'static const char *json_string(const cJSON *json, const char *key)',
                          'static void handle_text(const char *data, size_t len)'):
            source += function('voice_client.c', signature)
        source += r'''
int main(void) {
    assert(!voice_client_set_active(true));
    s_ready = true; assert(voice_client_set_active(true));
    s_session_epoch = s_epoch; assert(session_current());
    unsigned epoch=s_epoch;assert(voice_client_set_active(true) && s_epoch==epoch && session_current());
    voice_client_set_active(false); assert(!session_current());
    assert(voice_client_set_active(true)); assert(!session_current());
    s_session_epoch = s_epoch;
    send_listen_start(); assert(sends == 0);
    const char *early = "{\"type\":\"stt\",\"text\":\"不应显示\"}";
    handle_text(early, strlen(early)); assert(reply[0] == 0);
    const char *hello = "{\"type\":\"hello\",\"session_id\":\"id\\\"quoted\"}";
    handle_text(hello, strlen(hello)); assert(sends == 1 && s_listening);
    cJSON *j = cJSON_Parse(sent); assert(j);
    assert(!strcmp(json_string(j, "session_id"), "id\"quoted")); cJSON_Delete(j);
    const char *raw = "{\"type\":\"stt\",\"text\":\"你好\"}";
    handle_text(raw, strlen(raw)); assert(!strcmp(reply, "你好"));
    const char *escaped = "{\"type\":\"stt\",\"text\":\"\\u4f60\\u597d\"}";
    handle_text(escaped, strlen(escaped)); assert(!strcmp(reply, "你好"));
    const char *bad = "{\"type\":\"stt\",\"text\":\"截断";
    handle_text(bad, strlen(bad)); assert(!strcmp(reply, "你好"));
    char nested[128]; memset(nested, '[', 32); memset(nested+32, ']', 32); nested[64]=0;
    assert(cJSON_Parse(nested) == NULL);
    epw_ws_message_t msg = {0};
    size_t n = strlen(raw);
    assert(!epw_ws_message_feed(&msg, 1, true, 0, n, raw, 5));
    assert(epw_ws_message_feed(&msg, 1, true, 5, n, raw + 5, n - 5));
    handle_text(msg.data, msg.used); assert(!strcmp(reply, "你好"));
    assert(!epw_ws_message_feed(&msg, 1, false, 0, 7, raw, 7));
    assert(!epw_ws_message_feed(&msg, 9, true, 0, 1, "x", 1));
    assert(epw_ws_message_feed(&msg, 0, true, 0, n - 7, raw + 7, n - 7));
    assert(!strcmp(msg.data, raw));
    assert(!epw_ws_message_feed(&msg, 1, true, 0, n, raw, 5));
    assert(!epw_ws_message_feed(&msg, 1, true, 6, n, raw + 6, n - 6));
    assert(!msg.collecting);
    char large[EPW_WS_MESSAGE_MAX + 1] = {0};
    assert(!epw_ws_message_feed(&msg, 1, true, 0, sizeof(large), large, sizeof(large)));
    assert(!msg.collecting);
    const char *tts_start="{\"type\":\"tts\",\"state\":\"start\"}";
    handle_text(tts_start,strlen(tts_start));assert(s_reply_active && !s_listening);
    const char *sentence="{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"继续聊天\"}";
    handle_text(sentence,strlen(sentence));assert(s_have_reply && !strcmp(reply,"继续聊天"));
    const char *stop="{\"type\":\"tts\",\"state\":\"stop\"}";
    handle_text(stop,strlen(stop));assert(s_listening && !s_reply_active && !strcmp(reply,"继续聊天"));
    const char *foreign="{\"type\":\"stt\",\"session_id\":\"old\",\"text\":\"不要覆盖\"}";
    handle_text(foreign,strlen(foreign));assert(!strcmp(reply,"继续聊天"));
    const char *empty_hello="{\"type\":\"hello\",\"session_id\":\"\"}";
    handle_text(empty_hello,strlen(empty_hello));assert(!s_requested);
    return 0;
}
'''
        run_c(source, json=True)

    def test_ui_cancel_return_clock_and_power(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct obj {bool hidden, clickable; char text[96]; struct obj *child;} lv_obj_t;
typedef struct {int x,y;} lv_point_t;
typedef int lv_indev_t;
typedef int lv_event_code_t;
typedef struct {int code;} lv_event_t;
typedef int lv_timer_t;
enum {LV_EVENT_PRESSED=1, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST};
#define SCREEN_H 466
#define CENTER_DRAG_TH 8
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_UNUSED(x) (void)(x)
static bool s_center_open,s_center_dragging,s_center_drag_engaged,s_home_snap_dirty,s_nur_drag_engaged;
static int s_center_press_x,s_center_press_y,s_center_press_reveal,s_center_reveal;
static bool s_tools_open;
static void tools_close(void){s_tools_open=false;}
static bool s_wifipwd_open,s_wifi_open,s_apps_open,s_status_open,s_torch_open,s_slider_open,s_pet_open,s_nursery_open;
static int fake_indev, point_x, point_y;
static int s_power_override=-1,s_bat_percent;
static bool s_charging,s_usb_powered;
static int s_last_h=-1,s_last_m=-1,s_last_day=-1;
static lv_obj_t time_obj,date_obj;
static lv_obj_t *s_time=&time_obj,*s_date=&date_obj;
static const char *s_week_cn[]={"周日","周一","周二","周三","周四","周五","周六"};
static struct tm fake_tm;
static time_t fake_time(time_t *t){return 0;}
static struct tm *fake_localtime(const time_t *t,struct tm *out){*out=fake_tm;return out;}
#define time fake_time
#define localtime_r fake_localtime
static bool pages_busy_except_center(void){return false;}
static bool page_anim_busy(void){return false;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &fake_indev;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){p->x=point_x;p->y=point_y;}
static void center_anim_stop(void){}
static void center_snap_ensure(void){}
static void center_drawer_begin(void){}
static void center_reveal_set(int h){s_center_reveal=h<0?0:h>SCREEN_H?SCREEN_H:h;}
static void center_open(void){s_center_open=true;}
static void center_animate_reveal(int to,int b,int c,int d){s_center_reveal=to;}
static int lv_anim_path_ease_in,center_hide_done;
static void wifi_pwd_close(void){s_wifipwd_open=false;}
static void wifi_close(void){s_wifi_open=false;}
static void apps_close(void){s_apps_open=false;}
static void apps_open(void){s_apps_open=true;}
static void status_close(void){s_status_open=false;}
static void torch_close(void){s_torch_open=false;}
static void slider_close(void){s_slider_open=false;}
static void pet_close(void){s_pet_open=false;}
static void nursery_close(void){s_nursery_open=false;}
static void center_close(void){s_center_open=false;}
static void battery_visuals(void){}
static void lv_obj_add_flag(lv_obj_t *o,int flag){if(flag==LV_OBJ_FLAG_HIDDEN)o->hidden=true;}
static void lv_obj_remove_flag(lv_obj_t *o,int flag){if(flag==LV_OBJ_FLAG_HIDDEN)o->hidden=false;else o->clickable=false;}
static unsigned lv_obj_get_child_count(lv_obj_t *o){return o->child!=NULL;}
static lv_obj_t *lv_obj_get_child(lv_obj_t *o,unsigned i){return o->child;}
static void lv_label_set_text(lv_obj_t *o,const char *s){snprintf(o->text,sizeof(o->text),"%s",s);}
#define lv_label_set_text_fmt(o,...) snprintf((o)->text,sizeof((o)->text),__VA_ARGS__)
'''
        for signature in ('static void home_pull_cb(lv_event_t *e)', 'void ui_demo_toggle_apps(void)',
                          'static void clear_clickable_tree(lv_obj_t *o)',
                          'void ui_update_battery(int percent, bool charging, bool usb_powered)',
                          'static void time_tick_cb(lv_timer_t *timer)'):
            source += function('ui.c', signature)
        source += r'''
int main(void){
    lv_event_t e={LV_EVENT_PRESSED}; home_pull_cb(&e);
    e.code=LV_EVENT_PRESSING;point_y=200;home_pull_cb(&e);
    point_y=0;home_pull_cb(&e);assert(s_center_reveal==0);
    e.code=LV_EVENT_RELEASED;home_pull_cb(&e);assert(!s_center_open);
    e.code=LV_EVENT_PRESSED;home_pull_cb(&e);
    e.code=LV_EVENT_PRESSING;point_y=200;home_pull_cb(&e);
    e.code=LV_EVENT_RELEASED;home_pull_cb(&e);assert(s_center_open);
    /* 中部起手：纵向拖动不应拉开控制中心 */
    s_center_open=false;s_center_reveal=0;point_x=233;point_y=300;
    e.code=LV_EVENT_PRESSED;home_pull_cb(&e);
    point_y=420;e.code=LV_EVENT_PRESSING;home_pull_cb(&e);assert(s_center_reveal==0);
    e.code=LV_EVENT_RELEASED;home_pull_cb(&e);assert(!s_center_open);
    /* 顶部横滑（右往左）：竖向位移小于横向时不抢手势 */
    point_x=420;point_y=20;e.code=LV_EVENT_PRESSED;home_pull_cb(&e);
    point_x=300;point_y=30;e.code=LV_EVENT_PRESSING;home_pull_cb(&e);assert(s_center_reveal==0);
    e.code=LV_EVENT_RELEASED;home_pull_cb(&e);assert(!s_center_open);
    s_tools_open=true;s_status_open=true;ui_demo_toggle_apps();assert(!s_tools_open && s_status_open);
    s_status_open=false;
    s_center_open=false;s_apps_open=true;s_status_open=true;ui_demo_toggle_apps();
    assert(!s_status_open && s_apps_open);ui_demo_toggle_apps();assert(!s_apps_open);
    s_center_open=true;s_torch_open=true;ui_demo_toggle_apps();assert(!s_torch_open && s_center_open);
    lv_obj_t grand={.clickable=true},child={.clickable=true,.child=&grand},root={.clickable=true,.child=&child};
    clear_clickable_tree(&root);assert(root.clickable && !child.clickable && !grand.clickable);
    ui_update_battery(50,false,true);assert(!s_charging && s_usb_powered);
    s_home_snap_dirty=false;ui_update_battery(50,false,true);assert(!s_home_snap_dirty);
    ui_update_battery(50,true,true);assert(s_charging && s_usb_powered && s_home_snap_dirty);
    fake_tm.tm_year=70;time_tick_cb(NULL);assert(time_obj.hidden && !strcmp(date_obj.text,"等待校时"));
    fake_tm=(struct tm){.tm_year=126,.tm_mon=0,.tm_mday=17,.tm_yday=16,.tm_wday=6,.tm_hour=12};
    time_tick_cb(NULL);assert(!time_obj.hidden && !strcmp(time_obj.text,"12:00"));
    assert(!strcmp(date_obj.text,"1月17日 周六"));
    assert(s_home_snap_dirty);s_home_snap_dirty=false;time_tick_cb(NULL);assert(!s_home_snap_dirty);
    fake_tm.tm_mon=1;fake_tm.tm_yday=47;fake_tm.tm_wday=2;time_tick_cb(NULL);
    assert(!strcmp(date_obj.text,"2月17日 周二"));
    return 0;
}
'''
        run_c(source)

    def test_wifi_save_only_after_success(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
typedef int esp_err_t;
typedef int esp_event_base_t;
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define portMAX_DELAY 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define IPSTR "%u"
#define IP2STR(p) 0u
#define CONFIG_EPW_WIFI_MAX_RETRY 20
#define WIFI_IF_STA 0
#define WIFI_AUTH_OPEN 0
#define WPA3_SAE_PWE_BOTH 0
#define WIFI_EVENT 1
#define IP_EVENT 2
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
enum {WIFI_REASON_AUTH_FAIL=1,WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,WIFI_REASON_HANDSHAKE_TIMEOUT,
 WIFI_REASON_NO_AP_FOUND,WIFI_REASON_AUTH_EXPIRE,WIFI_REASON_ASSOC_FAIL,WIFI_REASON_CONNECTION_FAIL,WIFI_REASON_ASSOC_LEAVE};
enum {EPW_WIFI_OFF,EPW_WIFI_CONNECTING,EPW_WIFI_CONNECTED,EPW_WIFI_FAILED};
typedef struct {struct {unsigned char ssid[32],password[64];struct{int authmode;}threshold;int sae_pwe_h2e;struct{bool capable,required;}pmf_cfg;}sta;}wifi_config_t;
typedef struct {unsigned char reason;}wifi_event_sta_disconnected_t;
typedef struct {struct{int ip;}ip_info;}ip_event_got_ip_t;
static bool s_inited=true,s_have_cred=true,s_user_connect,s_auto_retry=true,s_hinted_no_ap,s_ignore_leave,s_scan_running;
static int s_control_lock,s_retry_num,s_state=EPW_WIFI_CONNECTED,saves;
static char s_ssid[33]="good",s_pass[65]="goodpass",s_ip[16],saved_ssid[33]="good";
typedef struct {unsigned char ssid[33];}wifi_ap_record_t;
static wifi_config_t last_config;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){memset(ap,0,sizeof(*ap));memcpy(ap->ssid,last_config.sta.ssid,32);return ESP_OK;}
static int xSemaphoreTake(int a,int b){return 1;}
static int xSemaphoreGive(int a){return 1;}
static void wifi_init_sta(void){}
static int esp_wifi_disconnect(void){return ESP_OK;}
static int connects;
static int esp_wifi_connect(void){connects++;return ESP_OK;}
static int esp_wifi_set_config(int iface,wifi_config_t *cfg){last_config=*cfg;return ESP_OK;}
static void creds_save(const char *ssid,const char *pass){saves++;snprintf(saved_ssid,sizeof(saved_ssid),"%s",ssid);}
static void sntp_start_once(void){}
static void configure_dns(void){}
'''
        for signature in ('static bool disconnect_is_fatal(uint8_t reason)',
                          'static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)',
                          'esp_err_t wifi_connect_to(const char *ssid, const char *password)'):
            source += function('wifi.c', signature)
        source += r'''
int main(void){
    s_scan_running=true;
    wifi_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
    assert(connects==0);
    wifi_event_sta_disconnected_t lost={.reason=WIFI_REASON_NO_AP_FOUND};
    wifi_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&lost);
    assert(s_state==EPW_WIFI_OFF && connects==0 && s_auto_retry);
    s_scan_running=false;
    assert(wifi_connect_to("bad","badpass1")==ESP_OK);assert(saves==0);
    wifi_event_sta_disconnected_t left={.reason=WIFI_REASON_ASSOC_LEAVE};
    wifi_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&left);
    assert(s_state==EPW_WIFI_CONNECTING && s_user_connect);
    wifi_event_sta_disconnected_t failed={.reason=WIFI_REASON_AUTH_FAIL};
    wifi_event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&failed);
    assert(s_state==EPW_WIFI_FAILED && saves==0 && !strcmp(saved_ssid,"good"));
    assert(wifi_connect_to("new","newpass1")==ESP_OK);
    ip_event_got_ip_t ip={0};wifi_event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);
    assert(saves==1 && !strcmp(saved_ssid,"new"));
    char ssid[33],key[65];memset(ssid,'s',32);ssid[32]=0;memset(key,'a',64);key[64]=0;
    assert(wifi_connect_to(ssid,key)==ESP_OK);
    assert(!memcmp(last_config.sta.ssid,ssid,32) && !memcmp(last_config.sta.password,key,64));
    key[63]='z';assert(wifi_connect_to(ssid,key)==ESP_ERR_INVALID_ARG);
    assert(wifi_connect_to("new","short")==ESP_ERR_INVALID_ARG);
    return 0;
}
'''
        run_c(source)

    def test_wifi_scan_interrupts_connect_and_keeps_saved_network(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define WIFI_AUTH_OPEN 0
#define EPW_WIFI_MAX_APS 16
#define portMAX_DELAY 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
enum {EPW_WIFI_OFF,EPW_WIFI_CONNECTING,EPW_WIFI_CONNECTED,EPW_WIFI_FAILED};
typedef struct {const char *ssid,*bssid;int channel;bool show_hidden;}wifi_scan_config_t;
typedef struct {char ssid[33];int rssi;bool secure;}epw_wifi_ap_t;
typedef struct {char ssid[33];int rssi,authmode;}wifi_ap_record_t;
static bool s_scan_running=true,s_scan_ready,s_scan_failed,s_auto_retry=true,s_have_cred=true,s_ignore_leave,s_user_connect;
static int s_control_lock,s_scan_lock=1,s_state=EPW_WIFI_CONNECTING,s_retry_num,s_scan_count;
static epw_wifi_ap_t s_scan_aps[16];
static int scan_error,disconnects,connects;
static int xSemaphoreTake(int a,int b){return 1;}
static void xSemaphoreGive(int a){}
static int esp_wifi_disconnect(void){disconnects++;return 0;}
static int esp_wifi_connect(void){connects++;return 0;}
static int esp_wifi_scan_start(wifi_scan_config_t *cfg,bool blocking){assert(s_state!=EPW_WIFI_CONNECTING);return scan_error;}
static void esp_wifi_scan_get_ap_num(uint16_t *n){*n=3;}
static int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *r){
    strcpy(r[0].ssid,"other");r[0].rssi=-70;r[0].authmode=1;
    strcpy(r[1].ssid,"other");r[1].rssi=-40;
    strcpy(r[2].ssid,"new");r[2].rssi=-30;return 0;
}
static void esp_wifi_clear_ap_list(void){}
static void vTaskDelete(void *p){}
'''
        source += function('wifi.c', 'static void scan_task(void *arg)')
        source += function('wifi.c', 'int wifi_scan_copy(epw_wifi_ap_t *out, int max)')
        source += r'''
int main(void){
    scan_task(NULL);assert(disconnects==1 && connects==1 && s_have_cred && !s_scan_running);
    epw_wifi_ap_t out[16];assert(wifi_scan_copy(out,16)==2);
    assert(!strcmp(out[0].ssid,"new") && !strcmp(out[1].ssid,"other") && out[1].rssi==-40);
    s_state=EPW_WIFI_CONNECTED;s_scan_running=true;scan_task(NULL);
    assert(disconnects==1 && connects==1 && s_state==EPW_WIFI_CONNECTED);
    scan_error=-1;s_scan_running=true;scan_task(NULL);assert(wifi_scan_copy(out,16)==-2);
    s_state=EPW_WIFI_CONNECTING;s_auto_retry=false;s_scan_running=true;scan_task(NULL);
    assert(connects==1 && s_state==EPW_WIFI_OFF && s_have_cred);
}
'''
        run_c(source)

    def test_microphone_success_cancel_and_handshake(self):
        source = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ws_message.h"
#define VOICE_MAX_PCM 1920
#define VOICE_MAX_OPUS 2048
#define EPW_WIFI_CONNECTED 1
#define ESP_AUDIO_ERR_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_CODEC_DEV_OK 0
#define pdMS_TO_TICKS(x) (x)
typedef struct {uint8_t *buffer;uint32_t len;}esp_audio_enc_in_frame_t;
typedef struct {uint8_t *buffer;uint32_t len;unsigned encoded_bytes;}esp_audio_enc_out_frame_t;
static atomic_bool s_requested,s_connected,s_hello,s_listening,s_reply_active;
static atomic_uint s_progress_tick;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static uint32_t xTaskGetTickCount(void){return 0;}
static void *heap_caps_calloc(size_t n,size_t bytes,int caps){assert(caps==3);return calloc(n,bytes);}
static void *heap_caps_malloc(size_t bytes,int caps){assert(caps==3);return malloc(bytes);}

static atomic_uint s_epoch;
static unsigned s_session_epoch;
static epw_ws_message_t *s_message;
static void *s_enc,*s_ws,*s_mic;
static int mode,reads,encodes,sends,opens,closes;
static jmp_buf finished;
static int wifi_get_state(void){return EPW_WIFI_CONNECTED;}
static void vTaskDelay(int ticks){if(!s_requested)longjmp(finished,1);s_requested=false;}
static void pet_set(const char *emotion,const char *text){}
static bool voice_client_set_active(bool value){s_requested=value;return value;}
static bool ws_start(void){s_connected=true;s_hello=mode!=2;s_listening=true;return true;}
static bool open_audio(void){opens++;s_enc=(void*)1;return true;}
static int esp_opus_enc_get_frame_size(void *enc,int *in,int *out){*in=1920;*out=128;return 0;}
static int esp_codec_dev_read(void *mic,void *pcm,int len){reads++;memset(pcm,1,len);if(mode==1)s_requested=false;if(mode==4 && reads==2)s_listening=true;return mode==3?1:0;}
static int esp_opus_enc_process(void *enc,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out){encodes++;assert(in->len==1920);out->encoded_bytes=8;return 0;}
static int esp_websocket_client_send_bin(void *ws,const char *data,int len,int timeout){assert(len==8);sends++;if(mode==4 && sends==1)s_listening=false;else s_requested=false;return len;}
static void close_audio(void){s_enc=NULL;}
static void close_session(void){closes++;free(s_message);s_message=NULL;close_audio();}
'''
        source += function('voice_client.c', 'static bool session_current(void)')
        source += function('voice_client.c', 'static bool voice_wait_expired(uint32_t now, uint32_t hello_since)')
        source += function('voice_client.c', 'static void voice_task(void *arg)')
        source += r'''
int main(void){
    for(mode=0;mode<5;mode++){
        reads=encodes=sends=opens=closes=0;s_requested=true;s_epoch=1;s_enc=NULL;
        if(setjmp(finished)==0)voice_task(NULL);
        assert(closes==1);
        if(mode==0){assert(reads==1 && encodes==1 && sends==1);}
        else if(mode==4){assert(reads==3 && encodes==2 && sends==2);}
        else if(mode==2){assert(opens==0 && reads==0 && sends==0);}
        else{assert(reads==1 && encodes==0 && sends==0);}
    }
    return 0;
}
'''
        run_c(source)

    def test_voice_wait_timeouts_and_tick_wrap(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#define pdMS_TO_TICKS(x) (x)
static atomic_bool s_hello,s_reply_active;
static atomic_uint s_progress_tick;
'''
        source += function('voice_client.c', 'static bool voice_wait_expired(uint32_t now, uint32_t hello_since)')
        source += r'''
int main(void){
    assert(!voice_wait_expired(19999,0));assert(voice_wait_expired(20000,0));
    assert(!voice_wait_expired(5,UINT32_MAX-15));
    assert(voice_wait_expired(20000,UINT32_MAX-15));
    s_hello=true;assert(!voice_wait_expired(999999,0));
    s_reply_active=true;s_progress_tick=1000;
    assert(!voice_wait_expired(45999,0));assert(voice_wait_expired(46000,0));
    s_progress_tick=45500;assert(!voice_wait_expired(46000,0));return 0;
}
'''
        run_c(source)

    def test_audio_starts_muted_but_accepts_manual_volume(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#define pdTRUE 1
#define portMAX_DELAY 0
#define NULL ((void *)0)
typedef int SemaphoreHandle_t;
static int s_play_volume, hardware_volume=-1,s_spk;
static bool s_play_open=true,lock_available=true;
static int audio_lock(void){return lock_available?1:0;}
static int xSemaphoreTake(int lock,int timeout){return 1;}
static void xSemaphoreGive(int lock){}
static void esp_codec_dev_set_out_vol(int device,int value){hardware_volume=value;}
'''
        source += function('audio.c', 'void audio_set_playback_volume(int volume)')
        source += function('audio.c', 'int audio_get_playback_volume(void)')
        source += r'''
int main(void){
    int values[]={0,60,100,-1,999};
    int expected[]={0,60,100,0,100};
    assert(audio_get_playback_volume()==0);
    for(int i=0;i<5;i++){audio_set_playback_volume(values[i]);assert(audio_get_playback_volume()==expected[i] && hardware_volume==expected[i]);}
    lock_available=false;audio_set_playback_volume(20);assert(audio_get_playback_volume()==100);
    lock_available=true;s_play_open=false;audio_set_playback_volume(25);
    assert(audio_get_playback_volume()==25 && hardware_volume==100);return 0;
}
'''
        run_c(source)
        self.assertIn('#define EPW_AUDIO_PLAY_VOLUME 0', (MAIN / 'audio.c').read_text())
        init = function('nursery.c', 'void nursery_init(void)')
        self.assertIn('uint8_t volume = 30;', init)
        self.assertIn('nvs_get_u8(s_nvs, "volume", &volume)', init)
        self.assertNotIn('audio_set_playback_volume(0);', init)

    def test_nursery_volume_restore_and_save(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
static int s_nvs,stored=255,commits,error;
static bool s_nvs_ok=true,available=true;
static struct {uint8_t volume;}s_cfg;
static int playback_volume;
static int clamp_int(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static int nvs_get_u8(int h,const char *key,uint8_t *v){if(stored==255)return -1;*v=stored;return 0;}
static int nvs_set_u8(int h,const char *key,uint8_t v){if(error)return error;stored=v;return 0;}
static int nvs_commit(int h){commits++;return 0;}
static bool nvs_take(void){return available;}
static void nvs_give(void){}
static void audio_set_playback_volume(int v){playback_volume=v;}
static int audio_get_playback_volume(void){return playback_volume;}
'''
        source += function('nursery.c', 'void nursery_save_volume(void)')
        init = function('nursery.c', 'void nursery_init(void)')
        start = init.index('    uint8_t volume = 30;')
        end = init.index('\n\n', start)
        source += 'static void restore(void){\n' + init[start:end] + '\n}\n'
        source += r'''
int main(void){
    restore();assert(playback_volume==30);
    s_cfg.volume=45;nursery_save_volume();restore();assert(playback_volume==45 && commits==1);
    s_cfg.volume=0;nursery_save_volume();restore();assert(playback_volume==0);
    stored=200;restore();assert(playback_volume==100);
    error=-1;int before=commits;nursery_save_volume();assert(commits==before);
    available=false;nursery_save_volume();assert(commits==before);
}
'''
        run_c(source)

    def test_playback_write_reports_written_bytes(self):
        # esp_codec_dev_write 成功返回 ESP_CODEC_DEV_OK(0) 而不是字节数，
        # audio_playback_write 必须把 0 翻译成实际写入字节数，否则上层会误判失败。
        source = r'''#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define pdTRUE 1
#define portMAX_DELAY 0
#define NULL ((void *)0)
#define ESP_CODEC_DEV_OK 0
#define ESP_LOGW(tag, fmt, ...) do {} while (0)
#define TAG "test"
typedef int SemaphoreHandle_t;
static int s_spk, write_result;
static bool s_play_open = true;
static size_t written_bytes;
static int audio_lock(void){return 1;}
static int xSemaphoreTake(int lock, int timeout){return 1;}
static void xSemaphoreGive(int lock){}
static int esp_codec_dev_write(int dev, void *pcm, int bytes){written_bytes=(size_t)bytes;return write_result;}
'''
        source += function('audio.c', 'int audio_playback_write(const void *pcm, size_t bytes)')
        source += r'''
int main(void){
    int16_t pcm[8]={0};
    write_result=ESP_CODEC_DEV_OK;
    assert(audio_playback_write(pcm,sizeof(pcm))==(int)sizeof(pcm));
    assert(written_bytes==sizeof(pcm));
    write_result=-1;
    assert(audio_playback_write(pcm,sizeof(pcm))==-1);
    s_play_open=false;
    write_result=ESP_CODEC_DEV_OK;
    assert(audio_playback_write(pcm,sizeof(pcm))==-1);
    assert(audio_playback_write(NULL,sizeof(pcm))==0);
    return 0;
}
'''
        run_c(source)

    def test_muted_tts_binary_fragments_never_open_output(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "ws_message.h"
typedef const char *esp_event_base_t;
typedef struct {int data_len,payload_len,payload_offset,op_code;bool fin;const char *data_ptr;} esp_websocket_event_data_t;
#define WEBSOCKET_EVENT_DISCONNECTED 1
#define WEBSOCKET_EVENT_CLOSED 2
#define WEBSOCKET_EVENT_CONNECTED 3
#define WEBSOCKET_EVENT_DATA 4
static atomic_bool s_connected,s_hello,s_listening,s_reply_active,s_have_reply;
static atomic_uint s_progress_tick;
static unsigned s_tts_packets,s_tts_bytes,texts;
static char s_session_id[96];
static epw_ws_message_t message,*s_message=&message;
static bool current=true;
static bool session_current(void){return current;}
static void pet_set(const char *a,const char *b){}
static bool send_json(const char *text){return true;}
static unsigned xTaskGetTickCount(void){return 123;}
static void handle_text(const char *data,size_t len){texts++;}
'''
        source += function('voice_client.c', 'static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)')
        source += r'''
int main(void){
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_CONNECTED,NULL);s_hello=true;
    esp_websocket_event_data_t part={.data_len=2,.payload_len=5,.op_code=2,.fin=true,.data_ptr="ab"};
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);assert(s_tts_packets==0);
    part.payload_offset=2;part.data_len=3;part.data_ptr="cde";
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);
    assert(s_tts_packets==1 && s_tts_bytes==5 && texts==0 && s_progress_tick==123);
    part=(esp_websocket_event_data_t){.data_len=2,.payload_len=2,.op_code=1,.fin=true,.data_ptr="{}"};
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);assert(texts==1 && s_tts_packets==1);
    s_message=NULL;ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DISCONNECTED,NULL);
    assert(!s_hello && !s_connected);return 0;
}
'''
        run_c(source)

    def test_http_date_utc_and_invalid_dates(self):
        run_c(r'''
#include <assert.h>
#include "http_date.h"
int main(void) {
    time_t t = epw_http_date("Tue, 17 Nov 2026 02:54:19 GMT");
    assert(t > 0);
    struct tm *utc = gmtime(&t);
    assert(utc->tm_year == 126 && utc->tm_mon == 10 && utc->tm_mday == 17);
    assert(utc->tm_hour == 2 && utc->tm_min == 54 && utc->tm_sec == 19);
    assert(epw_http_date("Thu, 29 Feb 2024 00:00:00 GMT") > 0);
    assert(epw_http_date("Sat, 29 Feb 2025 00:00:00 GMT") == 0);
    assert(epw_http_date("Tue, 17 Nov 2026 24:00:00 GMT") == 0);
    assert(epw_http_date("Tue, 17 Nov 2026 02:54:19 CST") == 0);
    assert(epw_http_date("Tue, 17 Nov 2026 02:54:19 GMT junk") == 0);
    assert(epw_http_date("bad") == 0 && epw_http_date(NULL) == 0);
    return 0;
}
''')

    def test_keyboard_all_printable_ascii(self):
        source = (MAIN / 'ui.c').read_text()
        characters = {' '}
        for match in re.finditer(r'kp_map_(\w+)\[\] = \{(.*?)\n\};', source, re.S):
            tokens = [ast.literal_eval('"' + v + '"') for v in re.findall(r'"((?:[^"\\]|\\.)*)"', match[2])]
            characters |= {v for v in tokens if len(v) == 1 and v != '\n'}
            controls = re.search(r'kp_ctrl_' + match[1] + r'\[\] = \{(.*?)\n\};', source, re.S)
            self.assertEqual(len(re.findall(r'\b\d+\b', controls[1])), len([v for v in tokens if v not in ('','\n')]))
        self.assertEqual(set(map(chr, range(32,127))) - characters, set())


if __name__ == '__main__':
    unittest.main(verbosity=2)
