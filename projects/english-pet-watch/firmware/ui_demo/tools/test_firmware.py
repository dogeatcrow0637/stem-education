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


# TTS 泵的最小运行环境：复用生产函数，只替换队列、解码与音频写入。
TTS_PUMP_HARNESS = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define VOICE_BARGE_SAMPLES 512
#define ESP_CODEC_DEV_OK 0
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define VOICE_MAX_PCM 1920
#define VOICE_MAX_OPUS 2048
#define VOICE_SAMPLE_RATE 16000
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_AUDIO_ERR_OK 0
#define ESP_OPUS_DEC_FRAME_DURATION_120_MS 8
#define ESP_OPUS_DEC_CONFIG_DEFAULT() {0}
#define CONFIG_EPW_VOICE_BARGE_IN 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define VOICE_PREBUFFER_MIN 6
#define VOICE_PREBUFFER_MAX 16
#define VOICE_PREBUFFER_STEP 4
#define VOICE_PREBUFFER_FIRST 3
typedef struct {size_t bytes;uint8_t data[];} voice_packet_t;
typedef struct {int sample_rate,channel,frame_duration;}esp_opus_dec_cfg_t;
typedef struct {uint8_t *buffer;size_t len,consumed;}esp_audio_dec_in_raw_t;
typedef struct {uint8_t *buffer;size_t len,decoded_size;}esp_audio_dec_out_frame_t;
typedef int esp_audio_dec_info_t;
static void *s_tts_queue=(void*)1,*s_dec;
static uint8_t *s_tts_pcm;
static uint32_t s_tts_written_samples;
static bool s_tts_output,s_tts_buffered,s_barge_ready,active=true;
static atomic_bool s_first_reply;
static atomic_uint s_prebuffer_target=VOICE_PREBUFFER_MIN;
static void *s_enc=(void*)1;static int s_mic;
static atomic_bool s_tts_end,s_reply_active;
static voice_packet_t *queue[64];static int queued,writes,listens,closed,dec_closed;
static bool session_current(void){return active;}
static bool open_audio(void){return true;}
static int audio_get_playback_volume(void){return 0;}
static bool voice_barge_begin(void){return true;}
static int esp_codec_dev_read(int dev,void *p,size_t n){memset(p,0,n);return 0;}
static bool voice_barge_feed(void *m,const void *r,size_t n){return false;}
static unsigned uxQueueMessagesWaiting(void *q){return queued;}
static void *heap_caps_malloc(size_t n,int caps){return malloc(n);}
static int xQueueSend(void *q,voice_packet_t **p,int wait){if(queued==64)return 0;queue[queued++]=*p;return 1;}
static int xQueueReceive(void *q,voice_packet_t **p,int wait){if(!queued)return 0;*p=queue[0];memmove(queue,queue+1,--queued*sizeof(*queue));return 1;}
static void caption_pump(void){}
static void captions_clear(void){s_tts_written_samples=0;}
static void audio_playback_close(void){closed++;}
static int audio_playback_open(void){return 1;}
static int esp_opus_dec_open(void *cfg,size_t n,void **d){*d=(void*)1;return 0;}
static int esp_opus_dec_close(void *d){dec_closed++;return 0;}
static int esp_opus_dec_decode(void *d,esp_audio_dec_in_raw_t *raw,esp_audio_dec_out_frame_t *frame,esp_audio_dec_info_t *info){raw->consumed=raw->len;frame->decoded_size=1920;return 0;}
static int audio_playback_write(void *pcm,size_t n){assert(n<=1024 && n>0);writes++;return n;}
static void pet_set(const char *a,const char *b){}
static bool voice_client_set_active(bool a){active=a;return a;}
static void vTaskDelay(int n){}
static void send_listen_start(void){listens++;}
static bool watch_control_pending(void){return false;}
static bool s_control_complete;
static bool tts_interrupt(bool with_preroll){return true;}
'''


class FirmwareTests(unittest.TestCase):
    def test_production_voice_has_no_automatic_test_commands(self):
        main = (MAIN / 'main.c').read_text()
        voice = (MAIN / 'voice_client.c').read_text()
        for source in (main, voice, (MAIN / 'watch_control.c').read_text()):
            self.assertNotIn('QUICK_QA', source)
            self.assertNotIn('qa_', source)
        self.assertNotIn('voice_client_set_active(true)', main)
        self.assertNotIn(r'\"detect\"', voice)

    def test_lunar_calendar_boundaries_and_leap_month(self):
        # 香港天文台 T2024e/T2025e/T2026e/T2033e/T2050e.txt 独立日期样例。
        run_c((MAIN / 'lunar_date.c').read_text() + r"""
#include <assert.h>
#include <string.h>
int main(void){
 char out[64];
 struct {int y,m,d;const char *text;} cases[]={
  {2024,1,1,"农历 冬月二十"},{2024,2,9,"农历 腊月三十"},{2024,2,10,"农历 正月初一"},
  {2024,2,29,"农历 正月二十"},{2025,1,29,"农历 正月初一"},
  {2025,7,24,"农历 六月三十"},{2025,7,25,"农历 闰六月初一"},
  {2025,8,22,"农历 闰六月廿九"},{2025,8,23,"农历 七月初一"},
  {2026,2,17,"农历 正月初一"},{2026,11,20,"农历 十月十二"},
  {2033,12,22,"农历 闰冬月初一"},{2050,12,31,"农历 冬月十八"}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  assert(lunar_date_format(cases[i].y,cases[i].m,cases[i].d,out,sizeof(out)));
  assert(!strcmp(out,cases[i].text));
 }
 assert(!lunar_date_format(2023,12,31,out,sizeof(out)) && !out[0]);
 assert(!lunar_date_format(2051,1,1,out,sizeof(out)));
 assert(!lunar_date_format(2025,2,29,out,sizeof(out)));
 assert(!lunar_date_format(2026,4,31,out,sizeof(out)));
 assert(!lunar_date_format(2026,13,1,out,sizeof(out)));
 assert(!lunar_date_format(2026,1,0,out,sizeof(out)));
 assert(!lunar_date_format(2026,1,1,out,5) && !out[0]);
 assert(!lunar_date_format(2026,1,1,NULL,0));
 return 0;
}
""")

    def test_dial_loading_orbit_and_pause(self):
        source = r"""#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
typedef struct {int x,y,w,h,opa;} lv_obj_t;
typedef int lv_timer_t;
#define LV_UNUSED(x) (void)(x)
static lv_obj_t dots[3][3],halo;
static lv_obj_t *s_time_sparks[3][3],*s_time_halo=&halo,*s_time_loading=&halo;
static bool s_screen_off,s_sleep_mode,s_center_open,s_home_snap_showing,s_nursery_open,busy,anim,visible=true;
static int s_last_h=-1,calls;
static bool pages_busy_except_center(void){return busy;}
static bool page_anim_busy(void){return anim;}
static bool lv_obj_is_visible(lv_obj_t *o){return visible;}
static uint32_t lv_tick_get(void){return 900;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h){o->w=w;o->h=h;calls++;}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){o->x=x;o->y=y;}
static void lv_obj_set_style_bg_opa(lv_obj_t *o,int v,int p){o->opa=v;}
"""
        source += function('ui.c', 'static void dial_loading_frame(uint32_t tick)')
        source += function('ui.c', 'static void dial_loading_cb(lv_timer_t *timer)')
        source += r"""
int main(void){
 for(int i=0;i<3;i++)for(int j=0;j<3;j++)s_time_sparks[i][j]=&dots[i][j];
 dial_loading_frame(0);int x=dots[0][0].x,y=dots[0][0].y;
 dial_loading_frame(900);assert(x!=dots[0][0].x && y!=dots[0][0].y);
 dial_loading_frame(3600);assert(x==dots[0][0].x && y==dots[0][0].y);
 for(int t=0;t<3600;t+=50){dial_loading_frame(t);
  assert(halo.opa>=12 && halo.opa<=32);
  for(int i=0;i<3;i++)for(int j=0;j<3;j++){
   assert(dots[i][j].x>=10 && dots[i][j].x+dots[i][j].w<=134);
   assert(dots[i][j].y>=15 && dots[i][j].y+dots[i][j].h<=85);
  }
 }
 bool *pause[]={&s_screen_off,&s_sleep_mode,&s_center_open,&s_home_snap_showing,&s_nursery_open,&busy,&anim};
 for(int i=0;i<7;i++){*pause[i]=true;calls=0;dial_loading_cb(0);assert(!calls);*pause[i]=false;}
 s_last_h=12;calls=0;dial_loading_cb(0);assert(!calls);s_last_h=-1;
 visible=false;dial_loading_cb(0);assert(!calls);visible=true;
 dial_loading_cb(0);assert(calls==9);
 return 0;
}
"""
        run_c(source)

    def test_round_keyboard_shape_and_hit_region(self):
        ui = (MAIN / 'ui.c').read_text()
        shapes = ui[ui.index('static const lv_point_precise_t wifi_key_corner'):ui.index('static void wifi_pwd_shape_cb')]
        run_c(r"""#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef struct {int32_t x,y;} lv_point_precise_t;
""" + shapes + r"""
int main(void){
 lv_point_precise_t p[18], q[18];
 for(unsigned id=0;id<14;id++){
  unsigned n=wifi_pwd_key_shape(id,p);
  if(id>=9 || id%3==1){assert(n==0);continue;}
  assert(n==18);
  assert(wifi_pwd_shape_contains(p,n,74,36));
  assert(!wifi_pwd_shape_contains(p,n,-1,36));
  assert(!wifi_pwd_shape_contains(p,n,148,36));
  int ox=id%3==0?8:311, oy=116+(id/3)*81;
  for(unsigned i=0;i<n;i++){
   int x=ox+p[i].x-233,y=oy+p[i].y-233;
   assert(x*x+y*y<227*227);
  }
 }
 wifi_pwd_key_shape(0,p);wifi_pwd_key_shape(8,q);
 for(int i=0;i<18;i++){assert(p[i].x+q[i].x==147);assert(p[i].y+q[i].y==72);}
 assert(!wifi_pwd_shape_contains(p,18,0,0));
 assert(wifi_pwd_shape_contains(p,18,100,30));
 wifi_pwd_key_shape(3,p);wifi_pwd_key_shape(5,q);
 for(int i=0;i<18;i++){assert(p[i].x+q[i].x==147);assert(p[i].y==q[i].y);}
 return 0;
}
""")

    def test_nursery_decoder_advances_partial_input_and_rejects_stalls(self):
        play = function('nursery.c', 'static int play_song(int index)')
        loop = play[play.index('        while (raw.len > 0'):play.index('\n        if (eos) {')]
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define SONG_CONTINUE 0
#define EPW_NURSERY_PAUSED 2
#define NURSERY_PCM_CAP 16
#define ESP_AUDIO_ERR_OK 0
#define ESP_AUDIO_ERR_BUFF_NOT_ENOUGH 1
typedef int esp_audio_err_t;
typedef struct {uint8_t *buffer;unsigned len,decoded_size,needed_size;}esp_audio_simple_dec_out_t;
typedef struct {uint8_t *buffer;unsigned len,consumed;}raw_t;
static int s_state,s_dec,mode,calls,outputs;
static uint8_t s_pcm_out[16],input[6]={1,2,3,4,5,6};
static int pump_commands(bool wait){return 0;}
static void status_set(const char *s){}
static int esp_audio_simple_dec_process(int d,raw_t *r,esp_audio_simple_dec_out_t *o){
 assert(++calls<10);
 if(mode==1){r->consumed=0;return 0;}
 if(mode==2){r->consumed=r->len+1;return 0;}
 if(r->len){assert(r->buffer==input+outputs*2);assert(r->len==6-outputs*2);
  r->consumed=2;o->decoded_size=4;outputs++;}
 return 0;
}
static int esp_audio_simple_dec_get_info(int d,int *info){return 0;}
static bool resample_chunk(uint8_t *p,int n,int *info,int *rs){return true;}
static bool decode(bool eos){
 raw_t raw={input,6,0};bool failed=false,resampler_ready=false;
 int idle_rounds=0,ev=0,result=0,info=0,rs=0;
'''
        source += loop
        source += r'''
out:return !failed;
}
int main(void){
 assert(decode(false) && calls==3 && outputs==3);
 calls=outputs=0;assert(decode(true) && calls==7 && outputs==3);
 calls=outputs=0;mode=1;assert(!decode(false) && calls==1);
 calls=outputs=0;mode=2;assert(!decode(false) && calls==1);
 return 0;
}
'''
        run_c(source)

    def test_audio_dma_failure_does_not_enable_broken_channel(self):
        # Run the actual configure patches on copies, including a second idempotence pass.
        with tempfile.TemporaryDirectory(prefix='epw-audio-patch-') as tmp:
            root = Path(tmp)
            (root / 'main').mkdir()
            paths = ['waveshare__esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c',
                     'espressif__esp_codec_dev/esp_codec_dev.c',
                     'espressif__esp_codec_dev/platform/audio_codec_data_i2s.c']
            for path in paths:
                dest = root / 'managed_components' / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                text = (ROOT / 'managed_components' / path).read_text()
                if path == paths[0]:
                    text = text.replace('.buffer_height = 24,', '.buffer_height = 50,')
                    text = text.replace('.use_psram = false,', '.use_psram = true,')
                text = text.replace('    chan_cfg.dma_frame_num = 120;\n', '')
                for call in ('data_if->set_fmt(data_if, dev->dev_caps, fs)',
                             'data_if->enable(data_if, dev->dev_caps, true)'):
                    fixed = '        int ret = ' + call + ';\n        if (ret != ESP_CODEC_DEV_OK) {\n            dev->input_opened = dev->output_opened = false;\n            return ret;\n        }'
                    text = text.replace(fixed, '        ' + call + ';')
                text = text.replace('if (ret == ESP_CODEC_DEV_OK) ret = _i2s_drv_enable(paired, true, true);',
                                    '_i2s_drv_enable(paired, true, true);')
                dest.write_text(text)
            patch = root / 'main' / 'patch.cmake'
            patch.write_text((MAIN / 'CMakeLists.txt').read_text().split('idf_component_register(')[0])
            subprocess.run(['cmake', '-P', str(patch)], check=True, capture_output=True)
            first = [(root / 'managed_components' / path).read_text() for path in paths]
            subprocess.run(['cmake', '-P', str(patch)], check=True, capture_output=True)
            self.assertEqual(first, [(root / 'managed_components' / path).read_text() for path in paths])
            self.assertEqual(first[0].count('chan_cfg.dma_frame_num = 120;'), 1)
            self.assertIn('.buffer_height = 24,', first[0])
            self.assertIn('.use_psram = false,', first[0])
            self.assertIn('.require_double_buffer = true,', first[0])
            def extract(text, signature):
                start = text.index(signature + '\n{')
                return text[start:text.index('\n}', start) + 2] + '\n'
            opening = extract(first[1], 'int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *fs)')
            setting = extract(first[2], 'static int set_fs(i2s_data_t *i2s_data, bool playback, bool skip)')
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_INVALID_ARG -1
#define ESP_CODEC_DEV_NOT_SUPPORT -2
#define ESP_CODEC_DEV_DRV_ERR -3
#define ESP_CODEC_DEV_TYPE_IN 1
#define ESP_CODEC_DEV_TYPE_OUT 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define VOL_TRANSITION_TIME 0
typedef int esp_codec_dev_type_t;
typedef struct {int rate;} esp_codec_dev_sample_info_t;
typedef struct data_if {
 int (*set_fmt)(const struct data_if *,int,esp_codec_dev_sample_info_t *);
 int (*enable)(const struct data_if *,int,bool);
} audio_codec_data_if_t;
typedef struct codec_if {
 int (*set_fs)(const struct codec_if *,esp_codec_dev_sample_info_t *);
 int (*enable)(const struct codec_if *,bool);
 int (*set_vol)(const struct codec_if *,int);
} audio_codec_if_t;
typedef struct volume {void (*open)(struct volume *,esp_codec_dev_sample_info_t *,int);} volume_t;
typedef struct {int dev_caps;bool input_opened,output_opened,sw_vol_alloced;
 const audio_codec_if_t *codec_if;const audio_codec_data_if_t *data_if;volume_t *sw_vol;} codec_dev_t;
typedef codec_dev_t *esp_codec_dev_handle_t;
static int fmt_error,enable_error,enables,driver_error,driver_enables;
static bool _verify_drv_ready(codec_dev_t *d,bool out){return true;}
static volume_t *audio_codec_new_sw_vol(void){return NULL;}
static void _update_codec_setting(codec_dev_t *d){}
static int fmt(const audio_codec_data_if_t *d,int caps,esp_codec_dev_sample_info_t *f){return fmt_error;}
static int enable(const audio_codec_data_if_t *d,int caps,bool on){enables++;return enable_error;}
typedef void *i2s_chan_handle_t;typedef int i2s_chan_info_t;
typedef struct {void *in_handle,*out_handle;esp_codec_dev_sample_info_t in_fs,out_fs;int clk_src;bool out_enable;} i2s_data_t;
static i2s_data_t paired={.out_handle=(void *)1};
static uint8_t get_bits(i2s_data_t *d,bool p){return 16;}
static void i2s_channel_get_info(void *h,i2s_chan_info_t *i){}
static int set_drv_fs(void *h,bool p,int b,int c,esp_codec_dev_sample_info_t *f){return driver_error;}
static i2s_data_t *get_paired(i2s_data_t *d,bool p){return &paired;}
static int _i2s_drv_enable(i2s_data_t *d,bool p,bool on){if(on)driver_enables++;return 0;}
'''
        source += opening + setting
        source += r'''
int main(void){
 audio_codec_data_if_t data={fmt,enable};
 codec_dev_t dev={.dev_caps=ESP_CODEC_DEV_TYPE_IN,.data_if=&data};esp_codec_dev_sample_info_t fs={0};
 fmt_error=257;assert(esp_codec_dev_open(&dev,&fs)==257 && !enables && !dev.input_opened);
 fmt_error=0;enable_error=-9;assert(esp_codec_dev_open(&dev,&fs)==-9 && enables==1 && !dev.input_opened);
 enable_error=0;assert(esp_codec_dev_open(&dev,&fs)==0 && enables==2 && dev.input_opened);
 assert(esp_codec_dev_open(&dev,&fs)==0 && enables==2);
 i2s_data_t i2s={.in_handle=(void *)1};driver_error=257;
 assert(set_fs(&i2s,false,false)==257 && driver_enables==0);
 driver_error=0;assert(set_fs(&i2s,false,false)==0 && driver_enables==1);
 return 0;
}
'''
        run_c(source)

    def test_control_center_stays_until_tool_covers_screen(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int lv_event_t;typedef struct {void *var;} lv_anim_t;
#define LV_UNUSED(x) ((void)(x))
typedef struct {bool hidden;int y;}lv_obj_t;
#define LV_OBJ_FLAG_HIDDEN 1
#define SCREEN_H 466
#define SLIDER_BRIGHT 0
#define SLIDER_VOLUME 1
static lv_obj_t center,snap,*s_center=&center,*s_center_snap_img=&snap;
static bool s_center_open,s_center_dragging,s_center_drag_engaged,ignored;
static int s_center_reveal,target;
static bool tap_ignore(lv_event_t *e){return ignored;}
static void home_snap_end(void){}
static void tool_snap_prepare(void *page){}
static void center_anim_stop(void){}
static void lv_obj_add_flag(lv_obj_t *o,int flag){o->hidden=true;}
static void lv_obj_set_y(lv_obj_t *o,int y){o->y=y;}
static void status_open(void){assert(s_center_open && !center.hidden);target=1;}
static void slider_open(int kind){assert(s_center_open && !center.hidden);target=kind==SLIDER_BRIGHT?2:3;}
static void torch_open(void){assert(s_center_open && !center.hidden);target=4;}
'''
        for signature in ('static void center_consume(void)', 'static void tool_open_done(lv_anim_t *anim)'):
            source += function('ui.c', signature)
        for name in ('center_status_cb', 'center_battery_cb', 'center_bright_cb',
                     'center_volume_cb', 'center_torch_cb'):
            source += function('ui.c', 'static void ' + name + '(lv_event_t *e)')
        source += r'''
int main(void){
 void (*callbacks[])(lv_event_t *)={center_status_cb,center_battery_cb,center_bright_cb,center_volume_cb,center_torch_cb};
 int expected[]={1,1,2,3,4};
 for(int i=0;i<5;i++){
  s_center_open=s_center_dragging=s_center_drag_engaged=true;
  center.hidden=snap.hidden=false;center.y=0;s_center_reveal=466;target=0;
  ignored=true;callbacks[i](NULL);assert(s_center_open && !target);
  ignored=false;callbacks[i](NULL);assert(target==expected[i]);
  if(i<4){assert(s_center_open && !center.hidden);lv_anim_t a={0};tool_open_done(&a);}
  assert(!s_center_open && !s_center_dragging && !s_center_drag_engaged);
  assert(center.hidden && snap.hidden && center.y==-466 && s_center_reveal==0);
 }
 return 0;
}
'''
        run_c(source)

    def test_tool_return_tracks_touch_and_settles_from_current_position(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define SCREEN_W 466
#define CENTER_DRAG_TH 8
#define LV_ABS(x) ((x)<0?-(x):(x))
#define LV_CLAMP(lo,x,hi) ((x)<(lo)?(lo):(x)>(hi)?(hi):(x))
enum {LV_EVENT_PRESSED=1,LV_EVENT_PRESSING,LV_EVENT_RELEASED,LV_EVENT_PRESS_LOST};
typedef int lv_event_code_t;typedef int lv_indev_t;
typedef struct {int x;}lv_obj_t;
typedef struct {int x,y;}lv_point_t;
typedef struct {int code;lv_obj_t *page;}lv_event_t;
typedef struct animation {lv_obj_t *var;int from,to;void (*done)(struct animation*);}lv_anim_t;
static lv_obj_t slider,status,*s_slider=&slider,*s_status=&status,*s_tool_drag_page;
static bool s_slider_open=true,s_status_open=true,s_slider_dragging,s_tool_dragging,s_tool_drag_engaged,busy;
static lv_point_t s_tool_press,point;
static int animations,saves,from,to,s_tool_x;static lv_anim_t pending;static lv_indev_t input;
static void tool_snap_prepare(lv_obj_t *p){}
static void tool_drawer_begin(lv_obj_t *p){s_tool_x=0;}
static void tool_drawer_x_cb(void *p,int x){s_tool_x=x;((lv_obj_t*)p)->x=x;}
static void tool_drawer_end(lv_obj_t *p,bool closed){p->x=s_tool_x;}
static lv_event_code_t lv_event_get_code(lv_event_t *e){return e->code;}
static lv_obj_t *lv_event_get_current_target(lv_event_t *e){return e->page;}
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &input;}
static bool page_anim_busy(void){return busy;}
static void page_anim_lock(int t){busy=true;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){*p=point;}
static int lv_obj_get_x(lv_obj_t *o){return o->x;}
static void lv_obj_set_x(lv_obj_t *o,int x){o->x=x;}
static void slider_close_now(void){s_slider_open=false;saves++;slider.x=0;}
static void status_close_now(void){s_status_open=false;status.x=0;}
static void anim_x_cb(void *obj,int value){}
static int lv_anim_path_ease_out;
static void lv_anim_delete(void *o,void (*cb)(void *,int)){}
static void lv_anim_init(lv_anim_t *a){*a=(lv_anim_t){0};}
static void lv_anim_set_var(lv_anim_t *a,lv_obj_t *o){a->var=o;}
static void *lv_anim_get_var(lv_anim_t *a){return a->var;}
static void lv_anim_set_values(lv_anim_t *a,int f,int t){a->from=f;a->to=t;}
static void lv_anim_set_duration(lv_anim_t *a,int t){}
static void lv_anim_set_path_cb(lv_anim_t *a,int p){}
static void lv_anim_set_exec_cb(lv_anim_t *a,void (*cb)(void *,int)){}
static void lv_anim_set_completed_cb(lv_anim_t *a,void (*cb)(lv_anim_t *)){a->done=cb;}
static void lv_anim_start(lv_anim_t *a){pending=*a;from=a->from;to=a->to;animations++;}
'''
        for signature in ('static void tool_back_done(lv_anim_t *anim)',
                          'static void tool_page_drag_cb(lv_event_t *e)'):
            source += function('ui.c', signature)
        source += r'''
static void event(lv_event_t *e,int code,int x,int y){e->code=code;point=(lv_point_t){x,y};tool_page_drag_cb(e);}
static void finish(void){tool_drawer_x_cb(pending.var,pending.to);pending.done(&pending);busy=false;}
int main(void){
 lv_event_t e={.page=&slider};
 event(&e,LV_EVENT_PRESSED,200,200);event(&e,LV_EVENT_PRESSING,250,200);assert(slider.x==50 && animations==0);
 event(&e,LV_EVENT_RELEASED,250,200);assert(from==50 && to==0 && s_slider_open);finish();assert(s_slider_open && saves==0 && slider.x==0);
 event(&e,LV_EVENT_PRESSED,200,200);event(&e,LV_EVENT_PRESSING,50,200);assert(slider.x==-150);
 event(&e,LV_EVENT_RELEASED,50,200);assert(from==-150 && to==-466);finish();assert(!s_slider_open && saves==1);
 s_slider_open=true;event(&e,LV_EVENT_PRESSED,200,200);event(&e,LV_EVENT_PRESSING,400,200);
 event(&e,LV_EVENT_PRESS_LOST,400,200);assert(from==200 && to==0);finish();assert(s_slider_open);
 int count=animations;event(&e,LV_EVENT_PRESSED,200,200);event(&e,LV_EVENT_PRESSING,202,280);event(&e,LV_EVENT_PRESSING,400,280);event(&e,LV_EVENT_RELEASED,400,280);assert(slider.x==0 && animations==count);
 s_slider_dragging=true;event(&e,LV_EVENT_PRESSED,200,200);event(&e,LV_EVENT_PRESSING,400,200);event(&e,LV_EVENT_RELEASED,400,200);assert(slider.x==0 && animations==count);s_slider_dragging=false;
 e.page=&status;event(&e,LV_EVENT_PRESSED,100,200);event(&e,LV_EVENT_PRESSING,260,200);assert(status.x==160);
 event(&e,LV_EVENT_RELEASED,260,200);assert(from==160 && to==466);finish();assert(!s_status_open);
 return 0;
}
'''
        run_c(source)

    def test_tool_drawer_moves_bitmap_and_restores_live_page(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_OBJ_FLAG_SCROLLABLE 4
#define LV_COLOR_FORMAT_RGB565 16
#define LV_RESULT_OK 0
#define ESP_LOGW(...) ((void)0)
typedef struct {int x,y;bool hidden;}lv_obj_t;
typedef int lv_draw_buf_t;
static lv_obj_t root,page,image,*s_screen=&root,*s_tool_snap_img;
static lv_draw_buf_t buffer,*s_tool_snap;
static bool s_tool_snap_valid,s_tool_snap_showing,s_apps_open,s_nursery_open,allocation_fails,render_fails;
static int s_tool_snap_ext,s_tool_x,allocations,renders,bg_begin,bg_end;
static lv_obj_t *lv_image_create(lv_obj_t *p){return &image;}
static void lv_obj_remove_flag(lv_obj_t *p,int flags){if(flags&1)p->hidden=false;}
static void lv_obj_add_flag(lv_obj_t *p,int flags){if(flags&1)p->hidden=true;}
static lv_draw_buf_t *lv_snapshot_create_draw_buf(lv_obj_t *p,int fmt){allocations++;return allocation_fails?NULL:&buffer;}
static int lv_snapshot_take_to_draw_buf(lv_obj_t *p,int fmt,lv_draw_buf_t *b){renders++;return render_fails?-1:0;}
static int lv_obj_get_ext_draw_size(lv_obj_t *p){return 2;}
static void lv_image_cache_drop(lv_draw_buf_t *b){}
static void lv_image_set_src(lv_obj_t *p,lv_draw_buf_t *b){}
static void home_snap_refresh(void){}
static void home_snap_begin(void){bg_begin++;}
static void home_snap_end(void){bg_end++;}
static void lv_obj_set_pos(lv_obj_t *p,int x,int y){p->x=x;p->y=y;}
static void lv_obj_set_x(lv_obj_t *p,int x){p->x=x;}
static void lv_obj_move_foreground(lv_obj_t *p){}
static void lv_anim_delete(void *p,void (*cb)(void*,int32_t)){}
'''
        for signature in ('static void tool_snap_prepare(lv_obj_t *page)',
                          'static void tool_drawer_x_cb(void *page, int32_t value)',
                          'static void tool_drawer_begin(lv_obj_t *page)',
                          'static void tool_drawer_end(lv_obj_t *page, bool closed)'):
            source += function('ui.c', signature)
        source += r'''
int main(void){
 tool_snap_prepare(&page);assert(s_tool_snap_valid && allocations==1 && renders==1);
 tool_drawer_begin(&page);assert(page.hidden && !image.hidden && s_tool_snap_showing && bg_begin==1);
 tool_drawer_x_cb(&page,-150);assert(s_tool_x==-150 && image.x==-152 && image.y==-2 && page.x==0);
 tool_drawer_x_cb(&page,-30);assert(image.x==-32 && page.x==0);
 tool_drawer_x_cb(&page,0);tool_drawer_end(&page,false);
 assert(!page.hidden && image.hidden && !s_tool_snap_showing && page.x==0 && bg_end==1);
 tool_snap_prepare(&page);assert(allocations==1 && renders==2);
 tool_drawer_begin(&page);tool_drawer_x_cb(&page,466);tool_drawer_end(&page,true);
 assert(page.hidden && image.hidden && !s_tool_snap_showing && bg_end==2);
 page.hidden=false;page.x=0;render_fails=true;tool_snap_prepare(&page);assert(!s_tool_snap_valid);
 tool_drawer_begin(&page);tool_drawer_x_cb(&page,50);assert(!page.hidden && page.x==50 && !s_tool_snap_showing);
 tool_drawer_x_cb(&page,0);tool_drawer_end(&page,false);assert(!page.hidden && image.hidden);
 s_tool_snap=NULL;allocation_fails=true;tool_snap_prepare(&page);assert(!s_tool_snap_valid);
 return 0;
}
'''
        run_c(source)

    def test_scale_cache_preserves_pixels_padding_and_falls_back_when_full(self):
        source = r"""#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define APP_COUNT 2
#define APP_ICON_D 8
#define APP_ICON_MIN_D 2
#define APP_SCALE_CACHE_BYTES cache_bytes
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_COLOR_FORMAT_RGB565 0
#define LV_STRIDE_AUTO 0
#define ESP_LOGI(...) ((void)0)
typedef struct {struct {int stride;}header;uint8_t data[128];int id;}lv_draw_buf_t;
typedef struct {uint32_t offset[APP_COUNT][APP_ICON_D+1];uint16_t runs[];}app_scale_cache_t;
static app_scale_cache_t *s_app_scale_cache;
static lv_draw_buf_t masters[2],scaled[2],*s_app_art[2]={masters,masters+1},*s_app_scaled[2]={scaled,scaled+1};
static int s_app_pixel_size[2],cache_bytes=4096,allocation_fails,patterns,reshapes;
static void *heap_caps_calloc(int n,int bytes,int caps){assert(caps==3);return allocation_fails?NULL:calloc(n,bytes);}
typedef struct {int x,y;}lv_point_t;
#define CENTER_X 233
#define CENTER_Y 233
static int app_icon_size(lv_point_t center){return APP_ICON_D;}
static void vTaskDelay(int ticks){assert(ticks==1);}
static bool lv_draw_buf_reshape(lv_draw_buf_t *d,int fmt,int w,int h,int stride){reshapes++;d->header.stride=(w*2+3)&~3;return true;}
static void app_resize_rgb565(const lv_draw_buf_t *src,lv_draw_buf_t *dst,int size){
 for(int y=0;y<size;y++)for(int x=0;x<size;x++){
  uint16_t color=patterns?src->id*1000+y*size+x:src->id+10+y/2;
  memcpy(dst->data+y*dst->header.stride+x*2,&color,2);
 }
}
"""
        source += function('ui.c', 'static bool app_scale_cache_restore(int i, lv_draw_buf_t *dst, int size)')
        source += function('ui.c', 'static void app_scale_cache_build(void)')
        source += r"""
int main(void){
 masters[0].id=1;masters[1].id=2;
 for(patterns=0;patterns<2;patterns++)for(int small=0;small<2;small++){
  cache_bytes=small?sizeof(app_scale_cache_t)+40:4096;
  app_scale_cache_build();assert(s_app_scale_cache);int hits=0,misses=0;
  for(int size=2;size<=APP_ICON_D;size++)for(int i=0;i<APP_COUNT;i++){
   lv_draw_buf_t actual={0},expected={0};actual.header.stride=expected.header.stride=(size*2+3)&~3;
   memset(actual.data,0xcc,sizeof(actual.data));memset(expected.data,0xcc,sizeof(expected.data));
   bool hit=app_scale_cache_restore(i,&actual,size);if(hit)hits++;else misses++;
   if(!hit)app_resize_rgb565(s_app_art[i],&actual,size);
   app_resize_rgb565(s_app_art[i],&expected,size);
   assert(memcmp(actual.data,expected.data,sizeof(actual.data))==0);
  }
  assert(hits>0 && (!small?misses==0:misses>0));
  assert(s_app_pixel_size[0]==0 && s_app_pixel_size[1]==0);
  free(s_app_scale_cache);s_app_scale_cache=NULL;
 }
 allocation_fails=1;app_scale_cache_build();assert(!s_app_scale_cache);
 assert(!app_scale_cache_restore(0,&scaled[0],2));return 0;
}
"""
        run_c(source)

    def test_display_te_waits_once_per_frame_and_falls_back_without_signal(self):
        source = r"""#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define IRAM_ATTR
#define LV_EVENT_RENDER_START 1
#define LV_EVENT_FLUSH_START 2
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portYIELD_FROM_ISR() ((void)0)
typedef int BaseType_t;
typedef struct {int code;}lv_event_t;
static bool s_enabled=true,s_first_flush,s_logged,fresh=true;
static unsigned s_misses,stale,waits,clears,isr_gives,delays;
static atomic_uint s_last_te_us,s_period_us;
static int s_te=1;
static void vTaskDelay(unsigned ticks){assert(ticks==4);delays++;}
static uint32_t now;
static uint32_t esp_timer_get_time(void){return now;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static int xSemaphoreTake(int sem,int timeout){
 assert(sem==1);if(!timeout){clears++;if(stale){stale--;return 1;}return 0;}
 assert(timeout==35);assert(!stale);waits++;return fresh;
}
static void xSemaphoreGiveFromISR(int sem,int *wake){assert(sem==1);isr_gives++;*wake=0;}
"""
        source += function('display_sync.c', 'static void IRAM_ATTR display_te_isr(void *arg)')
        source += function('display_sync.c', 'static void display_sync_event(lv_event_t *event)')
        source += r"""
static void event(int code){lv_event_t e={code};display_sync_event(&e);}
int main(void){
 now=10000;display_te_isr(0);now+=16667;display_te_isr(0);
 assert(isr_gives==2 && s_period_us==16667);
 event(2);assert(waits==0);
 for(int frame=0;frame<10;frame++){
  event(1);assert(s_first_flush);stale=2;event(2);
  assert(!s_first_flush && waits==(unsigned)frame+1 && s_misses==0);
  for(int chunk=0;chunk<30;chunk++)event(2);
  assert(waits==(unsigned)frame+1);
 }
 assert(s_logged && clears==30 && delays==10);
 s_period_us=9999;event(1);event(2);assert(delays==10);
 s_period_us=25001;event(1);event(2);assert(delays==10);
 s_period_us=16667;
 fresh=false;event(1);event(2);assert(s_enabled && s_misses==1);
 fresh=true;event(1);event(2);assert(s_enabled && s_misses==0);
 fresh=false;
 for(int frame=0;frame<3;frame++){event(1);event(2);}
 assert(!s_enabled && s_misses==3);unsigned before=waits;
 event(1);event(2);assert(waits==before);
 now=UINT32_MAX-10;s_last_te_us=now;now=15;display_te_isr(0);assert(s_period_us==26);
 return 0;
}
"""
        run_c(source)

    def test_normal_boot_has_no_icon_benchmark(self):
        source = (MAIN / "main.c").read_text()
        self.assertNotIn("icon_bench", source)
        self.assertNotIn("ui_debug_icon_", (MAIN / "ui.c").read_text())
        self.assertNotIn("DRAG BENCH", (MAIN / "ui.c").read_text())
        self.assertNotIn("LINE QA", (MAIN / "ui.c").read_text())
        self.assertNotIn("SYNC PERF", (MAIN / "ui.c").read_text())
        self.assertNotIn("ROUND QA", (MAIN / "ui.c").read_text())
        self.assertNotIn("ui_round_qa_start", source)
        self.assertNotIn("ui_drag_bench_start", source)

    def test_three_color_ring_has_no_gray_underlay_or_highlight(self):
        ui = (MAIN / "ui.c").read_text()
        body = ui.split("static void mk_ring_seg(", 1)[1].split("\n}", 1)[0]
        # 黑底上半透明的加宽同色弧/白色高光都会露出灰色轮廓，只允许一条实色弧。
        self.assertEqual(body.count("mk_arc_seg("), 1, body)
        self.assertIn("RING_WIDTH,", body)
        self.assertNotIn("RING_WIDTH + 10", body)
        self.assertNotIn("0xFFFFFF", body)
        self.assertNotIn("LV_OPA_20", body)


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

    def test_button_page_changes_are_deferred_to_lvgl(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define LV_RESULT_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int lv_result_t;
static int busy,queue_error,locks,pet,apps;
static bool cancelled,woke;
static void (*callback)(void *);
static void *argument;
static int bsp_display_lock(int timeout){if(!busy)locks++;return busy;}
static void bsp_display_unlock(void){locks--;}
static int lv_async_call(void (*cb)(void *),void *arg){if(queue_error)return -1;callback=cb;argument=arg;return 0;}
static bool ui_power_menu_cancel(void){return cancelled;}
static bool ui_activity(void){return woke;}
static void ui_demo_toggle_pet(void){assert(locks==0);pet++;}
static void ui_demo_toggle_apps(void){assert(locks==0);apps++;}
'''
        source += function('button.c', 'static void toggle_page_cb(void *arg)')
        source += function('button.c', 'static void toggle_page(const char *source)')
        source += r'''
int main(void){
    toggle_page("BOOT");assert(pet==0 && apps==0 && locks==0 && callback);
    callback(argument);assert(pet==1 && apps==0);
    toggle_page("PWR");callback(argument);assert(apps==1);
    woke=true;callback(argument);assert(apps==1);
    woke=false;cancelled=true;callback(argument);assert(apps==1);
    callback=NULL;busy=-1;toggle_page("BOOT");assert(!callback && locks==0);
    busy=0;queue_error=-1;toggle_page("BOOT");assert(!callback && locks==0);
}
'''
        run_c(source)

    def test_microphone_open_applies_gain_and_handles_failure(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_CODEC_DEV_OK 0
#define ESP_AUDIO_ERR_OK 0
#define VOICE_SAMPLE_RATE 16000
#define ESP_OPUS_BITRATE_AUTO 0
#define ESP_OPUS_ENC_FRAME_DURATION_60_MS 60
#define ESP_OPUS_ENC_APPLICATION_VOIP 1
typedef struct {int bits_per_sample,channel,sample_rate;}esp_codec_dev_sample_info_t;
typedef struct {int sample_rate,channel,bits_per_sample,bitrate,frame_duration,application_mode,complexity;}esp_opus_enc_config_t;
static void *s_mic,*s_enc;
static int gain_error,encodes;
static float gain;
static bool audio_init(void){return true;}
static void *bsp_audio_codec_microphone_init(void){return (void*)1;}
static int esp_codec_dev_open(void *m,esp_codec_dev_sample_info_t *fs){assert(fs->sample_rate==16000 && fs->channel==2);return 0;}
static int esp_codec_dev_set_in_gain(void *m,float db){gain=db;return gain_error;}
static int esp_opus_enc_open(esp_opus_enc_config_t *cfg,size_t n,void **enc){assert(gain==30 && cfg->frame_duration==60);encodes++;*enc=(void*)2;return 0;}
'''
        source += function('voice_client.c', 'static bool open_audio(void)')
        source += r'''
int main(void){
    assert(open_audio() && encodes==1 && gain==30);
    gain_error=-1;s_enc=NULL;assert(!open_audio() && encodes==1 && !s_enc);
}
'''
        run_c(source)

    def test_microphone_stereo_mix_preserves_mono_frame_and_headroom(self):
        source = '#include <stdint.h>\n#include <stddef.h>\n#include <assert.h>\n'
        source += function('voice_client.c', 'static void mix_microphones(uint8_t *pcm, size_t mono_bytes)')
        source += r'''
int main(void){
    int16_t pcm[]={100,300,32767,32767,-32768,-32768,0,800,-600,0};
    mix_microphones((uint8_t*)pcm,10);
    assert(pcm[0]==200 && pcm[1]==32767 && pcm[2]==-32768 && pcm[3]==400 && pcm[4]==-300);
}
'''
        run_c(source)

    def test_microphone_peak_handles_silence_and_full_scale(self):
        source = '#include <stdint.h>\n#include <stddef.h>\n#include <assert.h>\n'
        source += function('voice_client.c', 'static int pcm_peak(const uint8_t *pcm, size_t bytes)')
        source += r'''
int main(void){
    int16_t pcm[]={0,-12,100,-32768,32767};
    assert(pcm_peak((uint8_t*)pcm,0)==0);
    assert(pcm_peak((uint8_t*)pcm,2)==0);
    assert(pcm_peak((uint8_t*)pcm,6)==100);
    assert(pcm_peak((uint8_t*)pcm,sizeof(pcm))==32768);
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
static void toggle_page(const char *s){shorts++;}
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
static void lv_anim_path_overshoot(void){}
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
#define APP_REAL_COUNT 10
#define APP_COUNT APP_REAL_COUNT
#define APP_ICON_D 120
#define APP_ROOT_W APP_ICON_D
#define APP_ROOT_H 120
#define APP_ICON_MIN_D 2
#define APP_GRID_STEP 116
#define APP_GRID_DRAG_TH 8
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_EVENT_PRESSED 1
#define LV_EVENT_PRESSING 2
#define LV_EVENT_RELEASED 3
#define LV_EVENT_PRESS_LOST 4
#define LV_UNUSED(x) ((void)(x))
#define LV_CLAMP(lo,x,hi) ((x)<(lo)?(lo):(x)>(hi)?(hi):(x))
static lv_obj_t apps, *s_apps=&apps;
static lv_point_t s_app_pan,s_app_pan_start,s_app_settle_from,s_app_settle_to,s_app_press,point;
static lv_obj_t icon_objects[APP_COUNT],*s_app_icons[APP_COUNT];
static bool s_app_drag_active,s_app_drag_moved,busy;
typedef int lv_draw_buf_t;
static lv_draw_buf_t *s_app_art[APP_COUNT];
static int opened=-1,layouts,sources,anim_deletes;
#define LV_PART_MAIN 0
#define LV_STYLE_PROP_ANY 0
static bool style_enabled=true;
static unsigned style_batches;
static void lv_obj_enable_style_refresh(bool enabled){style_enabled=enabled;}
static void lv_obj_refresh_style(lv_obj_t *o,int part,int prop){assert(style_enabled);style_batches++;}
static void app_art_resize(int i,int size){}
static void lv_label_set_text(lv_obj_t *o,const char *t){}
static void lv_image_set_src(lv_obj_t *o,void *src){sources++;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h){}
static void lv_obj_add_flag(lv_obj_t *o,int f){}
static void lv_obj_remove_flag(lv_obj_t *o,int f){}

static int lv_event_get_code(lv_event_t *e){return e->code;}
static lv_indev_t input;
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &input;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){*p=point;}
static bool page_anim_busy(void){return busy;}
static void app_open_index(int i){opened=i;}
static void lv_anim_delete(void *obj,void (*cb)(void *,int32_t)){anim_deletes++;}
static void lv_anim_init(lv_anim_t *a){}
static void lv_anim_set_var(lv_anim_t *a,void *v){}
static void lv_anim_set_values(lv_anim_t *a,int x,int y){}
static void lv_anim_set_duration(lv_anim_t *a,int ms){}
static void lv_anim_path_ease_out(void){}
static void lv_anim_path_overshoot(void){}
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
        assert(s_app_pan.x==app_pan_bound(directions[i][0]*9/10,true) && s_app_pan.y==app_pan_bound(directions[i][1]*9/10,true));
        event(LV_EVENT_RELEASED,233+directions[i][0],233+directions[i][1]);
        assert(opened==-1 && !s_app_drag_active);
        assert(s_app_pan.x==app_pan_bound(directions[i][0]*9/10,false) && s_app_pan.y==app_pan_bound(directions[i][1]*9/10,false));
    }
    assert(layouts>0);
    s_app_pan=(lv_point_t){0,0};event(LV_EVENT_PRESSED,233,233);
    event(LV_EVENT_PRESSING,243,233);assert(s_app_pan.x==9);
    int unchanged=layouts;event(LV_EVENT_PRESSING,243,233);assert(layouts==unchanged);
    event(LV_EVENT_RELEASED,310,233);assert(s_app_pan.x==APP_GRID_STEP);

    for(int sign=-1;sign<=1;sign+=2){
        s_app_pan=(lv_point_t){0,0};
        event(LV_EVENT_PRESSED,233,233);
        event(LV_EVENT_PRESSING,233+sign*300,233+sign*300);
        assert(s_app_pan.x==sign*270 && s_app_pan.y==sign*270);
        event(LV_EVENT_RELEASED,233+sign*300,233+sign*300);
        assert(s_app_pan.x==sign*APP_GRID_STEP && s_app_pan.y==sign*APP_GRID_STEP);
    }
    /* 越界仍跟随、停住不偷跑；反向移动不等手指跨回原边界。 */
    s_app_pan=(lv_point_t){APP_GRID_STEP,0};
    event(LV_EVENT_PRESSED,100,233);
    event(LV_EVENT_PRESSING,250,233);assert(s_app_pan.x==APP_GRID_STEP+135);
    unchanged=layouts;event(LV_EVENT_PRESSING,250,233);
    assert(s_app_pan.x==APP_GRID_STEP+135 && layouts==unchanged);
    event(LV_EVENT_PRESSING,240,233);assert(s_app_pan.x==APP_GRID_STEP+126);
    event(LV_EVENT_RELEASED,260,233);
    assert(s_app_settle_from.x==APP_GRID_STEP+144 && s_app_settle_to.x==APP_GRID_STEP);
    /* 回弹中按住，沿当前显示坐标续拖，没有跳回停靠点。 */
    s_app_pan=(lv_point_t){180,-150};
    int before_grab=anim_deletes;
    event(LV_EVENT_PRESSED,233,233);
    assert(anim_deletes==before_grab+1);
    assert(s_app_pan.x==180 && s_app_pan.y==-150);
    event(LV_EVENT_PRESSING,253,223);assert(s_app_pan.x==198 && s_app_pan.y==-159);
    event(LV_EVENT_PRESS_LOST,300,300);
    assert(s_app_settle_from.x==198 && s_app_settle_from.y==-159);
    assert(s_app_pan.x==APP_GRID_STEP && s_app_pan.y==-APP_GRID_STEP);
    for(int x=-582;x<=582;x++)assert(app_pan_bound(x,true)==x);
    s_app_pan=(lv_point_t){0,0};
    for(int i=0;i<APP_COUNT;i++){
        opened=-1;lv_point_t p=app_icon_center(i);
        event(LV_EVENT_PRESSED,p.x,p.y);event(LV_EVENT_RELEASED,p.x,p.y);assert(opened==(i<APP_REAL_COUNT?i:-1));

    }
    opened=-1;event(LV_EVENT_PRESSED,20,20);event(LV_EVENT_RELEASED,20,20);assert(opened==-1);
    lv_point_t p=app_icon_center(0);
    event(LV_EVENT_PRESSED,p.x,p.y);event(LV_EVENT_PRESS_LOST,p.x,p.y);assert(opened==-1);
    busy=true;event(LV_EVENT_PRESSED,p.x,p.y);event(LV_EVENT_RELEASED,p.x,p.y);assert(opened==-1);
    busy=false;
    event(LV_EVENT_PRESSED,p.x+100,p.y);event(LV_EVENT_RELEASED,p.x,p.y);assert(opened==-1);
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++){
        s_app_pan=(lv_point_t){x*APP_GRID_STEP,y*APP_GRID_STEP};
        for(int i=APP_REAL_COUNT;i<APP_COUNT;i++){
            opened=-1;apps_open_at(app_icon_center(i));assert(opened==-1);
        }
    }
    assert(app_pan_bound(50,false)==0 && app_pan_bound(-50,false)==0);
    s_app_pan=(lv_point_t){0,0};assert(app_icon_size(app_icon_center(0))==104);
    assert(app_icon_size(app_icon_center(1))<APP_ICON_D);
    assert(app_icon_size(app_icon_center(3))<app_icon_size(app_icon_center(1)));
    for(int i=0;i<APP_COUNT;i++){
        lv_point_t p=app_icon_center(i);
        float dx=p.x-CENTER_X,dy=p.y-CENTER_Y;
        if(app_icon_size(p)==0)continue;
        assert(sqrtf(dx*dx+dy*dy)+app_icon_size(p)/2.0f<233);
        for(int j=0;j<i;j++){
            lv_point_t q=app_icon_center(j);if(!app_icon_size(q))continue;dx=p.x-q.x;dy=p.y-q.y;
            assert(sqrtf(dx*dx+dy*dy)>=(app_icon_size(p)+app_icon_size(q))/2.0f);
        }
    }
    for(int x=-132;x<=132;x+=11)for(int y=-132;y<=132;y+=11){
        s_app_pan=(lv_point_t){x,y};
        for(int i=0;i<APP_COUNT;i++){
            lv_point_t p=app_icon_center(i);
            float dx=p.x-CENTER_X,dy=p.y-CENTER_Y;
            int size=app_icon_size(p);
            if(!size)continue;
            assert(sqrtf(dx*dx+dy*dy)+(size+1)/2.0f+1<233);
            for(int j=0;j<i;j++){
                lv_point_t q=app_icon_center(j);if(!app_icon_size(q))continue;dx=p.x-q.x;dy=p.y-q.y;
                assert(sqrtf(dx*dx+dy*dy)+1>=(size+app_icon_size(q))/2.0f);
            }
        }
    }
    assert(app_icon_size((lv_point_t){1000,1000})==0);
    s_app_pan=(lv_point_t){0,0};
    assert(app_icon_size(app_icon_center(1))==94);
    assert(app_icon_size(app_icon_center(3))==88);
    int visible=0;
    for(int i=0;i<APP_COUNT;i++)if(app_icon_size(app_icon_center(i)))visible++;
    assert(APP_COUNT==10 && visible==10);
    int previous[APP_COUNT];
    s_app_pan=(lv_point_t){0,0};
    for(int i=0;i<APP_COUNT;i++)previous[i]=app_icon_size(app_icon_center(i));
    bool disappeared=false;
    for(int x=1;x<=116;x++){
        s_app_pan=(lv_point_t){x,x};
        for(int i=0;i<APP_COUNT;i++){
            int size=app_icon_size(app_icon_center(i));
            assert(abs(size-previous[i])<=6);previous[i]=size;
        }
        if(previous[4]==0)disappeared=true;
    }
    assert(disappeared && previous[3]==104 && previous[4]==0);
    s_app_pan=(lv_point_t){0,0};
    s_app_art[0]=(lv_draw_buf_t *)&apps;
    unsigned batches_before=style_batches;
    apps_layout_icons();assert(style_enabled && style_batches==batches_before+1);
    int before=sources;apps_layout_icons();assert(sources==before && style_enabled);
    s_app_pan=(lv_point_t){80,80};apps_layout_icons();assert(sources==before);
    int last=104;
    for(int x=0;x<400;x++){
        int size=app_icon_size((lv_point_t){CENTER_X+x,CENTER_Y});
        assert(size<=last && last-size<=4 && (size==0 || size>=APP_ICON_MIN_D));last=size;
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
#define APP_COUNT 6
typedef struct {uint32_t offset[APP_COUNT][APP_ICON_D+1];uint16_t runs[];}app_scale_cache_t;
static app_scale_cache_t *s_app_scale_cache;
static lv_draw_buf_t *s_app_art[6],*s_app_scaled[6];
static int s_app_pixel_size[6],icon,*s_app_icons[6],sources,drops;
static lv_draw_buf_t *lv_draw_buf_reshape(lv_draw_buf_t *b,int cf,int w,int h,int stride){
    if(w>120)return 0;b->header.w=w;b->header.h=h;b->header.stride=(w*2+3)&~3;return b;
}
static void lv_image_cache_drop(lv_draw_buf_t *b){drops++;}
static void lv_image_set_src(int *o,lv_draw_buf_t *b){sources++;}
'''
        for signature in ('static bool app_scale_cache_restore(int i, lv_draw_buf_t *dst, int size)',
                          'static uint16_t app_mix_rgb565(uint16_t a, uint16_t b, unsigned weight)',
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
    for(int y=0;y<120;y++)for(int x=0;x<120;x++)
        large_src[y][x]=((x/12+y/12)%3)?0x39E7:(uint16_t)(x*7919+y*101);
    for(int size=2;size<=120;size++){
        lv_draw_buf_reshape(&dst,LV_COLOR_FORMAT_RGB565,size,size,0);
        app_resize_rgb565(&src,&dst,size);
        for(int y=0;y<size;y++)for(int x=0;x<size;x++){
            unsigned px=x*119*256u/(size-1),py=y*119*256u/(size-1);
            int xx=px>>8,yy=py>>8,nx=xx+(xx<119),ny=yy+(yy<119);
            uint16_t a=app_mix_rgb565(large_src[yy][xx],large_src[yy][nx],px&255);
            uint16_t b=app_mix_rgb565(large_src[ny][xx],large_src[ny][nx],px&255);
            assert(*(uint16_t *)(large_dst+y*dst.header.stride+x*2)==app_mix_rgb565(a,b,py&255));
        }
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

    def test_timer_preset_fills_only_then_start_button_runs(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef int lv_event_t;
typedef int lv_color_t;
typedef struct {int unused;} lv_obj_t;
enum {WATCH_TIMER_IDLE,WATCH_TIMER_RUNNING,WATCH_TIMER_PAUSED,WATCH_TIMER_FINISHED};
#define COL_TEAL 1
#define COL_SURF_HI 2
#define COL_INK 3
#define COL_TEXT 4
static int state,ignored,refreshes,starts,pauses,resets,toasts;
static uint32_t selected,s_timer_total=60;
static const uint32_t s_timer_choices[]={60,180,300,600,1800,3600};
static lv_obj_t objects[13];
static lv_obj_t *s_timer_set_value=&objects[0];
static lv_obj_t *s_timer_preset_btn[6]={&objects[1],&objects[2],&objects[3],&objects[4],&objects[5],&objects[6]};
static lv_obj_t *s_timer_preset_label[6]={&objects[7],&objects[8],&objects[9],&objects[10],&objects[11],&objects[12]};
static bool tap_ignore(lv_event_t *e){return ignored;}
static void *lv_event_get_user_data(lv_event_t *e){return (void *)(uintptr_t)*e;}
static int watch_timer_state(void){return state;}
static bool watch_timer_select(uint32_t s){if(state==1 || state==2 || !s || s>86399)return false;selected=s;return true;}
static void watch_timer_start(void){starts++;state=WATCH_TIMER_RUNNING;}
static void watch_timer_pause(void){pauses++;state=WATCH_TIMER_PAUSED;}
static void watch_timer_reset(void){resets++;state=WATCH_TIMER_IDLE;}
static void tools_refresh(void){refreshes++;}
static void toast_show(const char *s){toasts++;}
static void lv_label_set_text(lv_obj_t *o,const char *t){}
static void lv_obj_set_style_bg_color(lv_obj_t *o,lv_color_t c,int p){}
static void lv_obj_set_style_text_color(lv_obj_t *o,lv_color_t c,int p){}
'''
        source += function('ui.c', 'static void timer_format(uint32_t seconds, char *out, size_t size)')
        source += function('ui.c', 'static uint32_t timer_stepped_total(uint32_t total, int unit, int delta)')
        source += function('ui.c', 'static int timer_preset_index(uint32_t seconds)')
        source += function('ui.c', 'static void timer_set_refresh(void)')
        for signature in ('static void timer_set_duration(uint32_t seconds)',
                          'static void timer_preset_cb(lv_event_t *e)',
                          'static void timer_step_cb(lv_event_t *e)',
                          'static void timer_start_cb(lv_event_t *e)',
                          'static void timer_action_cb(lv_event_t *e)',
                          'static void timer_reset_cb(lv_event_t *e)'):
            source += function('ui.c', signature)
        source += r'''
int main(void){
    int event=2;ignored=1;timer_preset_cb(&event);assert(!starts && selected==0);
    ignored=0;timer_preset_cb(&event);
    assert(selected==300 && s_timer_total==300 && starts==0);
    /* 预设只填值：再点一次同一个预设也不能开跑 */
    timer_preset_cb(&event);assert(starts==0 && s_timer_total==300);
    /* 分钟 +1 / 秒 -1 只改设定值 */
    event=(1<<8)|0;timer_step_cb(&event);assert(selected==360 && s_timer_total==360 && starts==0);
    event=(((uintptr_t)(uint8_t)-1<<8)&0xffff)|1;timer_step_cb(&event);
    assert(selected==359 && starts==0);
    /* 上下限：1 秒 / 23:59:59 */
    s_timer_total=1;event=(((uintptr_t)(uint8_t)-1<<8)&0xffff)|0;timer_step_cb(&event);
    assert(selected==1);s_timer_total=300;
    s_timer_total=86399;event=(1<<8)|1;timer_step_cb(&event);assert(selected==86399);
    /* 没选时长点开始：只提示，不启动 */
    s_timer_total=0;timer_start_cb(&event);assert(starts==0 && toasts==1);
    s_timer_total=300;timer_start_cb(&event);assert(starts==1 && state==WATCH_TIMER_RUNNING);
    /* 运行中预设和步进都锁住 */
    timer_preset_cb(&(int){0});assert(starts==1 && s_timer_total==300);
    timer_step_cb(&(int){(1<<8)|0});assert(s_timer_total==300);
    /* 暂停 / 继续 / 完成后再来一次 / 取消 */
    timer_action_cb(&event);assert(pauses==1 && state==WATCH_TIMER_PAUSED);
    timer_action_cb(&event);assert(starts==2 && state==WATCH_TIMER_RUNNING);
    state=WATCH_TIMER_FINISHED;timer_action_cb(&event);assert(starts==3);
    state=WATCH_TIMER_FINISHED;timer_reset_cb(&event);assert(resets==1 && state==WATCH_TIMER_IDLE);
    return 0;
}
'''
        run_c(source)
        tools_build = function('ui.c', 'static void build_tools(void)')
        self.assertIn('"熄屏"', tools_build)
        self.assertIn('"到点会亮屏"', tools_build)
        self.assertNotIn('"系统设置"', tools_build)
        self.assertNotIn('"到点提醒', tools_build)
        preset = function('ui.c', 'static void timer_preset_cb(lv_event_t *e)')
        self.assertNotIn('watch_timer_start', preset)

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
    assert(!watch_timer_select(0) && !watch_timer_select(86400) && !watch_timer_select(UINT32_MAX));
    uint32_t custom[]={1,59,61,3599,3601,86399};
    for(int i=0;i<6;i++){
        assert(watch_timer_select(custom[i]));watch_timer_start();
        assert(watch_timer_remaining()==custom[i]);
        now_us=s_deadline_us;assert(watch_system_tick());
        watch_timer_start();assert(watch_timer_remaining()==custom[i]);watch_timer_reset();
    }
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

    def test_audio_bsp_failure_cleanup_clears_both_handles(self):
        path = MAIN.parent / 'managed_components/waveshare__esp32_s3_touch_amoled_1_75c/esp32_s3_touch_amoled_1_75c.c'
        source = path.read_text()
        for channel in ('i2s_tx_chan', 'i2s_rx_chan'):
            self.assertIn(f'i2s_channel_disable({channel});\n        i2s_del_channel({channel});\n        {channel} = NULL;', source)
        self.assertNotIn('assert(i2s_data_if)', source)

    def test_large_fonts_remain_in_flash_for_runtime_memory(self):
        config = (MAIN.parent / 'sdkconfig.defaults').read_text()
        self.assertNotIn('CONFIG_SPIRAM_RODATA=y', config)
        self.assertNotIn('CONFIG_SPIRAM_XIP_FROM_PSRAM=y', config)
        self.assertIn('CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y', config)
        main = (MAIN / 'main.c').read_text()
        self.assertLess(main.index('audio_init()'), main.index('ui_init()'))
        self.assertLess(main.index('audio_init()'), main.index('wifi_init_sta()'))

    def test_pet_typewriter_keeps_one_line_of_latest_utf8_text(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define LV_COORD_MAX 32767
#define LV_TEXT_FLAG_EXPAND 1
static char s_pet_text[320],shown[320];
static size_t s_pet_text_end,s_pet_text_start;
static int s_pet_bubble_label,epw_cjk_28,width=84;
typedef struct {int x,y;}lv_point_t;
static void lv_label_set_text(int label,const char *text){snprintf(shown,sizeof(shown),"%s",text);}
static int lv_obj_get_content_width(int label){return width;}
static void lv_text_get_size(lv_point_t *size,const char *text,int *font,int a,int b,int max,int flags){
    size->x=0;size->y=41;
    for(size_t i=0;text[i];){
        unsigned char c=text[i];size->x+=c<128?14:28;
        i+=c<128?1:(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:4;
    }
}
typedef struct {bool is_placeholder;}lv_font_glyph_dsc_t;
static bool lv_font_get_glyph_dsc(int *font,lv_font_glyph_dsc_t *d,uint32_t cp,int next){d->is_placeholder=false;return cp<0x10000;}
'''
        for signature in ('static const char *pet_safe_text(const char *in)',
                          'static void pet_text_set(const char *text)',
                          'static void pet_text_step(void)', 'void ui_pet_reveal_text(void)'):
            source += function('ui.c', signature)
        source += r'''
int main(void){
    pet_text_set("你好世界");pet_text_step();assert(!strcmp(shown,"你"));
    pet_text_step();assert(!strcmp(shown,"你好"));
    size_t end=s_pet_text_end;pet_text_set("你好世界");assert(s_pet_text_end==end);
    pet_text_step();assert(!strcmp(shown,"你好世"));
    pet_text_step();assert(!strcmp(shown,"好世界") && !strcmp(s_pet_text,"你好世界"));
    pet_text_step();assert(!strcmp(shown,"好世界"));
    pet_text_set("你好世界！");assert(s_pet_text_end==12);pet_text_step();assert(!strcmp(shown,"世界！"));
    pet_text_set("Hi猫咪ABC");assert(shown[0]==0);
    for(int i=0;i<7;i++)pet_text_step();assert(!strcmp(shown,"咪ABC"));
    pet_text_set("换一句");pet_text_step();assert(!strcmp(shown,"换"));
    pet_text_set("甲乙丙丁戊");for(int i=0;i<5;i++)pet_text_step();
    pet_text_set("乙丙丁戊己");assert(s_pet_text_end==12);pet_text_step();assert(!strcmp(shown,"丁戊己"));

    assert(!strcmp(pet_safe_text("甲\n乙\r\nA\tB😀"),"甲 乙  A B"));
    char bad[]={ (char)0xE4,0 };assert(pet_safe_text(bad)[0]==0);
    assert(!strcmp(pet_safe_text("中文"),"中文"));
}
'''
        run_c(source)
        ui = (MAIN / 'ui.c').read_text()
        self.assertIn('lv_label_set_long_mode(s_pet_bubble_label, LV_LABEL_LONG_CLIP);', ui)
        self.assertIn('text = s_pet_text;', ui)
        self.assertIn('&epw_cjk_28, COL_TEXT,', ui)
        self.assertIn('lv_obj_set_height(s_pet_bubble_label, 44)', ui)

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

    def test_voice_ui_mailbox_keeps_latest_utf8_and_rejects_old_session(self):
        original = (MAIN / 'voice_client.c').read_text()
        start = original.index('typedef struct {\n    unsigned epoch;')
        types = original[start:original.index('static QueueHandle_t s_ui_queue;', start)]
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define pdTRUE 1
''' + types + r'''
static void *s_ui_queue=(void*)1;
static atomic_uint s_epoch=1;
static unsigned s_session_epoch=1;
static atomic_bool s_requested=true;
static voice_ui_update_t pending;
static bool queued;
static int updates,reveals;
static void ui_pet_reveal_text(void){reveals++;}
static char shown[320],mood[32];
static bool session_current(void){return s_requested && s_epoch==s_session_epoch;}
static int xQueueOverwrite(void *q,const voice_ui_update_t *u){pending=*u;queued=true;return 1;}
static int xQueueReceive(void *q,voice_ui_update_t *u,int wait){if(!queued)return 0;*u=pending;queued=false;return 1;}
static int xQueuePeek(void *q,voice_ui_update_t *u,int wait){if(!queued)return 0;*u=pending;return 1;}
static void ui_activity(void){}
static void ui_pet_set_state_by_name(const char *e,const char *t){updates++;snprintf(mood,sizeof(mood),"%s",e);if(t)snprintf(shown,sizeof(shown),"%s",t);}
'''
        source += function('voice_client.c', 'static void pet_post(const char *emotion, const char *text, bool spoken)')
        source += function('voice_client.c', 'static void pet_set(const char *emotion, const char *text)')
        source += function('voice_client.c', 'void voice_client_update_ui(void)')
        source += r'''
int main(void){
 pet_set("thinking","旧文字");pet_set("speaking","最新文字");assert(updates==0);
 pet_set("listening",NULL);voice_client_update_ui();assert(updates==1 && !strcmp(shown,"最新文字") && !strcmp(mood,"listening"));
 char long_text[610];for(int i=0;i<200;i++)memcpy(long_text+i*3,"猫",3);strcpy(long_text+600,"结束");
 pet_set("speaking",long_text);voice_client_update_ui();assert(strlen(shown)<=319 && strlen(shown)%3==0 && !strcmp(shown+strlen(shown)-6,"结束"));
 pet_post("speaking","已说的词",true);pet_set("listening",NULL);voice_client_update_ui();assert(reveals==1 && !strcmp(shown,"已说的词"));
 pet_set("sad","错误提示");s_requested=false;s_epoch=2;voice_client_update_ui();assert(!strcmp(shown,"错误提示"));
 s_requested=true;s_session_epoch=2;pet_set("thinking","上一轮");s_epoch=3;s_session_epoch=3;
 int before=updates;voice_client_update_ui();assert(updates==before);
 s_ui_queue=NULL;pet_set("sad","忽略");voice_client_update_ui();assert(updates==before);
}
'''
        run_c(source)

    def test_spoken_captions_wait_for_pcm_and_clear_on_interrupt(self):
        original = (MAIN / 'voice_client.c').read_text()
        start = original.index('typedef struct {\n    uint32_t at_samples;')
        types = original[start:original.index('static QueueHandle_t s_caption_queue;', start)]
        source = r"""#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define VOICE_SAMPLE_RATE 16000
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdTRUE 1
""" + types + r"""
static void *s_caption_queue=(void*)1;
static uint32_t s_tts_written_samples,s_caption_received_ms;
static char s_reply_emotion[32]="happy",shown[320],mood[32];
static int count,updates,allocated,released;static bool fail;
static voice_caption_t *queue[32];
static void *heap_caps_malloc(size_t n,int caps){if(fail)return NULL;allocated++;return malloc(n);}
static void release(void *p){released++;free(p);}
#define free release
static int xQueueSend(void *q,voice_caption_t **p,int wait){if(count==32)return 0;queue[count++]=*p;return 1;}
static int xQueuePeek(void *q,voice_caption_t **p,int wait){if(!count)return 0;*p=queue[0];return 1;}
static int xQueueReceive(void *q,voice_caption_t **p,int wait){if(!count)return 0;*p=queue[0];memmove(queue,queue+1,--count*sizeof(*queue));return 1;}
static void pet_post(const char *e,const char *t,bool spoken){assert(spoken);updates++;snprintf(shown,sizeof(shown),"%s",t);snprintf(mood,sizeof(mood),"%s",e);}
"""
        for signature in ('static bool caption_enqueue(uint32_t at_ms, const char *text)',
                          'static void caption_pump(void)', 'static void captions_clear(void)'):
            source += function('voice_client.c', signature)
        source += r"""
int main(void){
 assert(caption_enqueue(0,"Hello") && caption_enqueue(600,"Hello world"));assert(!updates);
 strcpy(s_reply_emotion,"sad");
 s_tts_written_samples=4800;caption_pump();assert(updates==1 && !strcmp(shown,"Hello") && !strcmp(mood,"happy"));
 for(int i=0;i<20;i++)caption_pump();assert(updates==1);
 s_tts_written_samples=9599;caption_pump();assert(updates==1);
 s_tts_written_samples=9600;caption_pump();assert(updates==2 && !strcmp(shown,"Hello world"));
 assert(!caption_enqueue(599,"倒序") && !caption_enqueue(600001,"超界") && !caption_enqueue(600,""));
 for(int i=0;i<32;i++)assert(caption_enqueue(1000+i,"等待"));
 assert(!caption_enqueue(1032,"满队列"));
 captions_clear();assert(!count && !s_tts_written_samples && allocated==released);
 s_caption_received_ms=0;fail=true;assert(!caption_enqueue(100,"内存失败"));fail=false;
 char text[601];for(int i=0;i<200;i++)memcpy(text+i*3,"猫",3);text[600]=0;
 assert(caption_enqueue(0,text));s_tts_written_samples=1;caption_pump();
 assert(strlen(shown)==300 && !strcmp(shown+297,"猫"));
 captions_clear();assert(allocated==released);
 return 0;
}
"""
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
static atomic_bool s_requested, s_ready, s_connected, s_hello, s_listening, s_reply_active, s_have_reply, s_tts_end, s_drop_reply;
static atomic_uint s_progress_tick;
static unsigned s_tts_packets,s_tts_bytes;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define VOICE_PREBUFFER_MIN 6
static uint32_t s_caption_received_ms;
static atomic_uint s_prebuffer_target;
static atomic_bool s_first_reply;
static unsigned xTaskGetTickCount(void){return 100;}

static atomic_uint s_epoch;
static unsigned s_session_epoch;
static char s_session_id[96], reply[512], sent[512];
static int sends,updates,volume;
static int audio_get_playback_volume(void){return volume;}
static void audio_set_playback_volume(int v){volume=v;}
static char mood[32],s_reply_emotion[32];
static bool s_reply_presented;
static char caption[512],caption_mood[32];static uint32_t caption_ms;static int captions;
static bool caption_enqueue(uint32_t at,const char *text){captions++;caption_ms=at;snprintf(caption,sizeof(caption),"%s",text);snprintf(caption_mood,sizeof(caption_mood),"%s",s_reply_emotion[0]?s_reply_emotion:"speaking");return true;}

static void pet_set(const char *emotion, const char *text) {
    updates++;snprintf(mood,sizeof(mood),"%s",emotion);
    if (text) snprintf(reply, sizeof(reply), "%s", text);
}
static void play_caption(void){pet_set(caption_mood,caption);}
static bool send_json(const char *text) {
    sends++; snprintf(sent, sizeof(sent), "%s", text); return true;
}
'''
        source += 'static bool watch_control_receive(const cJSON *payload){return true;}\n'
        for signature in ('static bool session_current(void)', 'bool voice_client_is_active(void)',
                          'bool voice_client_set_active(bool active)', 'static void send_listen_start(void)',
                          'static const char *json_string(const cJSON *json, const char *key)',
                          'static void handle_text(const char *data, size_t len)'):
            source += function('voice_client.c', signature)
        source += r'''
int main(void) {
    assert(!voice_client_set_active(true));
    s_ready = true; assert(voice_client_set_active(true));assert(volume==0);
    volume=22;
    s_session_epoch = s_epoch; assert(session_current());
    unsigned epoch=s_epoch;assert(voice_client_set_active(true) && s_epoch==epoch && session_current() && volume==22);
    voice_client_set_active(false); assert(!session_current());
    assert(voice_client_set_active(true)); assert(!session_current() && volume==22);
    s_session_epoch = s_epoch;
    send_listen_start(); assert(sends == 0);
    const char *early = "{\"type\":\"stt\",\"text\":\"不应显示\"}";
    handle_text(early, strlen(early)); assert(reply[0] == 0);
    const char *hello = "{\"type\":\"hello\",\"session_id\":\"id\\\"quoted\"}";
    handle_text(hello, strlen(hello)); assert(sends == 1 && s_listening);
    cJSON *j = cJSON_Parse(sent); assert(j);
    assert(!strcmp(json_string(j, "session_id"), "id\"quoted")); cJSON_Delete(j);
    const char *partial="{\"type\":\"stt\",\"text\":\"你好呀\",\"final\":false}";
    handle_text(partial,strlen(partial));assert(s_listening && !s_reply_active && !strcmp(mood,"listening") && !strcmp(reply,"你好呀"));
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
    int before=updates;
    const char *emotion="{\"type\":\"llm\",\"emotion\":\"happy\",\"text\":\"不要提前替换 ASR\"}";
    handle_text(emotion,strlen(emotion));
    assert(updates==before && !strcmp(reply,"你好") && !strcmp(mood,"thinking"));
    const char *tts_start="{\"type\":\"tts\",\"state\":\"start\"}";
    handle_text(tts_start,strlen(tts_start));assert(s_reply_active && !s_listening);
    const char *sentence="{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"继续聊天\"}";
    handle_text(sentence,strlen(sentence));assert(s_have_reply && !strcmp(reply,"你好") && caption_ms==0);play_caption();assert(!strcmp(reply,"继续聊天") && !strcmp(mood,"happy"));
    before=updates;
    const char *late="{\"type\":\"llm\",\"emotion\":\"sad\"}";
    handle_text(late,strlen(late));assert(updates==before && !strcmp(mood,"happy"));
    handle_text(sentence,strlen(sentence));assert(!strcmp(mood,"happy"));
    const char *stop="{\"type\":\"tts\",\"state\":\"stop\"}";
    /* TTS 结束后等播放队列排空，由 tts_pump 恢复监听，handle_text 只置结束标记。 */
    handle_text(stop,strlen(stop));assert(atomic_load(&s_tts_end) && s_reply_active && !s_listening && !strcmp(reply,"继续聊天"));
    const char *foreign="{\"type\":\"stt\",\"session_id\":\"old\",\"text\":\"不要覆盖\"}";
    handle_text(foreign,strlen(foreign));assert(!strcmp(reply,"继续聊天"));
    handle_text(raw,strlen(raw));assert(!strcmp(mood,"thinking") && !s_reply_presented && !s_reply_emotion[0]);
    handle_text(tts_start,strlen(tts_start));
    handle_text(sentence,strlen(sentence));play_caption();assert(!strcmp(mood,"speaking"));
    before=updates;handle_text(late,strlen(late));assert(updates==before && !strcmp(mood,"speaking"));
    s_drop_reply=true;s_reply_active=false;s_listening=true;before=updates;
    handle_text(tts_start,strlen(tts_start));handle_text(sentence,strlen(sentence));
    handle_text(emotion,strlen(emotion));handle_text(stop,strlen(stop));
    assert(s_drop_reply && !s_reply_active && s_listening && updates==before);
    handle_text(foreign,strlen(foreign));assert(s_drop_reply);
    handle_text(raw,strlen(raw));assert(!s_drop_reply && s_reply_active && !s_listening);

    handle_text(hello,strlen(hello));assert(!s_reply_presented && !s_reply_emotion[0]);
    handle_text(raw,strlen(raw));
    const char *stream="{\"type\":\"llm\",\"state\":\"partial\",\"text\":\"实时回答\"}";
    handle_text(stream,strlen(stream));assert(!strcmp(reply,"你好"));
    handle_text(sentence,strlen(sentence));assert(!strcmp(reply,"你好") && !strcmp(caption,"继续聊天"));
    const char *revision="{\"type\":\"llm\",\"state\":\"partial\",\"text\":\"实时回答完成\",\"final\":true}";
    handle_text(revision,strlen(revision));assert(!strcmp(reply,"你好"));
    const char *word="{\"type\":\"tts\",\"state\":\"word_boundary\",\"at_ms\":240,\"text\":\"正在说的词\"}";
    handle_text(word,strlen(word));assert(caption_ms==240 && !strcmp(caption,"正在说的词") && !strcmp(reply,"你好"));
    int caption_before=captions;
    const char *invalid="{\"type\":\"tts\",\"state\":\"word_boundary\",\"at_ms\":-1,\"text\":\"错误\"}";
    handle_text(invalid,strlen(invalid));assert(captions==caption_before);
    handle_text(hello,strlen(hello));assert(!s_reply_presented);
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
static lv_obj_t time_obj,date_obj,lunar_obj,loading_obj;
static lv_obj_t *s_time=&time_obj,*s_date=&date_obj,*s_lunar=&lunar_obj,*s_time_loading=&loading_obj;
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
        source += (MAIN / 'lunar_date.c').read_text()
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
    fake_tm.tm_year=70;time_tick_cb(NULL);assert(time_obj.hidden && !loading_obj.hidden && lunar_obj.hidden && !strcmp(date_obj.text,"正在校时"));
    fake_tm=(struct tm){.tm_year=126,.tm_mon=0,.tm_mday=17,.tm_yday=16,.tm_wday=6,.tm_hour=12};
    time_tick_cb(NULL);assert(!time_obj.hidden && !strcmp(time_obj.text,"12:00"));
    assert(!strcmp(date_obj.text,"1月17日 周六"));
    assert(s_home_snap_dirty);s_home_snap_dirty=false;time_tick_cb(NULL);assert(!s_home_snap_dirty);
    fake_tm.tm_mon=1;fake_tm.tm_yday=47;fake_tm.tm_wday=2;time_tick_cb(NULL);
    assert(!strcmp(date_obj.text,"2月17日 周二"));
    assert(loading_obj.hidden && !lunar_obj.hidden && !strcmp(lunar_obj.text,"农历 正月初一"));
    fake_tm.tm_year=151;time_tick_cb(NULL);assert(!time_obj.hidden && lunar_obj.hidden);
    fake_tm.tm_year=70;time_tick_cb(NULL);assert(time_obj.hidden && !loading_obj.hidden && lunar_obj.hidden);
    return 0;
}
'''
        run_c(source)

    def test_bottom_edge_pull_opens_notice_sheet(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {int x,y;} lv_point_t;
typedef struct {int code;} lv_event_t;
typedef int lv_event_code_t;
typedef int lv_indev_t;
typedef int lv_obj_t;
enum {LV_EVENT_PRESSED=1, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST};
#define SCREEN_H 466
#define CENTER_DRAG_TH 8
#define NOTICE_H SCREEN_H
#define NOTICE_EDGE_H (SCREEN_H / 12)
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_UNUSED(x) (void)(x)
static bool s_notice_open,s_notice_dragging,s_notice_drag_engaged;
static bool s_center_open,s_center_dragging,s_nur_drag_engaged,s_home_snap_dirty;
static int s_notice_press_x,s_notice_press_y,s_notice_press_reveal,s_notice_reveal;
static int fake_indev,point_x,point_y;
static lv_obj_t notice_obj;
static lv_obj_t *s_notice=&notice_obj;
static bool pages_busy_except_center(void){return false;}
static bool page_anim_busy(void){return false;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return &fake_indev;}
static void lv_indev_get_point(lv_indev_t *i,lv_point_t *p){p->x=point_x;p->y=point_y;}
static void lv_obj_remove_flag(lv_obj_t *o,int f){LV_UNUSED(o);LV_UNUSED(f);}
static void notice_anim_stop(void){}
static void notice_reveal_set(int h){if(h<0)h=0;if(h>NOTICE_H)h=NOTICE_H;s_notice_reveal=h;}
static void notice_open(void){s_notice_open=true;s_notice_reveal=NOTICE_H;}
static void notice_animate_reveal(int to,int b,int c,int d){s_notice_reveal=to;}
static int lv_anim_path_ease_in,notice_hide_done;
'''
        source += function('ui.c', 'static void home_notice_pull_cb(lv_event_t *e)')
        source += r'''
int main(void){
    lv_event_t e={LV_EVENT_PRESSED};
    /* 中部起手不响应 */
    point_y=233;home_notice_pull_cb(&e);
    point_y=100;e.code=LV_EVENT_PRESSING;home_notice_pull_cb(&e);
    e.code=LV_EVENT_RELEASED;home_notice_pull_cb(&e);assert(!s_notice_open && s_notice_reveal==0);
    /* 底部横滑不抢手势 */
    point_x=420;point_y=455;e.code=LV_EVENT_PRESSED;home_notice_pull_cb(&e);
    point_x=300;point_y=445;e.code=LV_EVENT_PRESSING;home_notice_pull_cb(&e);
    e.code=LV_EVENT_RELEASED;home_notice_pull_cb(&e);assert(!s_notice_open && s_notice_reveal==0);
    /* 底部上拉超过 1/4 才吸附展开 */
    point_x=233;point_y=455;e.code=LV_EVENT_PRESSED;home_notice_pull_cb(&e);
    point_y=355;e.code=LV_EVENT_PRESSING;home_notice_pull_cb(&e);assert(s_notice_reveal==100);
    point_y=335;home_notice_pull_cb(&e);assert(s_notice_reveal==120);
    e.code=LV_EVENT_RELEASED;home_notice_pull_cb(&e);assert(s_notice_open && s_notice_reveal==NOTICE_H);
    return 0;
}
'''
        run_c(source)

    def test_notice_page_is_fullscreen_and_minimal(self):
        ui = (MAIN / "ui.c").read_text()
        self.assertIn("#define NOTICE_H        SCREEN_H", ui)
        body = ui.split("static void build_notice(void)", 1)[1].split("\n}", 1)[0]
        # 只保留标题和「没有通知」，不再放分类图标与说明文案。
        self.assertEqual(body.count("mk_label("), 2, body)
        self.assertNotIn("新的提醒会出现在这里", body)
        self.assertNotIn("chip_x", body)
        self.assertNotIn("draw_chat_mini", body)

    def test_wifi_saved_click_tries_before_opening_password(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LV_UNUSED(x) ((void)x)
#define ESP_OK 0
#define COL_TECH_GREEN 0
#define COL_CORAL 0
#define LV_OBJ_FLAG_HIDDEN 0
typedef int epw_wifi_state_t;
enum {EPW_WIFI_OFF,EPW_WIFI_CONNECTING,EPW_WIFI_CONNECTED,EPW_WIFI_FAILED};
typedef int lv_event_t;typedef int lv_obj_t;typedef int lv_timer_t;
typedef struct {char ssid[33];int8_t rssi;bool secure;}epw_wifi_ap_t;
static bool s_wifi_open=true,s_wifipwd_open,s_wifipwd_connecting,s_wifipwd_secure,s_wifi_scan_seen=true;
static int s_wifi_ap_count=1,state=EPW_WIFI_CONNECTING,saved_result,opened,saved_attempts,connected;
static char s_wifipwd_ssid[33];
static epw_wifi_ap_t s_wifi_aps[1]={{{0},0,true}};
static lv_obj_t obj,*s_wifi_scan_hint=&obj,*s_wifipwd_btn_label=&obj,*s_wifipwd_btn=&obj,*s_wifipwd_hint=&obj;
static bool tap_ignore(void *e){return false;}
static void *lv_event_get_current_target(void *e){return &obj;}
static void *lv_obj_get_user_data(void *r){return 0;}
static bool wifi_scan_busy(void){return false;}
static void toast_show(const char *t){}
static void wifi_status_refresh(void){}
static int wifi_connect_saved(const char *s){assert(!strcmp(s,"known"));saved_attempts++;return saved_result;}
static int wifi_connect_to(const char *s,const char *p){assert(!*p);connected++;return 0;}
static void wifi_pwd_open(const epw_wifi_ap_t *ap){assert(!strcmp(ap->ssid,"known"));opened++;s_wifipwd_open=true;}
static int wifi_get_state(void){return state;}
static void wifi_populate_list(void){}
static void lv_label_set_text(void *o,const char *s){}
static void lv_obj_set_style_bg_color(void *o,int c,int s){}
static void lv_obj_set_style_text_color(void *o,int c,int s){}
static void lv_obj_remove_flag(void *o,int f){}
static void wifi_pwd_close_now(void){s_wifipwd_open=false;}
static void wifi_close(void){s_wifi_open=false;}
'''
        source += function('ui.c', 'static void wifi_row_cb(lv_event_t *e)')
        source += function('ui.c', 'static void wifi_poll_cb(lv_timer_t *timer)')
        source += r'''
int main(void){
 strcpy(s_wifi_aps[0].ssid,"known");
 wifi_row_cb(0);assert(saved_attempts==1 && !opened && s_wifipwd_connecting);
 state=EPW_WIFI_FAILED;wifi_poll_cb(0);assert(opened==1 && s_wifipwd_open && !s_wifipwd_connecting);
 s_wifipwd_open=false;saved_result=-1;wifi_row_cb(0);assert(opened==2);
 s_wifipwd_open=false;saved_result=0;wifi_row_cb(0);
 state=EPW_WIFI_CONNECTED;wifi_poll_cb(0);assert(!s_wifi_open && !s_wifipwd_connecting && opened==2);
 s_wifi_open=true;s_wifi_aps[0].secure=false;wifi_row_cb(0);
 assert(connected==1 && !s_wifipwd_secure);
 return 0;
}
'''
        run_c(source)

    def test_wifi_saved_password_history_migration_and_failure(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND -2
#define ESP_ERR_NOT_FOUND -3
#define ESP_ERR_INVALID_ARG -4
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define WIFI_NVS_NS "epw_wifi"
#define WIFI_NVS_SSID "ssid"
#define WIFI_NVS_PASS "pass"
#define ESP_LOGW(...) ((void)0)
typedef int nvs_handle_t;
typedef int esp_err_t;
typedef struct {char ssid[33];char password[65];}wifi_saved_cred_t;
static char legacy_ssid[33]="old",legacy_pass[65]="oldpass1",s_ssid[33]="old";
static struct {char key[16];wifi_saved_cred_t value;} records[8];
static int count,commits,connects,error,collision;
static char connected_ssid[33],connected_pass[65];
static uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t *p,size_t n){
 uint32_t h=seed;while(n--)h=h*33+*p++;return collision?1:h;
}
static int nvs_open(const char *ns,int mode,int *h){*h=1;return 0;}
static void nvs_close(int h){}
static int nvs_get_str(int h,const char *key,char *out,size_t *len){
 const char *v=!strcmp(key,"ssid")?legacy_ssid:legacy_pass;
 if(!v[0])return -2;assert(*len>strlen(v));strcpy(out,v);return 0;
}
static int nvs_set_str(int h,const char *key,const char *v){
 strcpy(!strcmp(key,"ssid")?legacy_ssid:legacy_pass,v);return 0;
}
static int nvs_get_blob(int h,const char *key,void *out,size_t *len){
 for(int i=0;i<count;i++)if(!strcmp(records[i].key,key)){
  memcpy(out,&records[i].value,sizeof(wifi_saved_cred_t));*len=sizeof(wifi_saved_cred_t);return 0;}
 return -2;
}
static int nvs_set_blob(int h,const char *key,const void *v,size_t len){
 if(error)return error;int i=0;for(;i<count;i++)if(!strcmp(records[i].key,key))break;
 if(i==count)count++;assert(count<=8);strcpy(records[i].key,key);memcpy(&records[i].value,v,len);return 0;
}
static int nvs_erase_key(int h,const char *key){
 if(!strcmp(key,"ssid"))legacy_ssid[0]=0;
 else if(!strcmp(key,"pass"))legacy_pass[0]=0;
 else for(int i=0;i<count;i++)if(!strcmp(records[i].key,key))records[i]=records[--count];
 return 0;
}
static int nvs_commit(int h){commits++;return 0;}
static const char *esp_err_to_name(int e){return "error";}
static int wifi_connect_to(const char *ssid,const char *pass){
 connects++;strcpy(connected_ssid,ssid);strcpy(connected_pass,pass);return 0;
}
'''
        for signature in ('static void creds_key(const char *ssid, char key[16])',
                          'static bool creds_read(nvs_handle_t h, const char *ssid, wifi_saved_cred_t *cred)',
                          'static esp_err_t creds_store(nvs_handle_t h, const wifi_saved_cred_t *cred)',
                          'static void creds_save(const char *ssid, const char *password)',
                          'static void creds_erase(void)',
                          'esp_err_t wifi_connect_saved(const char *ssid)'):
            source += function('wifi.c', signature)
        source += r'''
int main(void){
 assert(wifi_connect_saved("old")==0 && !strcmp(connected_pass,"oldpass1"));
 assert(wifi_connect_saved("unknown")==-3 && connects==1);
 creds_save("new","newpass1");assert(commits==1 && count==2);
 assert(wifi_connect_saved("old")==0 && !strcmp(connected_pass,"oldpass1"));
 assert(wifi_connect_saved("new")==0 && !strcmp(connected_pass,"newpass1"));
 creds_save("old","updated1");assert(count==2);
 assert(wifi_connect_saved("old")==0 && !strcmp(connected_pass,"updated1"));
 error=-1;creds_save("third","thirdpass");assert(commits==2 && !strcmp(legacy_ssid,"old"));error=0;
 creds_erase();assert(wifi_connect_saved("old")==-3);
 assert(wifi_connect_saved("new")==0 && !strcmp(connected_pass,"newpass1"));
 assert(wifi_connect_saved(NULL)==-4 && wifi_connect_saved("")==-4);
 collision=1;creds_save("one","password1");int before=commits;
 creds_save("two","password2");assert(commits==before);
 assert(wifi_connect_saved("one")==0 && !strcmp(connected_pass,"password1"));
 assert(wifi_connect_saved("two")==-3);
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
#define ESP_LOGE(...) ((void)0)
typedef int esp_audio_err_t;
#define ESP_CODEC_DEV_OK 0
#define pdMS_TO_TICKS(x) (x)
typedef struct {uint8_t *buffer;uint32_t len;}esp_audio_enc_in_frame_t;
typedef struct {uint8_t *buffer;uint32_t len;unsigned encoded_bytes;}esp_audio_enc_out_frame_t;
static atomic_bool s_requested,s_connected,s_hello,s_listening,s_reply_active,s_interrupt_requested;
static bool s_barge_ready;
static int audio_get_playback_volume(void){return 0;}
static bool voice_barge_begin(void){return true;}
static bool voice_barge_feed(const int16_t *m,const int16_t *r,size_t n);
static bool tts_interrupt(bool with_preroll);
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
static int mode,reads,encodes,sends,opens,closes,interrupts;
static bool voice_barge_feed(const int16_t *m,const int16_t *r,size_t n){assert(n==960);for(size_t i=0;i<n;i++)assert(r[i]==0);return mode==9;}
static bool tts_interrupt(bool with_preroll){assert(with_preroll==(mode==9));interrupts++;s_listening=true;s_reply_active=false;return true;}
static jmp_buf finished;
static int wifi_get_state(void){return EPW_WIFI_CONNECTED;}
static void vTaskDelay(int ticks){if(!s_requested)longjmp(finished,1);if(ticks!=500)s_requested=false;}
static void pet_set(const char *emotion,const char *text){}
static bool voice_client_set_active(bool value){s_requested=value;return value;}
static bool ws_start(void){s_connected=true;s_hello=mode!=2;s_listening=mode==11 || mode!=10;s_reply_active=mode==9 || mode==10;s_interrupt_requested=mode==10 || mode==11;return true;}
static bool tts_pump(void){return false;}
static void tts_close(void){}
static bool open_audio(void){opens++;s_enc=(void*)1;return true;}
static int esp_opus_enc_get_frame_size(void *enc,int *in,int *out){*in=1920;*out=128;return 0;}
static int esp_codec_dev_read(void *mic,void *pcm,int len){reads++;assert(len==3840);memset(pcm,1,len);if(mode==1)s_requested=false;if(mode==4 && reads==2)s_listening=true;return mode==3?1:0;}
static int esp_opus_enc_process(void *enc,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out){encodes++;assert(in->len==1920);for(unsigned i=0;i<in->len;i++)assert(in->buffer[i]==1);out->encoded_bytes=8;return mode==6?-1:0;}
static int esp_websocket_client_send_bin(void *ws,const char *data,int len,int timeout){assert(len==8);sends++;if(mode==7 && sends==1)return -1;if(mode==8)return -1;if((mode==4 || mode==9) && sends==1)s_listening=false;else s_requested=false;return mode==5?-1:len;}
static void close_audio(void){s_enc=NULL;}
static void close_session(void){closes++;free(s_message);s_message=NULL;close_audio();}
'''
        source += function('voice_client.c', 'static void mix_microphones(uint8_t *pcm, size_t mono_bytes)')
        source += function('voice_client.c', 'static int pcm_peak(const uint8_t *pcm, size_t bytes)')
        source += function('voice_client.c', 'static bool session_current(void)')
        source += function('voice_client.c', 'static bool voice_wait_expired(uint32_t now, uint32_t hello_since)')
        source += 'static bool s_control_complete; static void watch_control_reset(void){} static void watch_control_poll(bool (*send)(const char*)){} static bool send_json(const char *s){return true;} static bool watch_control_pending(void){return false;} static void watch_control_finish(bool apply){}\n'
        source += function('voice_client.c', 'static void voice_task(void *arg)')
        source += r'''
int main(void){
    for(mode=0;mode<12;mode++){
        reads=encodes=sends=opens=closes=interrupts=0;s_requested=true;s_epoch=1;s_enc=NULL;
        if(setjmp(finished)==0)voice_task(NULL);
        assert(closes==(mode==7?2:mode==8?3:1));
        if(mode==7){assert(sends==2 && opens==2);}
        else if(mode==8){assert(sends==3 && opens==3);}
        else if(mode==9){assert(reads==3 && encodes==2 && sends==2 && interrupts==1);}
        else if(mode==10){assert(reads==1 && sends==1 && interrupts==1 && !s_interrupt_requested);}
        else if(mode==11){assert(reads==1 && encodes==1 && sends==1 && interrupts==1 && s_listening && !s_requested);}
        else if(mode==0){assert(reads==1 && encodes==1 && sends==1);}
        else if(mode==4){assert(reads==3 && encodes==2 && sends==2);}
        else if(mode==5){assert(reads==1 && encodes==1 && sends==1);}
        else if(mode==6){assert(reads==1 && encodes==1 && sends==0);}
        else if(mode==2){assert(opens==0 && reads==0 && sends==0);}
        else{assert(reads==1 && encodes==0 && sends==0);}
    }
    return 0;
}
'''
        run_c(source)

    def test_voice_interrupt_allowed_while_listening_or_replying_only(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
static atomic_bool s_requested,s_connected,s_hello,s_listening,s_reply_active,s_interrupt_requested;
static bool voice_client_is_active(void){return s_requested;}
'''
        source += function('voice_client.c', 'bool voice_client_can_interrupt(void)')
        source += function('voice_client.c', 'bool voice_client_interrupt(void)')
        source += r'''
int main(void){
 s_requested=true;s_connected=true;s_hello=true;
 assert(!voice_client_can_interrupt());
 assert(!voice_client_interrupt() && !s_interrupt_requested);
 s_listening=true;assert(voice_client_can_interrupt());
 assert(voice_client_interrupt() && s_interrupt_requested);
 s_interrupt_requested=false;s_listening=false;s_reply_active=true;
 assert(voice_client_can_interrupt() && voice_client_interrupt() && s_interrupt_requested);
 s_interrupt_requested=false;s_connected=false;
 assert(!voice_client_can_interrupt() && !voice_client_interrupt() && !s_interrupt_requested);
 s_connected=true;s_hello=false;assert(!voice_client_can_interrupt());
 s_hello=true;s_reply_active=false;s_listening=true;s_requested=false;
 assert(!voice_client_can_interrupt() && !voice_client_interrupt());
 return 0;
}
'''
        run_c(source)
        touch = function('ui.c', 'static void pet_touch_cb(lv_event_t *e)')
        self.assertIn('voice_client_interrupt()', touch)
        self.assertLess(touch.index('tap_ignore(e)'), touch.index('voice_client_interrupt()'))
        stop = function('ui.c', 'static void pet_stop_cb(lv_event_t *e)')
        self.assertIn('voice_client_interrupt()', stop)
        self.assertLess(stop.index('tap_ignore(e)'), stop.index('voice_client_interrupt()'))

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

    def test_nursery_tasks_leave_internal_ram_for_audio_and_network(self):
        # Execute init with a deliberately unavailable internal stack allocator.
        init = function('nursery.c', 'void nursery_init(void)')
        creation = init[init.index('    /* 将联网与解码栈'):]
        source = r'''#include <assert.h>
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define ESP_LOGE(...) ((void)0)
static int tasks;
static void catalog_task(void *p){}
static void playback_task(void *p){}
static int xTaskCreateWithCaps(void (*fn)(void*),const char *n,int bytes,void *p,int prio,void *h,int caps){
 assert(fn==catalog_task && bytes==8192 && caps==3);tasks++;return pdPASS;
}
static int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void*),const char *n,int bytes,void *p,int prio,void *h,int core,int caps){
 assert(fn==playback_task && bytes==12288 && core==0 && caps==3);tasks++;return pdPASS;
}
static void initialize(void){
'''
        source += creation
        source += 'int main(void){initialize();assert(tasks==2);}'
        run_c(source)
        catalog = function('nursery.c', 'static void catalog_task(void *arg)')
        self.assertEqual(catalog.count('vTaskDeleteWithCaps(NULL)'), 2)
        self.assertNotIn('vTaskDelete(NULL)', catalog)

    def test_nursery_checkin_slot_states(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nursery.h"
typedef int lv_color_t;
typedef struct {char text[100]; bool hidden;} lv_obj_t;
static lv_obj_t objects[12];
static lv_obj_t *s_nur_slot_name[3]={&objects[0],&objects[1],&objects[2]};
static lv_obj_t *s_nur_slot_sub[3]={&objects[3],&objects[4],&objects[5]};
static lv_obj_t *s_nur_slot_end[3]={&objects[6],&objects[7],&objects[8]};
static lv_obj_t *s_nur_slot[3]={&objects[9],&objects[10],&objects[11]};
#define COL_MUTED 1
#define COL_LINE 2
#define COL_TEAL 3
#define COL_AMBER 4
#define LV_OBJ_FLAG_HIDDEN 1
static void lv_label_set_text(lv_obj_t *o,const char *t){snprintf(o->text,sizeof(o->text),"%s",t);}
static void lv_obj_set_style_text_color(lv_obj_t *o,int c,int p){}
static void lv_obj_set_style_border_color(lv_obj_t *o,int c,int p){}
static void lv_obj_add_flag(lv_obj_t *o,int f){o->hidden=true;}
static void lv_obj_remove_flag(lv_obj_t *o,int f){o->hidden=false;}
static epw_nursery_session_t sessions[3];
static epw_nursery_state_t state=EPW_NURSERY_PLAYING;
static void nursery_checkin_session_mock(int i,epw_nursery_session_t *out){*out=sessions[i];}
#define nursery_checkin_session nursery_checkin_session_mock
static epw_nursery_state_t nursery_get_state_mock(void){return state;}
#define nursery_get_state nursery_get_state_mock
'''
        source += function('ui.c', 'static void nur_format_time(uint32_t ms, char *out, size_t size)')
        source += function('ui.c', 'static void nursery_update_checkin(void)')
        source += r'''int main(void){
    for(int i=0;i<3;i++)sessions[i].target_minutes=15;
    nursery_update_checkin();
    assert(strcmp(s_nur_slot_name[0]->text,"早安")==0);
    assert(strcmp(s_nur_slot_name[1]->text,"午后")==0);
    assert(strcmp(s_nur_slot_name[2]->text,"晚安")==0);
    assert(strcmp(s_nur_slot_sub[0]->text,"15分钟")==0);
    assert(s_nur_slot_end[0]->hidden);
    sessions[0].active=true;sessions[0].elapsed_seconds=1;
    nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"剩余 14:59")==0 && !s_nur_slot_end[0]->hidden);
    assert(s_nur_slot_end[1]->hidden);
    sessions[0].elapsed_seconds=2;nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"剩余 14:58")==0);
    sessions[0].paused=true;nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"暂停 14:58")==0);
    sessions[0].paused=false;state=EPW_NURSERY_PAUSED;nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"暂停 14:58")==0);
    state=EPW_NURSERY_LOADING;nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"等待 14:58")==0);
    state=EPW_NURSERY_PLAYING;sessions[0].elapsed_seconds=9999;nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"剩余 0:00")==0 && !s_nur_slot_end[0]->hidden);
    sessions[0].active=false;sessions[0].done=true;nursery_update_checkin();
    assert(strcmp(s_nur_slot_sub[0]->text,"完成")==0 && s_nur_slot_end[0]->hidden);
    return 0;
}'''
        run_c(source)

    def test_nursery_checkin_end_button_and_wish_visibility(self):
        wish = function('ui.c', 'static bool nur_wish_visible(int stars, int goal, bool ready, bool redeemed)')
        run_c(r'''#include <assert.h>
#include <stdbool.h>
''' + wish + r'''
int main(void){
    assert(!nur_wish_visible(0,5,false,false));
    assert(!nur_wish_visible(2,5,false,false));
    assert(nur_wish_visible(3,5,false,false));
    assert(nur_wish_visible(5,5,false,false));
    assert(nur_wish_visible(0,5,true,false));
    assert(nur_wish_visible(0,5,false,true));
    assert(!nur_wish_visible(0,0,false,false));
    assert(!nur_wish_visible(4,0,false,false));
    return 0;
}''')

        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "nursery.h"
typedef int lv_event_t;
static int ends,toasts,updates;
static uint32_t s_nur_ui_ms;
static epw_nursery_session_t sessions[3];
static bool tap_ignore(lv_event_t *e){return false;}
static void lv_event_stop_bubbling(lv_event_t *e){}
static void *lv_event_get_user_data(lv_event_t *e){return (void *)(uintptr_t)*e;}
static void nursery_checkin_session_mock(int i,epw_nursery_session_t *out){*out=sessions[i];}
#define nursery_checkin_session nursery_checkin_session_mock
static void nursery_checkin_end_mock(void){ends++;}
#define nursery_checkin_end nursery_checkin_end_mock
static void toast_show(const char *s){toasts++;}
static void nursery_update_ui(void){updates++;}
'''
        source += function('ui.c', 'static void nur_slot_end_cb(lv_event_t *e)')
        source += r'''int main(void){
    int slot=(int)(intptr_t)1;
    sessions[1].active=false;
    nur_slot_end_cb(&slot);assert(ends==0 && updates==0);
    sessions[1].active=true;
    nur_slot_end_cb(&slot);assert(ends==1 && toasts==1 && updates==1);
    return 0;
}'''
        run_c(source)

    def test_settings_and_timer_swipe_back(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define LV_EVENT_GESTURE 0
#define LV_DIR_RIGHT 1
#define LV_DIR_LEFT 2
#define LV_DIR_TOP 3
#define LV_DIR_BOTTOM 4
enum {WATCH_TIMER_IDLE,WATCH_TIMER_RUNNING};
typedef int lv_event_t;
typedef int lv_indev_t;
typedef int lv_dir_t;
#define NULL ((void *)0)
static int closes,steps,step_dir,code=LV_EVENT_GESTURE,dir=LV_DIR_RIGHT;
static bool busy;
static int s_tools_kind;
static int timer_state;
static bool page_anim_busy(void){return busy;}
static int lv_event_get_code(lv_event_t *e){return code;}
static lv_indev_t *lv_event_get_indev(lv_event_t *e){return (lv_indev_t *)1;}
static int lv_indev_get_gesture_dir(lv_indev_t *i){return dir;}
static void tools_close(void){closes++;}
static void idle_step(int delta){steps++;step_dir=delta;}
static int watch_timer_state(void){return timer_state;}
'''
        source += function('ui.c', 'static void tools_gesture_cb(lv_event_t *e)')
        source += r'''
int main(void){
    busy=true;tools_gesture_cb(&(int){0});assert(!closes);
    busy=false;code=1;tools_gesture_cb(&(int){0});assert(!closes);
    code=LV_EVENT_GESTURE;dir=LV_DIR_RIGHT;s_tools_kind=0;tools_gesture_cb(&(int){0});
    assert(closes==1 && steps==0);
    s_tools_kind=1;tools_gesture_cb(&(int){0});assert(closes==2 && steps==0);
    s_tools_kind=0;timer_state=WATCH_TIMER_IDLE;
    dir=LV_DIR_TOP;tools_gesture_cb(&(int){0});assert(steps==1 && step_dir==1);
    dir=LV_DIR_BOTTOM;tools_gesture_cb(&(int){0});assert(steps==2 && step_dir==-1);
    dir=LV_DIR_LEFT;tools_gesture_cb(&(int){0});assert(steps==2);
    timer_state=WATCH_TIMER_RUNNING;dir=LV_DIR_TOP;tools_gesture_cb(&(int){0});assert(steps==3);
    s_tools_kind=1;dir=LV_DIR_TOP;tools_gesture_cb(&(int){0});assert(steps==3);
    return 0;
}'''
        run_c(source)
        build = function('ui.c', 'static void build_tools(void)')
        self.assertIn('lv_obj_add_event_cb(s_tools, tools_gesture_cb, LV_EVENT_GESTURE', build)
        self.assertIn('timer_start_cb', build)
        self.assertIn('timer_preset_cb', build)

    def test_nursery_player_commands_resume_checkin(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CMD_PAUSE 1
#define CMD_RESUME 2
#define CMD_PLAY_INDEX 3
#define CMD_PLAY_WEEKLY 4
#define pdTRUE 1
typedef struct {uint8_t cmd;int16_t arg;} nursery_cmd_t;
static int queue,saves,accepted=1;
static void *s_cmd_q=&queue;
static struct {bool valid,paused;} s_act;
static uint32_t s_tick_ms;
static int clamp_int(int v,int a,int b){return v<a?a:v>b?b:v;}
static int xQueueSend(void *q,nursery_cmd_t *m,int ticks){return accepted;}
static int64_t esp_timer_get_time(void){return 123000;}
static void nvs_save_active(void){saves++;}
'''
        source += function('nursery.c', 'static void send_cmd(uint8_t cmd, int arg)')
        source += r'''int main(void){
    s_act.valid=true;send_cmd(CMD_PAUSE,0);assert(s_act.paused && saves==1);
    send_cmd(CMD_RESUME,0);assert(!s_act.paused && saves==2 && s_tick_ms==123);
    send_cmd(CMD_RESUME,0);assert(saves==2);
    s_act.paused=true;send_cmd(CMD_PLAY_INDEX,0);assert(!s_act.paused);
    s_act.paused=true;send_cmd(CMD_PLAY_WEEKLY,0);assert(!s_act.paused);
    accepted=0;send_cmd(CMD_PAUSE,0);assert(!s_act.paused);
    s_act.valid=false;accepted=1;send_cmd(CMD_PAUSE,0);assert(!s_act.paused);
}'''
        run_c(source)

    def test_nursery_checkin_counts_playback_only(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define EPW_NURSERY_PLAYING 2
#define EPW_NURSERY_PAUSED 3
static struct {bool valid,paused;uint32_t duration_s,elapsed_s;int16_t last_index;} s_act;
static uint32_t s_tick_ms,s_act_elapsed_ms,s_act_saved_ms,now_ms;
static int s_state=EPW_NURSERY_PLAYING,completed,saves;
static int64_t esp_timer_get_time(void){return (int64_t)now_ms*1000;}
static void checkin_complete(void){completed++;s_act.valid=false;}
static int nursery_current_index(void){return 2;}
static void nvs_save_active(void){saves++;}
'''
        source += function('nursery.c', 'void nursery_checkin_tick(void)')
        source += r'''int main(void){
    s_act.valid=true;s_act.duration_s=60;
    for(int i=0;i<4;i++){now_ms+=500;nursery_checkin_tick();}
    assert(s_act_elapsed_ms==2000);
    s_act.paused=true;now_ms+=1000;nursery_checkin_tick();assert(s_act_elapsed_ms==2000);
    s_act.paused=false;s_state=EPW_NURSERY_PAUSED;now_ms+=1000;nursery_checkin_tick();assert(s_act_elapsed_ms==2000);
    s_state=1;now_ms+=1000;nursery_checkin_tick();assert(s_act_elapsed_ms==2000);
    s_state=EPW_NURSERY_PLAYING;
    for(int i=0;i<116;i++){now_ms+=500;nursery_checkin_tick();}
    assert(completed==1 && !s_act.valid && saves>0);
    now_ms+=500;nursery_checkin_tick();assert(completed==1);
}'''
        run_c(source)

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
static int s_spk, write_result,s_play_volume=30;
static bool s_play_open = true;
static size_t written_bytes;
static int16_t *expected;static size_t offset;
static int audio_lock(void){return 1;}
static int xSemaphoreTake(int lock, int timeout){return 1;}
static void xSemaphoreGive(int lock){}
static int esp_codec_dev_write(int dev, void *pcm, int bytes){
 written_bytes+=(size_t)bytes;int16_t *stereo=pcm;
 for(int i=0;i<bytes/4;i++)if(expected){int16_t value=s_play_volume?expected[offset+i]:0;assert(stereo[2*i]==value && stereo[2*i+1]==value);}
 offset+=bytes/4;return write_result;
}
'''
        source += function('audio.c', 'int audio_playback_write(const void *pcm, size_t bytes)')
        source += r'''
int main(void){
    int16_t pcm[8]={0};
    write_result=ESP_CODEC_DEV_OK;
    assert(audio_playback_write(pcm,sizeof(pcm))==(int)sizeof(pcm));
    assert(written_bytes==sizeof(pcm)*2);
    int16_t large[300];for(int i=0;i<300;i++)large[i]=i*101-15000;
    expected=large;offset=0;written_bytes=0;
    assert(audio_playback_write(large,sizeof(large))==sizeof(large));
    assert(offset==300 && written_bytes==1200);
    s_play_volume=0;offset=0;written_bytes=0;
    assert(audio_playback_write(large,sizeof(large))==sizeof(large));
    assert(offset==300 && written_bytes==1200);expected=NULL;
    assert(audio_playback_write(pcm,1)==-1);
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

    def test_interrupt_stops_old_reply_and_uploads_preroll(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#define VOICE_MAX_PCM 1920
#define VOICE_MAX_OPUS 2048
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_AUDIO_ERR_OK 0
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
typedef struct {uint8_t *buffer;unsigned len;}esp_audio_enc_in_frame_t;
typedef struct {uint8_t *buffer;unsigned len,encoded_bytes;}esp_audio_enc_out_frame_t;
static atomic_bool s_drop_reply,s_reply_active,s_listening;
static void *s_enc=(void*)1,*s_ws=(void*)1;
static int stage,closed,aborted,frames,sent;
static int16_t pre[1920];
static void pet_set(const char *emotion,const char *text){assert(strcmp(emotion,"listening")==0);}
static void tts_close(void){assert(s_drop_reply);closed++;}
static bool send_json(const char *p){assert(closed && strstr(p,"abort"));aborted++;return stage!=1;}
static void send_listen_start(void){assert(aborted);s_listening=stage!=2;}
static bool session_current(void){return true;}
static const int16_t *voice_barge_preroll(size_t *n){*n=sizeof(pre);return pre;}
static void *heap_caps_malloc(size_t n,int caps){return stage==5?NULL:malloc(n);}
static int esp_opus_enc_get_frame_size(void *enc,int *n,int *m){*n=1920;*m=128;return 0;}
static int esp_opus_enc_process(void *enc,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out){
 assert(in->len==1920 && ((int16_t *)in->buffer)[0]==1234);frames++;out->encoded_bytes=8;return stage==3?-1:0;
}
static int esp_websocket_client_send_bin(void *w,const char *p,unsigned n,int wait){assert(s_listening);sent++;return stage==4?0:n;}
'''
        source += 'static void watch_control_finish(bool apply){}\n'
        source += function('voice_client.c', 'static bool tts_interrupt(bool with_preroll)')
        source += r'''
int main(void){
 for(int i=0;i<1920;i++)pre[i]=1234;
 for(stage=0;stage<=5;stage++){
  closed=aborted=frames=sent=0;s_reply_active=true;s_listening=false;s_drop_reply=false;
  bool ok=tts_interrupt(true);assert(ok==(stage==0));assert(closed==1 && aborted==1 && s_drop_reply && !s_reply_active);
  if(!stage)assert(frames==2 && sent==2 && s_listening);
 }
 stage=0;closed=aborted=frames=sent=0;s_reply_active=true;s_listening=false;
 assert(tts_interrupt(false));assert(closed==1 && aborted==1 && frames==0 && sent==0 && s_listening);
 return 0;
}
'''
        run_c(source)

    def test_barge_frontend_warmup_echo_gate_preroll_and_cleanup(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define VOICE_BARGE_SAMPLES 512
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define AEC_MODE_FD_LOW_COST 5
#define AEC_NLP_LEVEL_AGGR 1
#define VAD_MODE_3 3
#define VAD_SPEECH 1
typedef int aec_handle_t;typedef int *vad_handle_t;typedef int vad_state_t;
typedef struct {int mic_num,ref_num,out_num,filter_length,sample_rate,caps,mode,nlp_level;}aec_config_t;
static int aec,vad,fail,created,destroyed;static bool speech;
static void *heap_caps_aligned_alloc(int alignment,size_t n,int caps){return fail==1?NULL:calloc(1,n);}
static aec_handle_t *aec_create_from_config(aec_config_t *c){assert(c->mode==5 && c->ref_num==1);created++;return fail==2?NULL:&aec;}
static int aec_get_chunksize(aec_handle_t *a){return fail==4?256:512;}
static vad_handle_t vad_create_with_param(int m,int rate,int ms,int on,int off){assert(ms==30 && on==90);return fail==3?NULL:&vad;}
static void aec_destroy(aec_handle_t *a){destroyed++;}
static void vad_destroy(vad_handle_t a){}
static void vad_reset_trigger(vad_handle_t a){}
static int vad_process_with_trigger(vad_handle_t a,int16_t *p){return speech;}
static void aec_process(aec_handle_t *a,int16_t *mic,int16_t *ref,int16_t *out){
 assert((uintptr_t)mic%16==0 && (uintptr_t)ref%16==0 && (uintptr_t)out%16==0);
 for(int i=0;i<512;i++)out[i]=mic[i]-ref[i];
}
'''
        real = (MAIN / 'voice_barge.c').read_text()
        source += real[real.index('typedef struct {'):]
        source += r'''
int main(void){
 int16_t mic[1024],ref[512];
 for(int i=0;i<512;i++){ref[i]=1000;mic[2*i]=mic[2*i+1]=2000;}
 assert(voice_barge_begin());speech=true;
 assert(!voice_barge_feed(mic,ref,128));
 assert(!voice_barge_feed(mic+256,ref+128,384));
 for(int i=0;i<20;i++)assert(!voice_barge_feed(mic,ref,512));
 speech=false;for(int i=0;i<1024;i++)mic[i]=1000;
 for(int i=0;i<3;i++)assert(!voice_barge_feed(mic,ref,512));
 assert(s_pcm->armed);
 speech=true;for(int i=0;i<1024;i++)mic[i]=2000;
 for(int i=0;i<4;i++)assert(voice_barge_feed(mic,ref,512));
 size_t bytes;const int16_t *pre=voice_barge_preroll(&bytes);assert(bytes==3840);
 for(int i=0;i<1920;i++)assert(pre[i]==1000);
 for(int i=0;i<1024;i++)mic[i]=3000;
 assert(!voice_barge_feed(mic,ref,31));pre=voice_barge_preroll(&bytes);
 assert(pre[1888]==1000 && pre[1889]==2000 && pre[1919]==2000 && !s_pcm->used);
 assert(voice_barge_begin() && created==1 && !s_pcm->armed);
 for(int i=0;i<1024;i++)mic[i]=1000;
 for(int i=0;i<20;i++)assert(!voice_barge_feed(mic,ref,512));
 speech=false;for(int i=0;i<1024;i++)mic[i]=2000;
 for(int i=0;i<20;i++)assert(!voice_barge_feed(mic,ref,512));
 voice_barge_close();assert(!voice_barge_preroll(&bytes) && bytes==0);
 assert(!voice_barge_feed(mic,ref,512));
 for(fail=1;fail<=4;fail++){
  assert(!voice_barge_begin());assert(!s_pcm && !s_aec && !s_vad);
 }
 return 0;
}
'''
        run_c(source)

    def test_tts_playback_drains_before_listening_and_handles_failure(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#define VOICE_BARGE_SAMPLES 512
#define ESP_CODEC_DEV_OK 0
#define ESP_LOGW(...) ((void)0)
#define VOICE_MAX_PCM 1920
#define VOICE_MAX_OPUS 2048
#define VOICE_PREBUFFER_MIN 6
#define VOICE_PREBUFFER_MAX 16
#define VOICE_PREBUFFER_STEP 4
#define VOICE_PREBUFFER_FIRST 3
#define VOICE_SAMPLE_RATE 16000
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_AUDIO_ERR_OK 0
#define ESP_OPUS_DEC_FRAME_DURATION_120_MS 8
#define ESP_OPUS_DEC_CONFIG_DEFAULT() {0}
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
typedef struct {size_t bytes;uint8_t data[];} voice_packet_t;
typedef struct {int sample_rate,channel,frame_duration;}esp_opus_dec_cfg_t;
typedef struct {uint8_t *buffer;size_t len,consumed;}esp_audio_dec_in_raw_t;
typedef struct {uint8_t *buffer;size_t len,decoded_size;}esp_audio_dec_out_frame_t;
typedef int esp_audio_dec_info_t;
static void *s_tts_queue=(void*)1,*s_dec;
static uint8_t *s_tts_pcm;
static uint32_t s_tts_written_samples;
static void captions_clear(void){s_tts_written_samples=0;}
static void caption_pump(void){}
#define CONFIG_EPW_VOICE_BARGE_IN 0
static bool s_tts_output,active=true,s_barge_ready,s_tts_buffered;
static atomic_bool s_first_reply;
static atomic_uint s_prebuffer_target=VOICE_PREBUFFER_MIN;
static void *s_enc=(void*)1;static int s_mic;
static bool session_current(void){return active;}
static bool open_audio(void){return true;}
static int audio_get_playback_volume(void){return 0;}
static bool voice_barge_begin(void){return true;}
static int esp_codec_dev_read(int dev,void *p,size_t n){memset(p,0,n);return 0;}
static bool voice_barge_feed(void *m,const void *r,size_t n){
 const int16_t *ref=r;for(size_t i=0;i<n;i++)assert(ref[i]==0);return false;
}
static bool tts_interrupt(bool with_preroll){return true;}
static atomic_bool s_tts_end,s_reply_active;
static voice_packet_t *queue[64];static int queued,writes,listens,closed,dec_closed,mic_closed,failure;
static unsigned uxQueueMessagesWaiting(void *q){return queued;}
static void *heap_caps_malloc(size_t n,int caps){return malloc(n);}
static int xQueueSend(void *q,voice_packet_t **p,int wait){if(queued==64)return 0;queue[queued++]=*p;return 1;}
static int xQueueReceive(void *q,voice_packet_t **p,int wait){if(!queued)return 0;*p=queue[0];memmove(queue,queue+1,--queued*sizeof(*queue));return 1;}
static void close_audio(void){mic_closed++;}
static void audio_playback_close(void){closed++;}
static int audio_playback_open(void){return failure!=2;}
static int esp_opus_dec_open(void *cfg,size_t n,void **d){if(failure==1)return -1;*d=(void*)1;return 0;}
static int esp_opus_dec_close(void *d){dec_closed++;return 0;}
static int esp_opus_dec_decode(void *d,esp_audio_dec_in_raw_t *raw,esp_audio_dec_out_frame_t *frame,esp_audio_dec_info_t *info){raw->consumed=raw->len;frame->decoded_size=1920;return failure==3?-1:0;}
static int audio_playback_write(void *pcm,size_t n){assert(n<=1024 && n>0);writes++;return failure==4?-1:n;}
static void pet_set(const char *a,const char *b){}
static bool voice_client_set_active(bool a){active=a;return a;}
static void vTaskDelay(int n){}
static void send_listen_start(void){assert(!s_tts_output && !queued);listens++;}
'''
        source += 'static bool s_control_complete,control_pending; static bool watch_control_pending(void){return control_pending;}\n'
        for signature in ('static bool tts_enqueue(const void *data, size_t bytes)',
                          'static void tts_close(void)', 'static bool tts_pump(void)'):
            source += function('voice_client.c', signature)
        source += r'''
int main(void){
 char packet[5]={0};assert(!tts_pump());
 assert(!tts_enqueue(packet,0) && !tts_enqueue(packet,2049));
 assert(tts_enqueue(packet,5) && tts_enqueue(packet,5));s_tts_end=true;s_reply_active=true;
 assert(tts_pump() && writes==2 && listens==0 && s_tts_written_samples==960);
 assert(tts_pump() && writes==4 && listens==0);
 assert(tts_pump() && listens==1 && !s_reply_active && closed==1 && dec_closed==1 && mic_closed==0);
 for(int i=0;i<64;i++)assert(tts_enqueue(packet,5));assert(!tts_enqueue(packet,5));
 tts_close();assert(!queued && writes==4);
 s_first_reply=false;s_prebuffer_target=VOICE_PREBUFFER_MIN;s_tts_end=false;
 assert(tts_enqueue(packet,5));assert(!tts_pump() && queued==1 && writes==4);
 for(int i=1;i<VOICE_PREBUFFER_MIN;i++)assert(tts_enqueue(packet,5));
 assert(tts_pump() && queued==VOICE_PREBUFFER_MIN-1 && writes==6);
 for(int i=0;i<VOICE_PREBUFFER_MIN-1;i++)assert(tts_pump());
 assert(!queued);assert(tts_pump());
 int before=writes;
 assert(tts_enqueue(packet,5));assert(tts_pump() && writes==before && s_prebuffer_target==VOICE_PREBUFFER_MIN+VOICE_PREBUFFER_STEP);
 s_tts_end=true;assert(tts_pump() && writes==before+2);assert(tts_pump());
 tts_close();assert(!s_tts_buffered && !s_first_reply);
 control_pending=true;active=true;s_tts_end=true;int old_listens=listens;
 assert(tts_pump() && s_control_complete && !active && listens==old_listens);
 control_pending=false;
 for(failure=1;failure<=4;failure++){
  active=true;s_tts_end=true;assert(tts_enqueue(packet,5));tts_pump();assert(!active);tts_close();
  assert(!queued && !s_dec && !s_tts_pcm && !s_tts_output);
 }
 return 0;
}
'''
        run_c(source)

    def test_tts_binary_fragments_are_queued_once(self):
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
static atomic_bool s_connected,s_hello,s_listening,s_reply_active,s_have_reply,s_drop_reply;
static atomic_uint s_progress_tick;
static unsigned s_tts_packets,s_tts_bytes,texts,queued;
static bool tts_enqueue(const void *p,size_t n){assert(n==5);queued++;return true;}
static bool voice_client_set_active(bool active){return active;}
static char s_session_id[96];
static epw_ws_message_t message,*s_message=&message;
static bool current=true;
static bool session_current(void){return current;}
static void pet_set(const char *a,const char *b){}
static bool send_json(const char *text){return true;}
static unsigned xTaskGetTickCount(void){return 123;}
static void handle_text(const char *data,size_t len){texts++;}
'''
        source += 'static void watch_control_new_connection(void){}\n'
        source += function('voice_client.c', 'static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)')
        source += r'''
int main(void){
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_CONNECTED,NULL);s_hello=true;
    esp_websocket_event_data_t part={.data_len=2,.payload_len=5,.op_code=2,.fin=true,.data_ptr="ab"};
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);assert(s_tts_packets==0 && queued==0);
    part.payload_offset=2;part.data_len=3;part.data_ptr="cde";
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);
    assert(s_tts_packets==1 && s_tts_bytes==5 && queued==1 && texts==0 && s_progress_tick==123);
    part=(esp_websocket_event_data_t){.data_len=2,.payload_len=2,.op_code=1,.fin=true,.data_ptr="{}"};
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);assert(texts==1 && s_tts_packets==1);
    s_drop_reply=true;
    part=(esp_websocket_event_data_t){.data_len=5,.payload_len=5,.op_code=2,.fin=true,.data_ptr="abcde"};
    ws_event_handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&part);assert(queued==1 && s_tts_packets==1);
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

    def test_nine_key_multitap_modes_cursor_and_length_limit(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int lv_event_t;typedef int lv_obj_t;
enum {KP_LC,KP_UC,KP_NUM,KP_SYMBOL};
static lv_obj_t obj,*s_wifipwd_kb=&obj,*s_wifipwd_ta=&obj;
static lv_obj_t *s_wifipwd_key_labels[14],*s_wifipwd_key_letters[9];
static int s_wifipwd_kb_mode;
static uint32_t s_wifipwd_tap_key=UINT32_MAX,s_wifipwd_tap_ms,now,selected;
static unsigned s_wifipwd_tap_index;
static int32_t s_wifipwd_tap_cursor,cursor;
static bool s_wifipwd_connecting;static int connects;
static char text[65];
static uint32_t lv_tick_get(void){return now;}
static void *lv_event_get_user_data(void *e){return (void*)(uintptr_t)selected;}
static void lv_label_set_text(void *o,const char *t){}
static void lv_obj_set_y(void *o,int y){}
static int32_t lv_textarea_get_cursor_pos(void *o){return cursor;}
static void lv_textarea_delete_char(void *o){if(cursor>0){memmove(text+cursor-1,text+cursor,strlen(text)-cursor+1);cursor--;}}
static void lv_textarea_add_text(void *o,const char *s){
 if(strlen(text)==64)return;assert(strlen(s)==1);memmove(text+cursor+1,text+cursor,strlen(text)-cursor+1);text[cursor++]=*s;
}
static void wifi_pwd_do_connect(void){connects++;}
'''
        ui = (MAIN / 'ui.c').read_text()
        source += ui[ui.index('static const char *const kp_map_lc[]'):ui.index('static void wifi_pwd_kb_apply(void)\n{')]
        source += function('ui.c', 'static void wifi_pwd_kb_apply(void)')
        source += function('ui.c', 'static void wifi_pwd_keypad_cb(lv_event_t *e)')
        source += r'''
static void key(const char *label){
 for(selected=0;selected<14;selected++)
  if(!strcmp(label,wifi_pwd_key_map()[selected]))break;
 assert(selected<14);now+=100;wifi_pwd_keypad_cb(0);
}
static void clear(void){text[0]=0;cursor=0;s_wifipwd_kb_mode=KP_LC;wifi_pwd_kb_apply();}
int main(void){
 clear();key("abc");key("abc");key("abc");assert(!strcmp(text,"c"));
 now+=800;key("abc");assert(!strcmp(text,"ca"));key("def");assert(!strcmp(text,"cad"));
 key("Aa");key("ABC");key("ABC");assert(!strcmp(text,"cadB"));
 key("1#");key("2");key("2");assert(!strcmp(text,"cadB22"));
 key("2#");key("'\"`");key("'\"`");assert(text[6]=='"');
 key("space");key("Del");assert(strlen(text)==7);key("OK");assert(connects==1);
 key("a-z");cursor=0;key("abc");key("abc");assert(!strncmp(text,"bcadB22",7));
 s_wifipwd_connecting=true;key("Del");assert(text[0]=='b');s_wifipwd_connecting=false;
 clear();now=UINT32_MAX-150;key("abc");key("abc");assert(!strcmp(text,"b"));
 clear();memset(text,'x',64);text[64]=0;cursor=64;key("abc");key("abc");assert(strlen(text)==64 && text[63]=='x');
 key("Del");key("abc");key("abc");assert(strlen(text)==64 && text[63]=='b');
 clear();key("pqrs");key("pqrs");key("pqrs");key("pqrs");key("pqrs");assert(!strcmp(text,"p"));
 selected=UINT32_MAX;wifi_pwd_keypad_cb(0);assert(!strcmp(text,"p"));
 return 0;
}
'''
        run_c(source)

    def test_keyboard_all_printable_ascii(self):
        source = (MAIN / 'ui.c').read_text()
        characters = {' '}
        for match in re.finditer(r'kp_map_(\w+)\[\] = \{(.*?)\n\};', source, re.S):
            tokens = [ast.literal_eval('"' + v + '"') for v in re.findall(r'"((?:[^"\\]|\\.)*)"', match[2])]
            characters |= set(''.join(v for v in tokens if v not in ('', '\n', 'Aa', 'a-z', '1#', '2#', 'space', 'Del', 'OK')))
            self.assertEqual(len(tokens), 14)
        self.assertEqual(set(map(chr, range(32,127))) - characters, set())


    def _tts_pump_source(self, extra):
        source = TTS_PUMP_HARNESS
        for signature in ('static bool tts_enqueue(const void *data, size_t bytes)',
                          'static void tts_close(void)',
                          'static bool tts_pump(void)'):
            source += function('voice_client.c', signature)
        return source + extra

    def test_prebuffer_adaptive_raises_on_underrun_and_caps(self):
        voice = (MAIN / 'voice_client.c').read_text()
        for name, value in (('VOICE_PREBUFFER_MIN', 6), ('VOICE_PREBUFFER_MAX', 16),
                            ('VOICE_PREBUFFER_STEP', 4), ('VOICE_PREBUFFER_FIRST', 3)):
            self.assertEqual(int(re.search(r'#define %s\s+(\d+)' % name, voice)[1]), value)
        self.assertNotIn('VOICE_PREBUFFER_PACKETS', voice)
        source = self._tts_pump_source(r'''
int main(void){
 char packet[5]={0};
 int t1=VOICE_PREBUFFER_MIN,t2=t1+VOICE_PREBUFFER_STEP,t3=t2+VOICE_PREBUFFER_STEP,t4=VOICE_PREBUFFER_MAX;
 int before;
 /* 首轮按最小目标 6 包起播 */
 for(int i=0;i<t1-1;i++)assert(tts_enqueue(packet,5));
 assert(!tts_pump() && queued==t1-1 && writes==0);
 assert(tts_enqueue(packet,5));
 assert(tts_pump() && queued==t1-1 && writes==2);
 for(int i=0;i<t1-1;i++)assert(tts_pump());
 assert(!queued);
 assert(tts_pump() && s_prebuffer_target==t2);
 /* 欠载后升到 10：9 包仍不起播 */
 before=writes;
 for(int i=0;i<t2-1;i++)assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before);
 assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before+2);
 for(int i=0;i<t2-1;i++)assert(tts_pump());
 assert(!queued);
 assert(tts_pump() && s_prebuffer_target==t3);
 /* 14+4 越界，封顶 16 */
 before=writes;
 for(int i=0;i<t3-1;i++)assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before);
 assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before+2);
 for(int i=0;i<t3-1;i++)assert(tts_pump());
 assert(!queued);
 assert(tts_pump() && s_prebuffer_target==t4);
 /* 16 包封顶，欠载不再升档 */
 before=writes;
 for(int i=0;i<t4-1;i++)assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before);
 assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before+2);
 for(int i=0;i<t4-1;i++)assert(tts_pump());
 assert(!queued);
 assert(tts_pump() && s_prebuffer_target==t4);
 /* 结束标记即使只剩 1 包也起播并恢复倾听 */
 before=writes;
 assert(tts_enqueue(packet,5));s_tts_end=true;
 assert(tts_pump() && writes==before+2 && listens==0);
 assert(tts_pump() && listens==1 && !s_reply_active && !s_tts_output);
 return 0;
}
''')
        run_c(source)

    def test_short_reply_starts_on_end(self):
        source = self._tts_pump_source(r'''
int main(void){
 char packet[5]={0};
 s_first_reply=true;
 assert(tts_enqueue(packet,5) && tts_enqueue(packet,5));
 assert(!tts_pump() && writes==0 && queued==2);
 s_tts_end=true;
 assert(tts_pump() && writes==2 && !s_first_reply);
 return 0;
}
''')
        run_c(source)

    def test_prebuffer_target_resets_on_new_session(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#define VOICE_PREBUFFER_MIN 6
static atomic_bool s_requested,s_ready;
static atomic_uint s_epoch;
static atomic_uint s_prebuffer_target=16;
'''
        source += function('voice_client.c', 'bool voice_client_set_active(bool active)')
        source += r'''
int main(void){
 s_ready=true;s_requested=true;s_prebuffer_target=16;
 assert(voice_client_set_active(true) && s_prebuffer_target==16);
 assert(!voice_client_set_active(false) && s_prebuffer_target==16);
 assert(voice_client_set_active(true) && s_prebuffer_target==VOICE_PREBUFFER_MIN && s_epoch==2);
 s_ready=false;assert(!voice_client_set_active(true));
 return 0;
}
'''
        run_c(source)

    def test_first_reply_starts_after_three_packets(self):
        source = self._tts_pump_source(r'''
int main(void){
 char packet[5]={0};
 s_first_reply=true;
 assert(tts_enqueue(packet,5) && tts_enqueue(packet,5));
 assert(!tts_pump() && writes==0 && queued==2);
 assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==2 && queued==2 && !s_first_reply);
 assert(tts_pump() && tts_pump() && !queued);
 /* 首句快起只作用于起播；队列见底后仍按自适应目标加长 */
 assert(tts_pump() && s_prebuffer_target==VOICE_PREBUFFER_MIN+VOICE_PREBUFFER_STEP);
 tts_close();
 s_first_reply=true;s_prebuffer_target=VOICE_PREBUFFER_MIN;
 int before=writes;
 assert(tts_enqueue(packet,5) && tts_enqueue(packet,5));
 assert(!tts_pump() && writes==before);
 assert(tts_enqueue(packet,5));
 assert(tts_pump() && writes==before+2);
 return 0;
}
''')
        run_c(source)

    def test_early_start_keeps_caption_in_sync(self):
        source = r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ESP_CODEC_DEV_OK 0
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define VOICE_BARGE_SAMPLES 512
#define VOICE_MAX_PCM 1920
#define VOICE_MAX_OPUS 2048
#define VOICE_SAMPLE_RATE 16000
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_AUDIO_ERR_OK 0
#define ESP_OPUS_DEC_FRAME_DURATION_120_MS 8
#define ESP_OPUS_DEC_CONFIG_DEFAULT() {0}
#define CONFIG_EPW_VOICE_BARGE_IN 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define VOICE_PREBUFFER_MIN 6
#define VOICE_PREBUFFER_MAX 16
#define VOICE_PREBUFFER_STEP 4
#define VOICE_PREBUFFER_FIRST 3
typedef struct {size_t bytes;uint8_t data[];} voice_packet_t;
typedef struct {uint32_t at_samples;char emotion[32];char text[];}voice_caption_t;
typedef struct {int sample_rate,channel,frame_duration;}esp_opus_dec_cfg_t;
typedef struct {uint8_t *buffer;size_t len,consumed;}esp_audio_dec_in_raw_t;
typedef struct {uint8_t *buffer;size_t len,decoded_size;}esp_audio_dec_out_frame_t;
typedef int esp_audio_dec_info_t;
static void *s_tts_queue=(void*)1,*s_caption_queue=(void*)2,*s_dec;
static uint8_t *s_tts_pcm;
static uint32_t s_tts_written_samples,s_caption_received_ms;
static bool s_tts_output,s_tts_buffered,s_barge_ready,active=true;
static atomic_bool s_first_reply;
static atomic_uint s_prebuffer_target=VOICE_PREBUFFER_MIN;
static void *s_enc=(void*)1;static int s_mic;
static atomic_bool s_tts_end,s_reply_active;
static char s_reply_emotion[32],shown[64];static int updates;
static voice_packet_t *queue[64];static int queued,writes,closed,dec_closed;
static voice_caption_t *cq[8];static int cq_count;
static bool session_current(void){return active;}
static bool open_audio(void){return true;}
static int audio_get_playback_volume(void){return 0;}
static bool voice_barge_begin(void){return true;}
static int esp_codec_dev_read(int dev,void *p,size_t n){memset(p,0,n);return 0;}
static bool voice_barge_feed(void *m,const void *r,size_t n){return false;}
static unsigned uxQueueMessagesWaiting(void *q){return queued;}
static void *heap_caps_malloc(size_t n,int caps){return malloc(n);}
static int xQueueSend(void *q,void *p,int wait){
 if(q==s_tts_queue){if(queued==64)return 0;queue[queued++]=*(voice_packet_t**)p;return 1;}
 if(cq_count==8)return 0;cq[cq_count++]=*(voice_caption_t**)p;return 1;
}
static int xQueuePeek(void *q,void *p,int wait){
 if(!cq_count)return 0;*(voice_caption_t**)p=cq[0];return 1;
}
static int xQueueReceive(void *q,void *p,int wait){
 if(q==s_tts_queue){if(!queued)return 0;*(voice_packet_t**)p=queue[0];memmove(queue,queue+1,--queued*sizeof(*queue));return 1;}
 if(!cq_count)return 0;*(voice_caption_t**)p=cq[0];memmove(cq,cq+1,--cq_count*sizeof(*cq));return 1;
}
static void pet_post(const char *e,const char *t,bool spoken){assert(spoken);updates++;snprintf(shown,sizeof(shown),"%s",t);}
static void audio_playback_close(void){closed++;}
static int audio_playback_open(void){return 1;}
static int esp_opus_dec_open(void *cfg,size_t n,void **d){*d=(void*)1;return 0;}
static int esp_opus_dec_close(void *d){dec_closed++;return 0;}
static int esp_opus_dec_decode(void *d,esp_audio_dec_in_raw_t *raw,esp_audio_dec_out_frame_t *frame,esp_audio_dec_info_t *info){raw->consumed=raw->len;frame->decoded_size=1920;return 0;}
static int audio_playback_write(void *pcm,size_t n){assert(n<=1024 && n>0);writes++;return n;}
static void pet_set(const char *a,const char *b){}
static bool voice_client_set_active(bool a){active=a;return a;}
static void vTaskDelay(int n){}
static void send_listen_start(void){}
static bool watch_control_pending(void){return false;}
static bool s_control_complete;
static bool tts_interrupt(bool with_preroll){return true;}
'''
        for signature in ('static bool caption_enqueue(uint32_t at_ms, const char *text)',
                          'static void caption_pump(void)',
                          'static void captions_clear(void)',
                          'static bool tts_enqueue(const void *data, size_t bytes)',
                          'static void tts_close(void)',
                          'static bool tts_pump(void)'):
            source += function('voice_client.c', signature)
        source += r'''
int main(void){
 char packet[5]={0};
 s_first_reply=true;
 assert(caption_enqueue(120,"Hi"));
 assert(tts_enqueue(packet,5) && tts_enqueue(packet,5) && tts_enqueue(packet,5));
 /* 首句虽然 3 包就起播，字幕仍要等对应 PCM 真正写入 */
 assert(tts_pump() && writes==2 && updates==0 && s_tts_written_samples==960);
 assert(tts_pump() && writes==4 && updates==1 && !strcmp(shown,"Hi") && s_tts_written_samples==1920);
 return 0;
}
'''
        run_c(source)

    def test_listening_interrupt_branch_aborts_and_relistens(self):
        voice = (MAIN / 'voice_client.c').read_text()
        start = voice.index('if (atomic_exchange(&s_interrupt_requested, false)')
        branch = voice[start:voice.index('continue;', start)]
        self.assertIn('s_listening', branch)
        self.assertIn('s_reply_active', branch)
        self.assertIn('tts_interrupt(false)', branch)

    def test_pet_stop_overlay_is_transparent_gesture_passthrough_and_interrupts(self):
        ui = (MAIN / 'ui.c').read_text()
        self.assertIn('static lv_obj_t *s_pet_stop;', ui)
        build = function('ui.c', 'static void build_pet(void)')
        layer = build.index('s_pet_stop = lv_obj_create(s_pet)')
        self.assertGreater(layer, build.index('s_pet_img = lv_image_create(s_pet)'))
        self.assertIn('lv_obj_remove_style_all(s_pet_stop)', build)
        self.assertIn('lv_obj_add_flag(s_pet_stop, LV_OBJ_FLAG_CLICKABLE)', build)
        self.assertIn('lv_obj_add_flag(s_pet_stop, LV_OBJ_FLAG_GESTURE_BUBBLE)', build)
        self.assertIn('lv_obj_add_flag(s_pet_stop, LV_OBJ_FLAG_HIDDEN)', build)
        self.assertIn('lv_obj_add_event_cb(s_pet_stop, pet_stop_cb, LV_EVENT_CLICKED, NULL)', build)
        timer = function('ui.c', 'static void pet_anim_timer_cb(lv_timer_t *timer)')
        self.assertIn('voice_client_can_interrupt()', timer)
        self.assertIn('s_pet_stop', timer)
        # 透明层冒泡到 s_pet：左右滑退出/聊天，上下滑在可打断阶段沿用猫咪语义。
        page = function('ui.c', 'static void pet_page_gesture_cb(lv_event_t *e)')
        self.assertIn('voice_client_can_interrupt()', page)
        self.assertIn('pet_area_gesture_cb(e)', page)
        area = function('ui.c', 'static void pet_area_gesture_cb(lv_event_t *e)')
        self.assertIn('LV_DIR_BOTTOM', area)
        self.assertIn('voice_client_set_active(false)', area)


    def test_launcher_nine_function_entries(self):
        source = r'''#include <assert.h>
#include <stdbool.h>
enum {APP_PET,APP_NURSERY,APP_STATUS,APP_WIFI,APP_SETTINGS,APP_TIMER,APP_BRIGHT,APP_VOLUME,APP_TORCH};
#define SLIDER_BRIGHT 0
#define SLIDER_VOLUME 1
#define UI_PET_HAPPY 1
static int opened=-1;static bool busy;
static bool page_anim_busy(void){return busy;}
static void ui_pet_set_state(int state,void *text){opened=APP_PET;}
static void nursery_open(void){opened=APP_NURSERY;}
static void status_open(void){opened=APP_STATUS;}
static void wifi_open(void){opened=APP_WIFI;}
static void tools_open(int kind){opened=kind?APP_TIMER:APP_SETTINGS;}
static void slider_open(int kind){opened=kind?APP_VOLUME:APP_BRIGHT;}
static void torch_open(void){opened=APP_TORCH;}
#define NULL ((void *)0)
'''
        source += function('ui.c', 'static void app_open_index(int idx)')
        source += r'''
int main(void){
    for(int i=0;i<9;i++){app_open_index(i);assert(opened==i);}
    opened=-1;app_open_index(9);assert(opened==-1);
    busy=true;app_open_index(0);assert(opened==-1);return 0;
}
'''
        run_c(source)


if __name__ == '__main__':
    unittest.main(verbosity=2)
