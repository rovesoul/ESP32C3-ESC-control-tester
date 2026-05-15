#include "esc_web.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "esc_pwm.h"
#include "simple_wifi_sta.h"

static const char *TAG = "esc_web";
static httpd_handle_t s_server = NULL;

static const char *ESC_HTML =
"<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>ESP32-C3 ESC 控制台</title>"
"<style>"
":root{--bg:#f5f7fb;--panel:#fff;--ink:#20242a;--muted:#687282;--line:#d8dee9;--blue:#2563eb;--green:#0f9f6e;--red:#d14d45;--amber:#bd7417}"
"*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;line-height:1.55}"
"header{background:#101827;color:#fff;padding:22px 18px}main{width:min(1040px,calc(100% - 28px));margin:22px auto 42px}.top{width:min(1040px,calc(100% - 28px));margin:0 auto}"
"h1{font-size:30px;margin:0 0 6px}h2{font-size:22px;margin:0 0 14px}.sub{color:#d9e1ee;margin:0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:18px;box-shadow:0 8px 22px rgba(18,25,38,.08)}"
"label{display:block;font-weight:700;margin:13px 0 7px}select,input{width:100%;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--ink);font-size:16px;padding:10px}input[type=range]{padding:0;accent-color:var(--blue)}"
".row{display:grid;grid-template-columns:1fr 130px;gap:10px;align-items:center}.metrics{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:14px}.metric{border:1px solid var(--line);border-radius:6px;padding:10px;background:#fbfcff}.metric b{display:block;color:var(--muted);font-size:12px}.metric span{font-size:20px;font-weight:800}"
".actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}button{border:0;border-radius:6px;padding:11px 14px;font-size:15px;font-weight:800;cursor:pointer}.primary{background:var(--blue);color:#fff}.enable{background:var(--green);color:#fff}.stop{background:var(--red);color:#fff}.ghost{background:#e9eef7;color:#273142}"
".status{margin-top:12px;padding:10px;border-radius:6px;background:#eef6ff;color:#1c4b82}.warn{background:#fff7e8;border-left:4px solid var(--amber);padding:12px;border-radius:0 6px 6px 0;color:#553810}.locked{color:var(--red)}.ok{color:var(--green)}"
"canvas{width:100%;height:260px;border:1px solid var(--line);border-radius:8px;background:#fff;display:block}.disabled{opacity:.55}.footer{color:var(--muted);font-size:13px;margin-top:16px}@media(max-width:760px){.grid{grid-template-columns:1fr}.row{grid-template-columns:1fr}.metrics{grid-template-columns:1fr}}"
"</style></head><body>"
"<header><div class='top'><h1>ESP32-C3 ESC 控制台</h1><p class='sub'>当前阶段实现 PWM 输出；DShot 选项先作为后续预留。</p></div></header>"
"<main><div class='grid'><section class='card'><h2>输出设置</h2>"
"<label for='protocol'>电调驱动形式</label><select id='protocol'><option value='pwm'>PWM / Servo PWM</option><option value='dshot150' disabled>DShot150（后续）</option><option value='dshot300' disabled>DShot300（后续）</option><option value='dshot600' disabled>DShot600（后续）</option><option value='dshot1200' disabled>DShot1200（后续）</option></select>"
"<label for='gpio'>输出 GPIO</label><select id='gpio'><option>0</option><option>1</option><option>2</option><option selected>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>10</option><option>20</option><option>21</option></select>"
"<label>频率 Hz（50-1000）</label><div class='row'><input id='freqRange' type='range' min='50' max='1000' value='50'><input id='freq' type='number' min='50' max='1000' value='50'></div>"
"<label>占空比 %（0-100）</label><div class='row'><input id='dutyRange' type='range' min='0' max='100' step='0.1' value='5'><input id='duty' type='number' min='0' max='100' step='0.1' value='5'></div>"
"<div class='actions'><button class='primary' id='save'>保存设置</button><button class='enable' id='enable'>解锁输出</button><button class='ghost' id='lock'>锁定输出</button><button class='stop' id='stop'>STOP</button></div><div id='status' class='status'>滑块会实时应用到 PWM，保存按钮只负责写入 NVS。</div></section>"
"<section class='card'><h2>实时状态</h2><canvas id='wave' width='900' height='300'></canvas><div class='metrics'><div class='metric'><b>设备 IP</b><span id='ip'>-</span></div><div class='metric'><b>输出状态</b><span id='enabled'>-</span></div><div class='metric'><b>周期</b><span id='period'>-</span></div><div class='metric'><b>高电平脉宽</b><span id='pulse'>-</span></div></div></section></div>"
"<section class='card' style='margin-top:16px'><h2>安全提醒</h2><p class='warn'>测试 ESC 前先拆桨。网页默认锁定输出，只有点击“解锁输出”后才会按当前频率/占空比输出 PWM。STOP 会立即锁定并输出 0% 占空比。</p><p class='footer'>XIAO ESP32-C3 上建议优先用 GPIO3/4，其次 GPIO5/6/7/10。GPIO8 当前用于呼吸灯且也是板载 LED/SDA，GPIO9 是 Boot 长按清配网且是 SCL，GPIO20/21 是串口 RX/TX。</p></section>"
"</main><script>"
"const $=id=>document.getElementById(id);const fields=['gpio','freq','freqRange','duty','dutyRange'];"
"function clamp(v,min,max){return Math.min(max,Math.max(min,Number(v)||0))}"
"function setStatus(t,bad=false){$('status').textContent=t;$('status').style.background=bad?'#fff0ef':'#eef6ff'}"
"function syncPair(a,b,min,max){$(a).addEventListener('input',()=>{$(b).value=clamp($(a).value,min,max);draw()});$(b).addEventListener('input',()=>{$(a).value=clamp($(b).value,min,max);draw()})}"
"syncPair('freqRange','freq',50,1000);syncPair('dutyRange','duty',0,100);"
"function draw(){const c=$('wave'),x=c.getContext('2d'),f=clamp($('freq').value,50,1000),d=clamp($('duty').value,0,100),base=230,hi=80;x.clearRect(0,0,c.width,c.height);x.fillStyle='#fff';x.fillRect(0,0,c.width,c.height);x.strokeStyle='#e1e7f0';x.lineWidth=1;for(let i=60;i<c.width-30;i+=60){x.beginPath();x.moveTo(i,36);x.lineTo(i,250);x.stroke()}const period=1000000/f,pulse=period*d/100,w=60+d*6.6;x.strokeStyle='#2563eb';x.lineWidth=6;x.lineJoin='round';x.beginPath();x.moveTo(70,base);x.lineTo(70,hi);x.lineTo(70+w,hi);x.lineTo(70+w,base);x.lineTo(830,base);x.stroke();x.fillStyle='#20242a';x.font='22px sans-serif';x.fillText('PWM: '+f+'Hz / '+d.toFixed(1)+'%',60,42);x.font='16px sans-serif';x.fillStyle='#69717d';x.fillText('周期 '+(period/1000).toFixed(2)+'ms，高电平 '+pulse.toFixed(0)+'us',60,280);$('period').textContent=(period/1000).toFixed(2)+'ms';$('pulse').textContent=pulse.toFixed(0)+'us'}"
"async function api(path){const r=await fetch(path);if(!r.ok)throw new Error(await r.text());return r.json()}"
"function applyState(s){$('gpio').value=s.gpio;$('freq').value=s.frequency_hz;$('freqRange').value=s.frequency_hz;$('duty').value=(s.duty_tenths/10).toFixed(1);$('dutyRange').value=(s.duty_tenths/10).toFixed(1);$('ip').textContent=s.ip;$('enabled').textContent=s.enabled?'已解锁':'已锁定';$('enabled').className=s.enabled?'ok':'locked';draw()}"
"async function refresh(){try{applyState(await api('/api/state'));setStatus('状态已同步')}catch(e){setStatus('读取失败: '+e.message,true)}}"
"let liveTimer=0,liveBusy=false,livePending=false;"
"function buildSetQuery(save){return new URLSearchParams({gpio:$('gpio').value,frequency:$('freq').value,duty:$('duty').value,save:save?'1':'0'})}"
"async function sendLive(){if(liveBusy){livePending=true;return}liveBusy=true;try{const s=await api('/api/set?'+buildSetQuery(false));$('enabled').textContent=s.enabled?'已解锁':'已锁定';$('enabled').className=s.enabled?'ok':'locked';setStatus(s.enabled?'实时输出已更新':'参数已更新，当前仍锁定')}catch(e){setStatus('实时更新失败: '+e.message,true)}finally{liveBusy=false;if(livePending){livePending=false;sendLive()}}}"
"function scheduleLive(){clearTimeout(liveTimer);liveTimer=setTimeout(sendLive,45)}"
"async function save(){try{applyState(await api('/api/set?'+buildSetQuery(true)));setStatus('设置已保存到 NVS')}catch(e){setStatus('保存失败: '+e.message,true)}}"
"async function enable(v){try{applyState(await api('/api/enable?value='+(v?1:0)));setStatus(v?'已解锁输出':'已锁定输出')}catch(e){setStatus('操作失败: '+e.message,true)}}"
"async function stop(){try{applyState(await api('/api/stop'));setStatus('STOP 已执行，输出锁定')}catch(e){setStatus('STOP 失败: '+e.message,true)}}"
"$('save').onclick=save;$('enable').onclick=()=>enable(true);$('lock').onclick=()=>enable(false);$('stop').onclick=stop;fields.forEach(id=>$(id).addEventListener('input',()=>{draw();scheduleLive()}));refresh();"
"</script></body></html>";

static esp_err_t send_json_state(httpd_req_t *req)
{
    const esc_pwm_config_t *config = esc_pwm_get_config();
    char json[256];
    snprintf(json, sizeof(json),
             "{\"protocol\":\"pwm\",\"gpio\":%d,\"frequency_hz\":%" PRIu32
             ",\"duty_tenths\":%u,\"pulse_width_us\":%" PRIu32
             ",\"enabled\":%s,\"ip\":\"%s\"}",
             config->gpio_num, config->frequency_hz, config->duty_tenths,
             esc_pwm_get_pulse_width_us(),
             esc_pwm_is_enabled() ? "true" : "false",
             get_wifi_ip_address());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ESC_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static bool get_query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char query[160];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

static esp_err_t state_handler(httpd_req_t *req)
{
    return send_json_state(req);
}

static esp_err_t set_handler(httpd_req_t *req)
{
    char value[24];
    const esc_pwm_config_t *current = esc_pwm_get_config();
    gpio_num_t gpio_num = current->gpio_num;
    uint32_t frequency_hz = current->frequency_hz;
    uint16_t duty_tenths = current->duty_tenths;

    if (get_query_value(req, "gpio", value, sizeof(value))) {
        gpio_num = (gpio_num_t)atoi(value);
    }
    if (get_query_value(req, "frequency", value, sizeof(value))) {
        frequency_hz = (uint32_t)atoi(value);
    }
    if (get_query_value(req, "duty", value, sizeof(value))) {
        float duty_percent = strtof(value, NULL);
        if (duty_percent < 0.0f) {
            duty_percent = 0.0f;
        }
        if (duty_percent > 100.0f) {
            duty_percent = 100.0f;
        }
        duty_tenths = (uint16_t)(duty_percent * 10.0f + 0.5f);
    }

    bool save = true;
    if (get_query_value(req, "save", value, sizeof(value))) {
        save = atoi(value) != 0;
    }

    esp_err_t err = esc_pwm_set_config(gpio_num, frequency_hz, duty_tenths, save);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid PWM settings");
        return err;
    }
    return send_json_state(req);
}

static esp_err_t enable_handler(httpd_req_t *req)
{
    char value[8] = {0};
    bool enabled = false;
    if (get_query_value(req, "value", value, sizeof(value))) {
        enabled = atoi(value) != 0;
    }

    ESP_RETURN_ON_ERROR(esc_pwm_set_enabled(enabled), TAG, "Failed to change output lock");
    return send_json_state(req);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(esc_pwm_stop(), TAG, "Failed to stop PWM");
    return send_json_state(req);
}

static void register_get(const char *uri, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t route = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &route));
}

esp_err_t esc_web_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "Failed to start ESC web server");

    register_get("/", root_handler);
    register_get("/api/state", state_handler);
    register_get("/api/set", set_handler);
    register_get("/api/enable", enable_handler);
    register_get("/api/stop", stop_handler);

    ESP_LOGI(TAG, "ESC web server started at http://%s", get_wifi_ip_address());
    return ESP_OK;
}
