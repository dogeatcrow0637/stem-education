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
            command += ['-DCJSON_NESTING_LIMIT=16', '-I', str(CJSON), str(CJSON / 'cJSON.c'), '-lm']
        subprocess.run(command, check=True)
        subprocess.run([str(Path(tmp) / 'test')], check=True)


class FirmwareTests(unittest.TestCase):
    def test_protocol_and_session(self):
        self.assertTrue((CJSON / 'cJSON.c').exists(), '先运行 idf.py build 获取依赖')
        source = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "ws_message.h"
static atomic_bool s_requested, s_ready, s_connected, s_hello, s_listening;
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
static bool s_center_open,s_center_dragging,s_center_drag_engaged;
static int s_center_press_y,s_center_press_reveal,s_center_reveal;
static bool s_wifipwd_open,s_wifi_open,s_apps_open,s_status_open,s_torch_open,s_slider_open,s_pet_open;
static int fake_indev, point_y;
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
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){p->x=0;p->y=point_y;}
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
    s_center_open=false;s_apps_open=true;s_status_open=true;ui_demo_toggle_apps();
    assert(!s_status_open && s_apps_open);ui_demo_toggle_apps();assert(!s_apps_open);
    s_center_open=true;s_torch_open=true;ui_demo_toggle_apps();assert(!s_torch_open && s_center_open);
    lv_obj_t grand={.clickable=true},child={.clickable=true,.child=&grand},root={.clickable=true,.child=&child};
    clear_clickable_tree(&root);assert(root.clickable && !child.clickable && !grand.clickable);
    ui_update_battery(50,false,true);assert(!s_charging && s_usb_powered);
    ui_update_battery(50,true,true);assert(s_charging && s_usb_powered);
    fake_tm.tm_year=70;time_tick_cb(NULL);assert(time_obj.hidden && !strcmp(date_obj.text,"等待校时"));
    fake_tm=(struct tm){.tm_year=126,.tm_mon=0,.tm_mday=17,.tm_yday=16,.tm_wday=6,.tm_hour=12};
    time_tick_cb(NULL);assert(!time_obj.hidden && !strcmp(time_obj.text,"12:00"));
    assert(!strcmp(date_obj.text,"1月17日 周六"));
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
static bool s_inited=true,s_have_cred=true,s_user_connect,s_auto_retry=true,s_hinted_no_ap,s_ignore_leave;
static int s_control_lock,s_retry_num,s_state=EPW_WIFI_CONNECTED,saves;
static char s_ssid[33]="good",s_pass[65]="goodpass",s_ip[16],saved_ssid[33]="good";
typedef struct {unsigned char ssid[33];}wifi_ap_record_t;
static wifi_config_t last_config;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){memset(ap,0,sizeof(*ap));memcpy(ap->ssid,last_config.sta.ssid,32);return ESP_OK;}
static int xSemaphoreTake(int a,int b){return 1;}
static int xSemaphoreGive(int a){return 1;}
static void wifi_init_sta(void){}
static int esp_wifi_disconnect(void){return ESP_OK;}
static int esp_wifi_connect(void){return ESP_OK;}
static int esp_wifi_set_config(int iface,wifi_config_t *cfg){last_config=*cfg;return ESP_OK;}
static void creds_save(const char *ssid,const char *pass){saves++;snprintf(saved_ssid,sizeof(saved_ssid),"%s",ssid);}
static void sntp_start_once(void){}
'''
        for signature in ('static bool disconnect_is_fatal(uint8_t reason)',
                          'static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)',
                          'esp_err_t wifi_connect_to(const char *ssid, const char *password)'):
            source += function('wifi.c', signature)
        source += r'''
int main(void){
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
#define ESP_CODEC_DEV_OK 0
#define pdMS_TO_TICKS(x) (x)
typedef struct {uint8_t *buffer;uint32_t len;}esp_audio_enc_in_frame_t;
typedef struct {uint8_t *buffer;uint32_t len;unsigned encoded_bytes;}esp_audio_enc_out_frame_t;
static atomic_bool s_requested,s_connected,s_hello,s_listening;
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
static int esp_codec_dev_read(void *mic,void *pcm,int len){reads++;memset(pcm,1,len);if(mode==1)s_requested=false;return mode==3?1:0;}
static int esp_opus_enc_process(void *enc,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out){encodes++;assert(in->len==1920);out->encoded_bytes=8;return 0;}
static int esp_websocket_client_send_bin(void *ws,const char *data,int len,int timeout){assert(len==8);sends++;s_requested=false;return len;}
static void close_audio(void){s_enc=NULL;}
static void close_session(void){closes++;free(s_message);s_message=NULL;close_audio();}
'''
        source += function('voice_client.c', 'static bool session_current(void)')
        source += function('voice_client.c', 'static void voice_task(void *arg)')
        source += r'''
int main(void){
    for(mode=0;mode<4;mode++){
        reads=encodes=sends=opens=closes=0;s_requested=true;s_epoch=1;s_enc=NULL;
        if(setjmp(finished)==0)voice_task(NULL);
        assert(closes==1);
        if(mode==0){assert(reads==1 && encodes==1 && sends==1);}
        else if(mode==2){assert(opens==0 && reads==0 && sends==0);}
        else{assert(reads==1 && encodes==0 && sends==0);}
    }
    return 0;
}
'''
        run_c(source)

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
