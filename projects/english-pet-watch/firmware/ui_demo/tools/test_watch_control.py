#!/usr/bin/env python3
"""编译真实 MCP 控制实现，模拟设备接口；不连接设备、不出声。"""
import unittest
from test_firmware import MAIN, run_c

STUBS = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "nursery.h"
#define ESP_OK 0
#define pdTRUE 1
#define ESP_LOGI(...) ((void)0)
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef int lv_result_t;
#define LV_RESULT_OK 0
#define portMAX_DELAY -1
static int done;
static void *xSemaphoreCreateBinary(void){return (void*)1;}
static void xSemaphoreGive(void *s){done++;}
static int xSemaphoreTake(void *s,int timeout){assert(done==1);done--;return 1;}
static int lv_async_call(void (*cb)(void*),void *data){cb(data);return LV_RESULT_OK;}

typedef enum {WATCH_TIMER_IDLE,WATCH_TIMER_RUNNING,WATCH_TIMER_PAUSED,WATCH_TIMER_FINISHED} watch_timer_state_t;
static cJSON *queue[6]; static int queued,volume=40,brightness=80,played=-1,current=-1,opened,power_menu,saves;
static bool lock_ok=true,send_ok=true,ready=true,locked_brightness;
static char page[32],screen[32]; static unsigned idle=30000,remaining;
static watch_timer_state_t timer_state;
static epw_nursery_state_t music_state;
static epw_nursery_mode_t mode;
static epw_nursery_song_t songs[]={
 {"001","1. Five Little Monkeys","one.mp3",100},
 {"002","2. The Wheels on the Bus","two.mp3",120},
 {"003","3. Five Little Ducks","three.mp3",90}};
static void *xQueueCreate(int n,size_t size){assert(n==6);return (void*)1;}
static int xQueueSend(void *q,cJSON **item,int wait){if(queued==6)return 0;queue[queued++]=*item;return 1;}
static int xQueueReceive(void *q,cJSON **item,int wait){if(!queued)return 0;*item=queue[0];memmove(queue,queue+1,--queued*sizeof(*queue));return 1;}
static int bsp_display_lock(int ms){return lock_ok?ESP_OK:-1;}
static void bsp_display_unlock(void){}
static void ui_activity(void){}
static int ui_watch_brightness(void){return brightness;}
static bool ui_watch_set_brightness(int v){if(locked_brightness)return false;brightness=v;return true;}
static bool ui_watch_open(const char *p){snprintf(page,sizeof(page),"%s",p);opened++;return true;}
static bool ui_watch_screen(const char *p){snprintf(screen,sizeof(screen),"%s",p);return true;}
static void ui_power_menu_open(void){power_menu++;}
static bool ui_watch_timer_start(unsigned sec){if(timer_state==WATCH_TIMER_RUNNING||timer_state==WATCH_TIMER_PAUSED)return false;remaining=sec;timer_state=WATCH_TIMER_RUNNING;return true;}
static watch_timer_state_t watch_timer_state(void){return timer_state;}
static uint32_t watch_timer_remaining(void){return remaining;}
static void watch_timer_start(void){timer_state=WATCH_TIMER_RUNNING;}
static void watch_timer_pause(void){timer_state=WATCH_TIMER_PAUSED;}
static void watch_timer_reset(void){remaining=0;timer_state=WATCH_TIMER_IDLE;}
static uint32_t watch_idle_timeout_ms(void){return idle;}
static int watch_set_idle_timeout_ms(uint32_t v){if(v!=0&&v!=15000&&v!=30000&&v!=60000&&v!=120000&&v!=300000)return -1;idle=v;return 0;}
static int power_battery_percent(void){return 85;}
static bool power_is_charging(void){return false;}
static const char *wifi_get_status_text(void){return "ONLINE";}
bool nursery_catalog_ready(void){return ready;}
int nursery_song_count(void){return 3;}
const epw_nursery_song_t *nursery_song(int i){return i>=0&&i<3?&songs[i]:NULL;}
int nursery_current_index(void){return current;}
int nursery_get_volume(void){return volume;}
void nursery_set_volume(int v){volume=v;}
void nursery_save_volume(void){saves++;}
epw_nursery_state_t nursery_get_state(void){return music_state;}
const char *nursery_status_text(void){return "";}
int nursery_weekly_count(void){return 3;}
bool nursery_queue_is_weekly(void){return false;}
epw_nursery_mode_t nursery_get_mode(void){return mode;}
int nursery_weekly_index(int slot){return slot<3?slot:-1;}
void nursery_play_index(int i){played=i;current=i;music_state=EPW_NURSERY_LOADING;}
void nursery_play_weekly(int i){nursery_play_index(i);}
void nursery_pause(void){music_state=EPW_NURSERY_PAUSED;}
void nursery_stop(void){music_state=EPW_NURSERY_IDLE;}
void nursery_resume(void){music_state=EPW_NURSERY_PLAYING;}
void nursery_next(void){current=(current+1)%3;}
void nursery_prev(void){current=(current+2)%3;}
void nursery_set_mode(epw_nursery_mode_t m){mode=m;}
int nursery_checkin_week_stars(void){return 4;}
int nursery_checkin_today_done(void){return 1;}
bool nursery_checkin_wish_ready(void){return false;}
bool nursery_checkin_enabled(void){return true;}
void nursery_checkin_session(int slot,epw_nursery_session_t *s){memset(s,0,sizeof(*s));s->done=slot==0;}
bool nursery_checkin_start(int slot){return slot>0;}
void nursery_checkin_pause(void){}
void nursery_checkin_resume(void){}
void nursery_checkin_end(void){}
bool nursery_checkin_redeem_wish(void){return false;}
void watch_control_finish(bool apply);
static cJSON *response;
static bool send_reply(const char *raw){cJSON_Delete(response);response=cJSON_Parse(raw);assert(response);return send_ok;}
'''

CHECKS = r'''
static cJSON *request(const char *raw){
 cJSON *r=cJSON_Parse(raw);assert(r);assert(watch_control_receive(r));cJSON_Delete(r);
 watch_control_poll(send_reply);
 return cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(response,"payload"),"result");
}
static cJSON *call(int id,const char *tool,const char *args){
 char raw[1024];snprintf(raw,sizeof(raw),"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\",\"params\":{\"name\":\"self.watch.%s\",\"arguments\":%s}}",id,tool,args);
 return request(raw);
}
static bool error(cJSON *r){return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"isError"));}
static cJSON *data(cJSON *r){
 cJSON *c=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r,"content"),0);
 return cJSON_Parse(cJSON_GetObjectItemCaseSensitive(c,"text")->valuestring);
}
int main(void){
 watch_control_init();
 request("{\"id\":9,\"method\":\"tools/list\"}");
 assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(response,"payload"),"error"));
 request("{\"id\":1,\"method\":\"initialize\",\"params\":{\"private_token\":\"not-retained\"}}");
 cJSON *r=request("{\"id\":2,\"method\":\"tools/list\"}");
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r,"tools"))==5);
 assert(!strcmp(string(r,"nextCursor"),"5"));
 r=request("{\"id\":2,\"method\":\"tools/list\",\"params\":{\"cursor\":\"5\"}}");
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r,"tools"))==5 && !string(r,"nextCursor")[0]);
 assert(!error(call(3,"volume","{\"value\":0}")) && volume==0 && saves==1);
 assert(error(call(3,"volume","{\"delta\":10}")) && volume==0 && saves==1);
 assert(!error(call(4,"volume","{\"delta\":100}")) && volume==100);
 assert(!error(call(5,"volume","{\"delta\":10}")) && volume==100);
 assert(error(call(6,"volume","{\"value\":101}")) && volume==100);
 assert(error(call(7,"volume","{\"value\":5.5}")));
 assert(error(call(8,"volume","{\"value\":5,\"delta\":1}")));
 assert(error(call(9,"volume","{\"value\":5,\"value\":6}")));
 assert(error(call(10,"volume","{\"value\":true}")));
 assert(error(call(11,"volume","{\"delta\":-1,\"command\":\"shell\"}")));
 assert(error(call(12,"volume","[]")));
 assert(error(call(13,"volume","{}")));
 assert(error(call(14,"unknown","{}")));
 assert(!error(call(15,"brightness","{\"delta\":-100}")) && brightness==10);
 locked_brightness=true;assert(error(call(16,"brightness","{\"value\":50}")) && brightness==10);locked_brightness=false;
 lock_ok=false;assert(error(call(17,"volume","{\"value\":25}")) && volume==100);lock_ok=true;
 cJSON *d=data(call(18,"search_songs","{\"query\":\"FIVE LITTLE\"}"));
 assert(cJSON_GetObjectItemCaseSensitive(d,"total_matches")->valueint==2);cJSON_Delete(d);
 d=data(call(19,"search_songs","{\"query\":\"Wheels-on the Bus\"}"));
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(d,"songs"))==1);cJSON_Delete(d);
 assert(error(call(20,"search_songs","{\"query\":\"---\"}")));
 ready=false;assert(error(call(21,"search_songs","{\"query\":\"five\"}")));ready=true;
 assert(error(call(22,"music","{\"action\":\"play\",\"song_id\":\"999\"}")) && !watch_control_pending());
 assert(error(call(23,"music","{\"action\":\"resume\"}")));
 assert(!error(call(24,"music","{\"action\":\"play\",\"song_id\":\"001\"}")));
 assert(watch_control_pending() && played==-1 && opened==0);
 assert(error(call(25,"shutdown","{}")) && power_menu==0);
 watch_control_finish(true);assert(played==0 && opened==1 && !strcmp(page,"nursery") && !watch_control_pending());
 watch_control_remember_music(); current=-1;
 assert(!error(call(26,"music","{\"action\":\"next\"}")));
 watch_control_finish(true);assert(played==1);
 assert(!error(call(27,"shutdown","{}")) && power_menu==0);watch_control_finish(false);assert(power_menu==0);
 assert(!error(call(28,"shutdown","{}")));watch_control_finish(true);assert(power_menu==1);
 assert(!error(call(29,"open","{\"page\":\"wifi\"}")));watch_control_reset();assert(!watch_control_pending() && strcmp(page,"wifi"));
 request("{\"id\":1,\"method\":\"initialize\"}");
 assert(!error(call(3,"timer","{\"action\":\"start\",\"seconds\":60}")) && remaining==60);
 assert(error(call(4,"timer","{\"action\":\"start\",\"seconds\":20}")) && remaining==60);
 assert(!error(call(5,"timer","{\"action\":\"pause\"}")) && timer_state==WATCH_TIMER_PAUSED);
 assert(!error(call(6,"timer","{\"action\":\"resume\"}")) && timer_state==WATCH_TIMER_RUNNING);
 assert(!error(call(7,"timer","{\"action\":\"cancel\"}")) && remaining==0);
 assert(error(call(8,"timer","{\"action\":\"start\",\"seconds\":0}")));
 assert(error(call(9,"screen","{\"action\":\"idle\",\"seconds\":5}")) && idle==30000);
 assert(!error(call(10,"screen","{\"action\":\"idle\",\"seconds\":120}")) && idle==120000);
 assert(error(call(11,"checkin","{\"action\":\"start\",\"slot\":0}")));
 assert(error(call(12,"checkin","{\"action\":\"redeem\"}")));
 assert(!error(call(13,"checkin","{\"action\":\"start\",\"slot\":1}")));watch_control_finish(true);
 send_ok=false;call(14,"open","{\"page\":\"wifi\"}");assert(!watch_control_pending());send_ok=true;
 assert(error(call(15,"open","{\"page\":\"terminal\"}")));
 cJSON *bad=cJSON_Parse("{\"id\":1.2,\"method\":\"initialize\"}");assert(!watch_control_receive(bad));cJSON_Delete(bad);
 bad=cJSON_Parse("{\"id\":2,\"method\":\"initialize\"}");
 for(int i=0;i<6;i++)assert(watch_control_receive(bad));assert(!watch_control_receive(bad));
 cJSON_Delete(bad);watch_control_reset();assert(!queued);
 request("{\"id\":1,\"method\":\"initialize\"}");
 assert(!error(call(16,"volume","{\"value\":30}")));
 bad=cJSON_Parse("{\"id\":17,\"method\":\"tools/call\",\"params\":{\"name\":\"self.watch.volume\",\"arguments\":{\"value\":99}}}");
 assert(watch_control_receive(bad));cJSON_Delete(bad);watch_control_new_connection();
 watch_control_poll(send_reply);assert(volume==30);
 request("{\"id\":1,\"method\":\"initialize\"}");
 assert(!error(call(3,"volume","{\"value\":35}")) && volume==35);
 cJSON_Delete(response);
 return 0;
}
'''


class WatchControlTests(unittest.TestCase):
    def test_protocol_validation_device_actions_and_handoff(self):
        source = (MAIN / 'watch_control.c').read_text()
        run_c(STUBS + source[source.index('static const char *const s_tools[]'):]+CHECKS, json=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
