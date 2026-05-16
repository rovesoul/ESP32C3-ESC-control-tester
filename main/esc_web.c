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

static const char *FAVICON_SVG =
"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\" role=\"img\" aria-label=\"ESC fan logo\">"
"<rect width=\"64\" height=\"64\" rx=\"12\" fill=\"#111827\"/>"
"<g transform=\"translate(32 24)\"><circle r=\"16\" fill=\"#f8fafc\"/>"
"<g fill=\"none\" stroke=\"#111827\" stroke-width=\"3\" stroke-linecap=\"round\">"
"<path d=\"M0-13v7\"/><path d=\"M0-13v7\" transform=\"rotate(60)\"/><path d=\"M0-13v7\" transform=\"rotate(120)\"/>"
"<path d=\"M0-13v7\" transform=\"rotate(180)\"/><path d=\"M0-13v7\" transform=\"rotate(240)\"/><path d=\"M0-13v7\" transform=\"rotate(300)\"/>"
"</g><g fill=\"#2563eb\">"
"<path d=\"M0-3C3-11 10-16 14-13c4 3 1 10-8 14C3 2 1 0 0-3Z\"/>"
"<path d=\"M0-3C3-11 10-16 14-13c4 3 1 10-8 14C3 2 1 0 0-3Z\" transform=\"rotate(120)\"/>"
"<path d=\"M0-3C3-11 10-16 14-13c4 3 1 10-8 14C3 2 1 0 0-3Z\" transform=\"rotate(240)\"/>"
"</g><circle r=\"5\" fill=\"#111827\"/><circle r=\"2.2\" fill=\"#f8fafc\"/></g>"
"<path d=\"M5 55V48H10V55H13V48H18V55H21V48H26V55H29V48H34V55H37V48H42V55H45V48H50V55H53V48H58V55H61\" fill=\"none\" stroke=\"#2563eb\" stroke-width=\"1.4\" stroke-linecap=\"square\" stroke-linejoin=\"miter\" opacity=\".65\"/>"
"<text x=\"32\" y=\"54\" text-anchor=\"middle\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"16\" font-weight=\"800\" fill=\"#f8fafc\" stroke=\"#111827\" stroke-width=\"2.6\" paint-order=\"stroke fill\">ESC</text>"
"</svg>";

static const char *ESC_HTML =
"<!DOCTYPE html><html lang='zh-CN'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<link rel='icon' type='image/svg+xml' href='/favicon.svg'>"
"<title>ESP32-C3 ESC 控制台</title>"
"<style>"
":root{--bg:#f5f7fb;--panel:#fff;--ink:#20242a;--muted:#687282;--line:#d8dee9;--blue:#2563eb;--green:#0f9f6e;--red:#d14d45;--amber:#bd7417}"
"*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC','Microsoft YaHei',sans-serif;line-height:1.55}"
"header{background:#101827;color:#fff;padding:22px 18px}main{width:min(1040px,calc(100% - 28px));margin:22px auto 42px}.top{width:min(1040px,calc(100% - 28px));margin:0 auto}.brand{display:flex;align-items:center;gap:14px}.brand-logo{width:128px;height:128px;border-radius:12px;flex:0 0 auto;box-shadow:0 10px 24px rgba(0,0,0,.24)}"
"h1{font-size:30px;margin:0 0 6px}h2{font-size:22px;margin:0 0 14px}.sub{color:#d9e1ee;margin:0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:18px;box-shadow:0 8px 22px rgba(18,25,38,.08)}"
".links{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}.links a{display:inline-flex;align-items:center;gap:8px;color:#fff;text-decoration:none;border:1px solid rgba(255,255,255,.24);border-radius:6px;padding:7px 10px;background:rgba(255,255,255,.08);font-weight:700}.links svg{width:20px;height:20px;fill:currentColor}"
"label{display:block;font-weight:700;margin:13px 0 7px}select,input{width:100%;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--ink);font-size:16px;padding:10px}input[type=range]{padding:0;accent-color:var(--blue)}"
".row{display:grid;grid-template-columns:1fr 130px;gap:10px;align-items:center}.metrics{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:14px}.metric{border:1px solid var(--line);border-radius:6px;padding:10px;background:#fbfcff}.metric b{display:block;color:var(--muted);font-size:12px}.metric span{font-size:20px;font-weight:800}"
".actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}button{border:0;border-radius:6px;padding:11px 14px;font-size:15px;font-weight:800;cursor:pointer}.primary{background:var(--blue);color:#fff}.enable{background:var(--green);color:#fff}.stop{background:var(--red);color:#fff}.ghost{background:#e9eef7;color:#273142}"
".status{margin-top:12px;padding:10px;border-radius:6px;background:#eef6ff;color:#1c4b82}.warn{background:#fff7e8;border-left:4px solid var(--amber);padding:12px;border-radius:0 6px 6px 0;color:#553810}.locked{color:var(--red)}.ok{color:var(--green)}"
"canvas{width:100%;height:260px;border:1px solid var(--line);border-radius:8px;background:#fff;display:block}.disabled{opacity:.55}.footer{color:var(--muted);font-size:13px;margin-top:16px}@media(max-width:760px){.grid{grid-template-columns:1fr}.row{grid-template-columns:1fr}.metrics{grid-template-columns:1fr}.brand{align-items:flex-start}.brand-logo{width:108px;height:108px}}"
"</style></head><body>"
"<header><div class='top'><div class='brand'><img class='brand-logo' src='/favicon.svg' alt='ESC logo'><div><h1>ESP32-C3 ESC 控制台</h1><p class='sub'>支持 PWM 和 DShot 输出，点击解锁后才会驱动 ESC。</p><div class='links'>"
"<a href='https://github.com/rovesoul/ESP32C3-ESC-control-tester' target='_blank' rel='noopener'><svg viewBox='0 0 24 24' aria-hidden='true'><path d='M12 .5A11.5 11.5 0 0 0 8.36 22.9c.58.1.79-.25.79-.56v-2.02c-3.22.7-3.9-1.38-3.9-1.38-.53-1.34-1.29-1.7-1.29-1.7-1.05-.72.08-.7.08-.7 1.17.08 1.79 1.2 1.79 1.2 1.03 1.77 2.71 1.26 3.37.96.1-.75.4-1.26.73-1.55-2.57-.29-5.27-1.28-5.27-5.72 0-1.26.45-2.3 1.2-3.11-.12-.3-.52-1.48.11-3.07 0 0 .98-.31 3.19 1.19a11.1 11.1 0 0 1 5.8 0c2.21-1.5 3.18-1.19 3.18-1.19.64 1.59.24 2.77.12 3.07.75.81 1.2 1.85 1.2 3.11 0 4.45-2.71 5.43-5.29 5.72.42.36.79 1.07.79 2.16v3.03c0 .31.21.67.8.56A11.5 11.5 0 0 0 12 .5Z'/></svg>GitHub</a>"
"<a href='https://space.bilibili.com/185878223' target='_blank' rel='noopener'><svg viewBox='0 0 24 24' aria-hidden='true'><path d='M8.1 3.2a.9.9 0 0 1 1.27.05L12 6.1l2.63-2.85a.9.9 0 1 1 1.32 1.22L13.9 6.7h3.35A3.75 3.75 0 0 1 21 10.45v5.3a3.75 3.75 0 0 1-3.75 3.75H6.75A3.75 3.75 0 0 1 3 15.75v-5.3A3.75 3.75 0 0 1 6.75 6.7h3.35L8.05 4.47a.9.9 0 0 1 .05-1.27ZM6.75 8.5c-1.08 0-1.95.87-1.95 1.95v5.3c0 1.08.87 1.95 1.95 1.95h10.5c1.08 0 1.95-.87 1.95-1.95v-5.3c0-1.08-.87-1.95-1.95-1.95H6.75Zm2.15 3.1a1 1 0 1 1 0 2 1 1 0 0 1 0-2Zm6.2 0a1 1 0 1 1 0 2 1 1 0 0 1 0-2Zm-6.65 3.35a.75.75 0 0 1 1.05-.15c1.52 1.13 3.48 1.13 5 0a.75.75 0 0 1 .9 1.2 5.7 5.7 0 0 1-6.8 0 .75.75 0 0 1-.15-1.05Z'/></svg>Bilibili</a>"
"</div></div></div></div></header>"
"<main><div class='grid'><section class='card'><h2>输出设置</h2>"
"<label for='protocol'>电调驱动形式</label><select id='protocol'><option value='pwm'>PWM / Servo PWM</option><option value='pulse'>脉宽控制</option><option value='dshot150'>DShot150</option><option value='dshot300'>DShot300</option><option value='dshot600'>DShot600</option><option value='dshot1200'>DShot1200</option></select>"
"<label for='gpio'>输出 GPIO</label><select id='gpio'><option>0</option><option>1</option><option>2</option><option selected>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>10</option><option>20</option><option>21</option></select>"
"<div id='freqRow'><label>频率 Hz（50-1000，PWM/脉宽）</label><div class='row'><input id='freqRange' type='range' min='50' max='1000' value='50'><input id='freq' type='number' min='50' max='1000' value='50'></div></div>"
"<div id='dutyRow'><label>占空比 %（0-100）</label><div class='row'><input id='dutyRange' type='range' min='0' max='100' step='0.1' value='5'><input id='duty' type='number' min='0' max='100' step='0.1' value='5'></div></div>"
"<div id='pulseRow'><label>高电平脉宽 us（1000-2000）</label><div class='row'><input id='pulseRange' type='range' min='1000' max='2000' step='1' value='1000'><input id='pulseWidth' type='number' min='1000' max='2000' step='1' value='1000'></div></div>"
"<div class='actions'><button class='primary' id='save'>保存设置</button><button class='enable' id='enable'>解锁输出</button><button class='stop' id='stop'>STOP</button></div><div id='status' class='status'>滑块会实时应用到输出，保存按钮只负责写入 NVS。</div></section>"
"<section class='card'><h2>实时状态</h2><canvas id='wave' width='900' height='300'></canvas><div class='metrics'><div class='metric'><b>设备 IP</b><span id='ip'>-</span></div><div class='metric'><b>输出状态</b><span id='enabled'>-</span></div><div class='metric'><b id='periodLabel'>周期</b><span id='period'>-</span></div><div class='metric'><b id='pulseLabel'>高电平脉宽</b><span id='pulse'>-</span></div></div></section></div>"
"<section class='card' style='margin-top:16px'><h2>安全提醒</h2><p class='warn'>测试 ESC 前先拆桨。只有点击“解锁输出”后才会按当前协议和油门驱动 ESC。STOP 会立即停止输出并把油门归零。</p><p class='footer'>XIAO ESP32-C3 上建议优先用 GPIO3/4，其次 GPIO5/6/7/10。GPIO8 当前用于呼吸灯且也是板载 LED/SDA，GPIO9 是 Boot 长按清配网且是 SCL，GPIO20/21 是串口 RX/TX。</p></section>"
"</main><script>"
"const $=id=>document.getElementById(id);const fields=['gpio','freq','freqRange','duty','dutyRange','pulseWidth','pulseRange'];"
"function clamp(v,min,max){return Math.min(max,Math.max(min,Number(v)||0))}"
"function setStatus(t,bad=false){$('status').textContent=t;$('status').style.background=bad?'#fff0ef':'#eef6ff'}"
"function syncPair(a,b,min,max){$(a).addEventListener('input',()=>{$(b).value=clamp($(a).value,min,max);draw()});$(b).addEventListener('input',()=>{$(a).value=clamp($(b).value,min,max);draw()})}"
"syncPair('freqRange','freq',50,1000);syncPair('dutyRange','duty',0,100);syncPair('pulseRange','pulseWidth',1000,2000);"
"function isPulse(){return $('protocol').value==='pulse'}"
"function isDigital(){return $('protocol').value.indexOf('dshot')===0}"
"function dshotThrottle(d){return d<=0?0:48+Math.floor(d*(2047-48)/100)}"
"function syncPulseLimit(){const f=clamp($('freq').value,50,1000),maxPulse=Math.min(2000,Math.floor(1000000/f));$('pulseRange').max=maxPulse;$('pulseWidth').max=maxPulse;if(Number($('pulseWidth').value)>maxPulse){$('pulseWidth').value=maxPulse;$('pulseRange').value=maxPulse}}"
"function draw(){syncPulseLimit();const c=$('wave'),x=c.getContext('2d'),p=$('protocol').value,f=clamp($('freq').value,50,1000),period=1000000/f,manualPulse=clamp($('pulseWidth').value,1000,Number($('pulseWidth').max)||2000),dutyInput=clamp($('duty').value,0,100),pulse=isPulse()?manualPulse:period*dutyInput/100,d=isPulse()?pulse*100/period:dutyInput,base=230,hi=80;x.clearRect(0,0,c.width,c.height);x.fillStyle='#fff';x.fillRect(0,0,c.width,c.height);x.strokeStyle='#e1e7f0';x.lineWidth=1;for(let i=60;i<c.width-30;i+=60){x.beginPath();x.moveTo(i,36);x.lineTo(i,250);x.stroke()}const isD=isDigital(),w=60+Math.min(d,100)*6.6;x.strokeStyle=isD?'#0f9f6e':(isPulse()?'#bd7417':'#2563eb');x.lineWidth=6;x.lineJoin='round';x.beginPath();x.moveTo(70,base);x.lineTo(70,hi);x.lineTo(70+w,hi);x.lineTo(70+w,base);x.lineTo(830,base);x.stroke();x.fillStyle='#20242a';x.font='22px sans-serif';x.fillText((isD?p.toUpperCase():(isPulse()?'脉宽: ':'PWM: ')+f+'Hz')+' / '+(isPulse()?pulse.toFixed(0)+'us':d.toFixed(1)+'%'),60,42);x.font='16px sans-serif';x.fillStyle='#69717d';if(isD){const baud=p.replace('dshot','');x.fillText('DShot'+baud+'，油门值 '+dshotThrottle(dutyInput),60,280);$('periodLabel').textContent='协议速率';$('pulseLabel').textContent='DShot 油门';$('period').textContent=baud+'k';$('pulse').textContent=dshotThrottle(dutyInput)}else{x.fillText('周期 '+(period/1000).toFixed(2)+'ms，高电平 '+pulse.toFixed(0)+'us，占空比 '+d.toFixed(2)+'%',60,280);$('periodLabel').textContent='周期';$('pulseLabel').textContent='高电平脉宽';$('period').textContent=(period/1000).toFixed(2)+'ms';$('pulse').textContent=pulse.toFixed(0)+'us'}$('freqRow').className=isD?'disabled':'';$('dutyRow').className=isPulse()?'disabled':'';$('pulseRow').className=isPulse()?'':'disabled'}"
"async function api(path){const r=await fetch(path);if(!r.ok)throw new Error(await r.text());return r.json()}"
"function applyState(s){const pulse=s.configured_pulse_width_us||1000;$('protocol').value=s.protocol;$('gpio').value=s.gpio;$('freq').value=s.frequency_hz;$('freqRange').value=s.frequency_hz;$('duty').value=(s.duty_tenths/10).toFixed(1);$('dutyRange').value=(s.duty_tenths/10).toFixed(1);$('pulseWidth').value=pulse;$('pulseRange').value=pulse;$('ip').textContent=s.ip;$('enabled').textContent=s.enabled?'已解锁':'未解锁';$('enabled').className=s.enabled?'ok':'locked';draw()}"
"async function refresh(){try{applyState(await api('/api/state'));setStatus('状态已同步')}catch(e){setStatus('读取失败: '+e.message,true)}}"
"let liveTimer=0,liveBusy=false,livePending=false;"
"function buildSetQuery(save){return new URLSearchParams({protocol:$('protocol').value,gpio:$('gpio').value,frequency:$('freq').value,duty:$('duty').value,pulse_width:$('pulseWidth').value,save:save?'1':'0'})}"
"async function sendLive(){if(liveBusy){livePending=true;return}liveBusy=true;try{const s=await api('/api/set?'+buildSetQuery(false));$('enabled').textContent=s.enabled?'已解锁':'未解锁';$('enabled').className=s.enabled?'ok':'locked';setStatus(s.enabled?'实时输出已更新':'参数已更新，当前未解锁')}catch(e){setStatus('实时更新失败: '+e.message,true)}finally{liveBusy=false;if(livePending){livePending=false;sendLive()}}}"
"function scheduleLive(){clearTimeout(liveTimer);liveTimer=setTimeout(sendLive,5)}"
"function resetThrottleControls(){$('duty').value='0.0';$('dutyRange').value='0';$('pulseWidth').value='1000';$('pulseRange').value='1000'}"
"async function protocolChanged(){clearTimeout(liveTimer);livePending=false;resetThrottleControls();draw();try{await api('/api/stop');applyState(await api('/api/set?'+buildSetQuery(false)));setStatus('协议已切换，输出已 STOP')}catch(e){setStatus('协议切换失败: '+e.message,true)}}"
"async function save(){try{applyState(await api('/api/set?'+buildSetQuery(true)));setStatus('设置已保存到 NVS')}catch(e){setStatus('保存失败: '+e.message,true)}}"
"async function enable(){try{applyState(await api('/api/enable'));setStatus('已解锁输出')}catch(e){setStatus('操作失败: '+e.message,true)}}"
"async function stop(){clearTimeout(liveTimer);livePending=false;resetThrottleControls();draw();try{applyState(await api('/api/stop'));setStatus('STOP 已执行，输出已停止')}catch(e){setStatus('STOP 失败: '+e.message,true)}}"
"$('save').onclick=save;$('enable').onclick=enable;$('stop').onclick=stop;$('protocol').addEventListener('change',protocolChanged);fields.forEach(id=>$(id).addEventListener('input',()=>{draw();scheduleLive()}));refresh();"
"</script></body></html>";

static esp_err_t send_json_state(httpd_req_t *req)
{
    const esc_pwm_config_t *config = esc_pwm_get_config();
    char json[320];
    snprintf(json, sizeof(json),
             "{\"protocol\":\"%s\",\"gpio\":%d,\"frequency_hz\":%" PRIu32
             ",\"duty_tenths\":%u,\"pulse_width_us\":%" PRIu32
             ",\"configured_pulse_width_us\":%u"
             ",\"dshot_throttle\":%u"
             ",\"enabled\":%s,\"ip\":\"%s\"}",
             esc_pwm_protocol_to_string(config->protocol),
             config->gpio_num, config->frequency_hz, config->duty_tenths,
             esc_pwm_get_pulse_width_us(), config->pulse_width_us, esc_pwm_get_dshot_throttle(),
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

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    httpd_resp_send(req, FAVICON_SVG, HTTPD_RESP_USE_STRLEN);
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
    esc_protocol_t protocol = current->protocol;
    gpio_num_t gpio_num = current->gpio_num;
    uint32_t frequency_hz = current->frequency_hz;
    uint16_t duty_tenths = current->duty_tenths;
    uint16_t pulse_width_us = current->pulse_width_us;

    if (get_query_value(req, "protocol", value, sizeof(value)) &&
        !esc_pwm_protocol_from_string(value, &protocol)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ESC protocol");
        return ESP_ERR_INVALID_ARG;
    }
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
    if (get_query_value(req, "pulse_width", value, sizeof(value))) {
        pulse_width_us = (uint16_t)atoi(value);
    }

    bool save = true;
    if (get_query_value(req, "save", value, sizeof(value))) {
        save = atoi(value) != 0;
    }

    esp_err_t err = esc_pwm_set_config(protocol, gpio_num, frequency_hz, duty_tenths, pulse_width_us, save);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ESC settings");
        return err;
    }
    return send_json_state(req);
}

static esp_err_t enable_handler(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(esc_pwm_set_enabled(true), TAG, "Failed to enable ESC output");
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
    register_get("/favicon.svg", favicon_handler);
    register_get("/api/state", state_handler);
    register_get("/api/set", set_handler);
    register_get("/api/enable", enable_handler);
    register_get("/api/stop", stop_handler);

    ESP_LOGI(TAG, "ESC web server started at http://%s", get_wifi_ip_address());
    return ESP_OK;
}
