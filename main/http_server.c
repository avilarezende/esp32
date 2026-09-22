#include "http_server.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "wifi_manager.h"
#include "wifi_form.h"
#include "assistant.h"

static const char *TAG = "http_config";

#define POST_BODY_MAXLEN 512
#define SCAN_JSON_MAXLEN 1024

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>ESP32 Wi-Fi</title><style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
    "background:#0f172a;color:#e2e8f0;margin:0;display:flex;min-height:100vh;"
    "align-items:center;justify-content:center}"
    ".card{background:#1e293b;padding:28px 32px;border-radius:14px;width:340px;"
    "box-shadow:0 10px 30px rgba(0,0,0,.4)}"
    "h1{font-size:1.25rem;margin:0 0 4px}p{color:#94a3b8;font-size:.85rem;margin:0 0 14px}"
    "label{display:block;font-size:.8rem;margin:14px 0 6px;color:#cbd5e1}"
    "input{width:100%;box-sizing:border-box;padding:10px 12px;border-radius:8px;"
    "border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:.95rem}"
    "button{width:100%;margin-top:18px;padding:11px;border:0;border-radius:8px;"
    "background:#3b82f6;color:#fff;font-size:1rem;font-weight:600;cursor:pointer}"
    "button:hover{background:#2563eb}button.alt{background:#475569}button.alt:hover{background:#334155}"
    "ul{list-style:none;padding:0;margin:8px 0 0}li{margin:0 0 6px}"
    ".net{width:100%;text-align:left;background:#0f172a;border:1px solid #334155;"
    "border-radius:8px;padding:8px 10px;color:#e2e8f0;cursor:pointer;font-size:.9rem;margin:0}"
    ".net:hover{border-color:#3b82f6}.row{display:flex;justify-content:space-between}"
    ".muted{color:#94a3b8;font-size:.8rem}.warn{color:#fca5a5}</style></head><body><div class=\"card\">";

static const char PAGE_TAIL[] = "</div></body></html>";

/* ---- helpers ---- */

static int read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int received = 0;
    int remaining = MIN(req->content_len, (int)buf_size - 1);
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + received, remaining);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return -1;
        }
        received += r;
        remaining -= r;
    }
    buf[received] = '\0';
    return received;
}

#if CONFIG_APP_ENABLE_WIFI_RADIO
/* Deferred reboot so the HTTP response can be flushed to the client first. */
static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "restarting to apply Wi-Fi change");
    esp_restart();
}
static void schedule_restart(void)
{
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
}
#endif

/* ---- pages ---- */

static void send_status_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, "<h1>Connected</h1><p>This ESP32 is configured.</p>");

    char row[160];
    const char *ssid = wifi_manager_get_ssid();
    const char *ip = wifi_manager_get_ip();
    int rssi = wifi_manager_get_rssi();

    snprintf(row, sizeof(row), "<div class=\"row\"><span class=\"muted\">Network</span><span>%s</span></div>",
             (ssid && ssid[0]) ? ssid : "&mdash;");
    httpd_resp_sendstr_chunk(req, row);
    snprintf(row, sizeof(row), "<div class=\"row\"><span class=\"muted\">IP address</span><span>%s</span></div>",
             (ip && ip[0]) ? ip : "&mdash;");
    httpd_resp_sendstr_chunk(req, row);
    if (rssi != 0) {
        snprintf(row, sizeof(row), "<div class=\"row\"><span class=\"muted\">Signal</span><span>%d dBm</span></div>", rssi);
        httpd_resp_sendstr_chunk(req, row);
    }
    if (wifi_manager_get_state() == WIFI_MANAGER_STATE_CONNECTING) {
        httpd_resp_sendstr_chunk(req, "<p class=\"warn\">Reconnecting&hellip;</p>");
    }

    httpd_resp_sendstr_chunk(req,
        "<form method=\"POST\" action=\"/forget\">"
        "<button class=\"alt\" type=\"submit\">Forget network</button></form>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
}

static void send_config_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req,
        "<h1>Wi-Fi Setup</h1><p>Choose a network for your ESP32 to join.</p>"
        "<form method=\"POST\" action=\"/connect\">"
        "<label for=\"ssid\">Network (SSID)</label>"
        "<input id=\"ssid\" name=\"ssid\" list=\"networks\" placeholder=\"Your Wi-Fi name\" "
        "maxlength=\"32\" required>"
        "<datalist id=\"networks\"></datalist>"
        "<label for=\"password\">Password</label>"
        "<input id=\"password\" name=\"password\" type=\"password\" placeholder=\"Leave blank if open\" "
        "maxlength=\"63\">"
        "<button type=\"submit\">Save &amp; Connect</button></form>"
        "<div class=\"row\" style=\"margin-top:18px\"><span class=\"muted\">Available networks</span>"
        "<a href=\"#\" class=\"muted\" onclick=\"loadNets();return false\">Rescan</a></div>"
        "<ul id=\"nets\"><li class=\"muted\">Scanning&hellip;</li></ul>"
        "<script>"
        "function pick(s){document.getElementById('ssid').value=s;"
        "document.getElementById('password').focus();}"
        "function loadNets(){var u=document.getElementById('nets');"
        "u.innerHTML='<li class=\"muted\">Scanning\\u2026</li>';"
        "fetch('/scan').then(function(r){return r.json()}).then(function(list){"
        "var dl=document.getElementById('networks');dl.innerHTML='';u.innerHTML='';"
        "if(!list.length){u.innerHTML='<li class=\"muted\">No networks found</li>';return;}"
        "list.forEach(function(n){"
        "var o=document.createElement('option');o.value=n.ssid;dl.appendChild(o);"
        "var li=document.createElement('li');var b=document.createElement('button');"
        "b.type='button';b.className='net';b.textContent=n.ssid+'  ('+n.rssi+' dBm)';"
        "b.onclick=function(){pick(n.ssid)};li.appendChild(b);u.appendChild(li);});"
        "}).catch(function(){u.innerHTML='<li class=\"warn\">Scan failed</li>';});}"
        "loadNets();"
        "</script>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
}

/* ---- assistant web app (served once connected) ---- */

/* Single-quoted attributes/JS keep C-string escaping minimal. */
static const char APP_HTML[] =
"<!DOCTYPE html><html lang='pt-br'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>ESP32 Assistant</title><style>"
"*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
"background:#0f172a;color:#e2e8f0}"
".overlay{position:fixed;inset:0;background:rgba(2,6,23,.85);display:flex;align-items:center;justify-content:center;padding:16px;z-index:9}"
".card{background:#1e293b;padding:22px 24px;border-radius:14px;width:360px;max-width:100%;box-shadow:0 10px 30px rgba(0,0,0,.4)}"
"h1{font-size:1.15rem;margin:0 0 6px}p{color:#94a3b8;font-size:.85rem;margin:0 0 12px}"
"label{display:block;font-size:.8rem;margin:12px 0 6px;color:#cbd5e1}"
".opt{display:flex;align-items:center;gap:8px;margin:6px 0}"
"input,select{width:100%;padding:10px;border-radius:8px;border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:.95rem}"
".row2{display:flex;gap:8px}.row2 button{width:auto;white-space:nowrap;margin:0}"
"button{width:100%;margin-top:16px;padding:11px;border:0;border-radius:8px;background:#3b82f6;color:#fff;font-weight:600;cursor:pointer}"
"button:hover{background:#2563eb}button.alt{background:#475569}"
".tabs{display:flex;gap:4px;padding:10px;background:#111827;position:sticky;top:0}"
".tabs button{margin:0;background:#1e293b}.tabs button.act{background:#3b82f6}"
"main{max-width:520px;margin:0 auto;padding:16px}"
".avatar{width:110px;height:110px;border-radius:50%;margin:6px auto 10px;position:relative;"
"background:radial-gradient(circle at 50% 35%,#38bdf8,#2563eb 70%);box-shadow:0 8px 24px rgba(37,99,235,.45)}"
".eyes{position:absolute;top:42px;left:0;right:0;display:flex;justify-content:center;gap:22px}"
".eye{width:12px;height:12px;border-radius:50%;background:#0b1220;animation:blink 4s infinite}"
".mouth{position:absolute;bottom:28px;left:50%;transform:translateX(-50%);width:40px;height:8px;border-radius:6px;background:#0b1220}"
".avatar.speaking .mouth{animation:talk .3s infinite}"
"@keyframes blink{0%,92%,100%{transform:scaleY(1)}96%{transform:scaleY(.12)}}"
"@keyframes talk{0%,100%{height:8px;width:40px}50%{height:20px;width:30px}}"
".botname{text-align:center;color:#93c5fd;font-weight:600;margin-bottom:10px}"
"#log{height:46vh;overflow:auto;display:flex;flex-direction:column;gap:8px;padding:4px}"
".m{max-width:80%;padding:9px 12px;border-radius:12px;font-size:.92rem;line-height:1.3}"
".m.you{align-self:flex-end;background:#3b82f6}.m.bot{align-self:flex-start;background:#334155}"
".composer{display:flex;gap:8px;margin-top:10px}.composer input{flex:1}.composer button{width:auto;margin:0}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
".dev{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:12px}"
".dev.on{border-color:#22c55e}.dn{font-weight:600}.dr{color:#94a3b8;font-size:.8rem}"
".ds{margin-top:6px;font-size:.85rem}.dev.on .ds{color:#86efac}.dev.off .ds{color:#94a3b8}"
".kv{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #24324a;font-size:.9rem}"
".muted{color:#94a3b8}.net{width:100%;text-align:left;background:#0f172a;border:1px solid #334155;border-radius:8px;padding:10px;color:#e2e8f0;margin:6px 0 0}"
".net:hover{border-color:#3b82f6}"
"</style></head><body>"

/* hub onboarding */
"<div id='ob' class='overlay' hidden><div class='card'>"
"<h1>Configuracao inicial</h1>"
"<p>Deseja incluir um hub da casa (HomeKit / Home Assistant) para ver e controlar seus dispositivos?</p>"
"<label class='opt'><input type='radio' name='inc' id='inc-yes' checked onchange='obToggle()'> Sim, incluir um hub</label>"
"<label class='opt'><input type='radio' name='inc' id='inc-no' onchange='obToggle()'> Agora nao</label>"
"<div id='hubfields'>"
"<label>Tipo</label><select id='htype'><option value='homeassistant'>Home Assistant</option>"
"<option value='homekit'>HomeKit</option><option value='mqtt'>MQTT</option></select>"
"<label>Endereco</label><div class='row2'><input id='haddr' placeholder='ex: homeassistant.local:8123'>"
"<button type='button' class='alt' onclick='discover()'>Pesquisar</button></div>"
"<select id='disc' hidden onchange='discPick(this.value)'></select>"
"<label>Usuario</label><input id='huser' placeholder='usuario'>"
"<label>Senha / token</label><input id='hpass' type='password' placeholder='senha ou token'>"
"</div><button onclick='saveHub(event)'>Continuar</button></div></div>"

/* bot chooser */
"<div id='botsel' class='overlay' hidden><div class='card'>"
"<h1>Escolha o bot principal</h1>"
"<p>Sua conta tem mais de um bot. Escolha qual aparece com o avatar.</p>"
"<div id='botlist'></div></div></div>"

/* app */
"<div id='app' hidden>"
"<nav class='tabs'><button data-tab='chat' class='act'>Assistente</button>"
"<button data-tab='dev'>Dispositivos</button><button data-tab='set'>Ajustes</button></nav>"
"<main>"
"<section id='tab-chat'>"
"<div id='avatar' class='avatar'><div class='eyes'><span class='eye'></span><span class='eye'></span></div><div class='mouth'></div></div>"
"<div id='botname' class='botname'>Grok</div>"
"<div id='log'></div>"
"<form class='composer' onsubmit='send(event)'><input id='msg' placeholder='Pergunte algo ao seu bot...' autocomplete='off'><button>Enviar</button></form>"
"</section>"
"<section id='tab-dev' hidden><div id='devs' class='grid'></div></section>"
"<section id='tab-set' hidden>"
"<div class='kv'><span class='muted'>Rede</span><span id='s-ssid'>-</span></div>"
"<div class='kv'><span class='muted'>IP</span><span id='s-ip'>-</span></div>"
"<div class='kv'><span class='muted'>Hub</span><span id='s-hub'>-</span></div>"
"<label>Bot principal</label><select id='s-bot' onchange='changeBot(this.value)'></select>"
"<button class='alt' onclick='forget()'>Esquecer rede Wi-Fi</button>"
"</section></main></div>"

"<script>"
"var S=null,BOTS=null;"
"function $(s){return document.querySelector(s);}"
"async function jget(u){return (await fetch(u)).json();}"
"function post(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});}"
"function esc(t){var d=document.createElement('div');d.textContent=t;return d.innerHTML;}"
"function show(id){['ob','botsel','app'].forEach(function(x){$('#'+x).hidden=(x!==id);});}"
"function obToggle(){$('#hubfields').hidden=$('#inc-no').checked;}"
"function discPick(v){if(v)$('#haddr').value=v;}"
"async function discover(){var l=await jget('/hub/discover');var s=$('#disc');s.innerHTML='<option value=\"\">(escolher encontrado)</option>';l.forEach(function(c){var o=document.createElement('option');o.value=c.addr;o.textContent=c.name+' - '+c.addr;s.appendChild(o);});s.hidden=false;}"
"async function saveHub(e){e.preventDefault();var inc=$('#inc-yes').checked;var b='enabled='+(inc?'1':'0');"
"if(inc){b+='&type='+encodeURIComponent($('#htype').value)+'&address='+encodeURIComponent($('#haddr').value)+'&user='+encodeURIComponent($('#huser').value)+'&password='+encodeURIComponent($('#hpass').value);}"
"await post('/hub',b);await boot();}"
"async function showBots(){BOTS=await jget('/bots');var w=$('#botlist');w.innerHTML='';BOTS.forEach(function(bt){var x=document.createElement('button');x.className='net';x.textContent=bt.name+(bt.controlsDevices?'  (controla dispositivos)':'');x.onclick=function(){chooseBot(bt.id);};w.appendChild(x);});show('botsel');}"
"async function chooseBot(id){await post('/bot','id='+encodeURIComponent(id));await boot();}"
"async function changeBot(id){await post('/bot','id='+encodeURIComponent(id));S=await jget('/state');setName();}"
"function setName(){var b=(BOTS||[]).find(function(x){return x.id===S.principalBot;})||(BOTS||[])[0];$('#botname').textContent=b?b.name:'Grok';}"
"function tab(t){['chat','dev','set'].forEach(function(x){$('#tab-'+x).hidden=(x!==t);});"
"document.querySelectorAll('[data-tab]').forEach(function(btn){btn.classList.toggle('act',btn.dataset.tab===t);});"
"if(t==='dev')loadDevices();if(t==='set')loadSettings();}"
"async function boot(){S=await jget('/state');"
"if(!S.onboardingDone){show('ob');obToggle();}"
"else if(S.botCount>1&&!S.principalBot){await showBots();}"
"else{if(!BOTS)BOTS=await jget('/bots');setName();show('app');tab('chat');}}"
"function addMsg(w,t){var l=$('#log');var d=document.createElement('div');d.className='m '+w;d.innerHTML=esc(t);l.appendChild(d);l.scrollTop=l.scrollHeight;}"
"function speak(){var a=$('#avatar');a.classList.add('speaking');setTimeout(function(){a.classList.remove('speaking');},1600);}"
"async function send(e){e.preventDefault();var i=$('#msg');var t=i.value.trim();if(!t)return;addMsg('you',t);i.value='';"
"var r=await (await post('/chat','message='+encodeURIComponent(t))).json();speak();addMsg('bot',r.reply);}"
"async function loadDevices(){var g=$('#devs');g.innerHTML='<p class=muted>carregando...</p>';var l=await jget('/devices');g.innerHTML='';"
"l.forEach(function(d){var c=document.createElement('div');c.className='dev '+(d.on?'on':'off');"
"c.innerHTML='<div class=dn>'+esc(d.name)+'</div><div class=dr>'+esc(d.room)+'</div><div class=ds>'+(d.on?'ligado':'desligado')+(d.detail?(' - '+esc(d.detail)):'')+'</div>';g.appendChild(c);});}"
"async function loadSettings(){$('#s-ssid').textContent=S.ssid||'-';$('#s-ip').textContent=S.ip||'-';"
"$('#s-hub').textContent=S.hubConfigured?(S.hubType+' @ '+S.hubAddr):'nenhum';"
"if(!BOTS)BOTS=await jget('/bots');var s=$('#s-bot');s.innerHTML='';BOTS.forEach(function(b){var o=document.createElement('option');o.value=b.id;o.textContent=b.name;if(b.id===S.principalBot)o.selected=true;s.appendChild(o);});}"
"async function forget(){await post('/forget','');location.reload();}"
"document.querySelectorAll('[data-tab]').forEach(function(b){b.onclick=function(){tab(b.dataset.tab);};});"
"boot();"
"</script></body></html>";

static void send_app_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, APP_HTML);
}

/* Build a JSON document with `fn` and send it. */
static esp_err_t send_json(httpd_req_t *req, void (*fn)(char *, size_t), size_t cap)
{
    char *buf = malloc(cap);
    if (!buf) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    fn(buf, cap);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
}

/* ---- handlers ---- */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    if (wifi_manager_is_connected()) {
        send_app_page(req);
    } else {
        send_config_page(req);
    }
    return ESP_OK;
}

static esp_err_t state_get_handler(httpd_req_t *req)   { return send_json(req, assistant_state_json, 512); }
static esp_err_t bots_get_handler(httpd_req_t *req)    { return send_json(req, assistant_bots_json, 384); }
static esp_err_t devices_get_handler(httpd_req_t *req) { return send_json(req, assistant_devices_json, 1024); }
static esp_err_t discover_get_handler(httpd_req_t *req){ return send_json(req, assistant_discover_json, 256); }

static esp_err_t chat_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char msg[192] = {0};
    wifi_form_get_field(body, "message", msg, sizeof(msg));

    char reply[256];
    assistant_chat_reply(msg, reply, sizeof(reply));

    char reply_esc[400];
    wifi_form_json_escape(reply, reply_esc, sizeof(reply_esc));

    char out[440];
    snprintf(out, sizeof(out), "{\"reply\":\"%s\"}", reply_esc);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

static esp_err_t hub_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char en[8] = {0}, type[32] = {0}, addr[96] = {0}, user[64] = {0}, pass[96] = {0};
    wifi_form_get_field(body, "enabled", en, sizeof(en));
    wifi_form_get_field(body, "type", type, sizeof(type));
    wifi_form_get_field(body, "address", addr, sizeof(addr));
    wifi_form_get_field(body, "user", user, sizeof(user));
    wifi_form_get_field(body, "password", pass, sizeof(pass));

    bool enabled = (en[0] == '1');
    assistant_set_hub(enabled, type, addr, user, pass);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t bot_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }
    char id[32] = {0};
    wifi_form_get_field(body, "id", id, sizeof(id));
    assistant_set_principal_bot(id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    char *json = malloc(SCAN_JSON_MAXLEN);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    wifi_manager_scan_json(json, SCAN_JSON_MAXLEN);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    char body[POST_BODY_MAXLEN];
    if (read_body(req, body, sizeof(body)) < 0) {
        return ESP_FAIL;
    }

    char ssid[WIFI_MANAGER_SSID_MAXLEN] = {0};
    char pass[WIFI_MANAGER_PASS_MAXLEN] = {0};
    wifi_form_get_field(body, "ssid", ssid, sizeof(ssid));
    wifi_form_get_field(body, "password", pass, sizeof(pass));

    if (!wifi_form_valid_ssid(ssid)) {
        ESP_LOGW(TAG, "rejected invalid SSID");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID must be 1-32 characters");
        return ESP_OK;
    }
    if (!wifi_form_valid_password(pass)) {
        ESP_LOGW(TAG, "rejected invalid password length");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be empty or 8-63 characters");
        return ESP_OK;
    }

    esp_err_t err = wifi_manager_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save credentials");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "received new credentials via portal for SSID '%s'", ssid);

    char mid[160];
    snprintf(mid, sizeof(mid),
        "<h1>Saved</h1><p>Connecting to <b>%s</b>&hellip;</p>"
        "<p class=\"muted\">The device is applying the new configuration.</p>", ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, mid);
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);

#if CONFIG_APP_ENABLE_WIFI_RADIO
    schedule_restart();
#else
    ESP_LOGI(TAG, "credentials saved; simulating connected state (QEMU build)");
    wifi_manager_mark_connected(ssid);
#endif
    return ESP_OK;
}

static esp_err_t forget_post_handler(httpd_req_t *req)
{
    esp_err_t err = wifi_manager_forget();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not clear credentials");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "network forgotten via portal");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req,
        "<h1>Network forgotten</h1>"
        "<p>Stored credentials were cleared. The setup portal is available again.</p>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);

#if CONFIG_APP_ENABLE_WIFI_RADIO
    schedule_restart();
#else
    wifi_manager_mark_provisioning();
#endif
    return ESP_OK;
}

httpd_handle_t http_config_server_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;

    if (httpd_start(&server, &config) != ESP_OK) {
        return NULL;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",             .method = HTTP_GET,  .handler = index_get_handler },
        { .uri = "/scan",         .method = HTTP_GET,  .handler = scan_get_handler },
        { .uri = "/connect",      .method = HTTP_POST, .handler = connect_post_handler },
        { .uri = "/forget",       .method = HTTP_POST, .handler = forget_post_handler },
        { .uri = "/state",        .method = HTTP_GET,  .handler = state_get_handler },
        { .uri = "/bots",         .method = HTTP_GET,  .handler = bots_get_handler },
        { .uri = "/devices",      .method = HTTP_GET,  .handler = devices_get_handler },
        { .uri = "/hub/discover", .method = HTTP_GET,  .handler = discover_get_handler },
        { .uri = "/chat",         .method = HTTP_POST, .handler = chat_post_handler },
        { .uri = "/hub",          .method = HTTP_POST, .handler = hub_post_handler },
        { .uri = "/bot",          .method = HTTP_POST, .handler = bot_post_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "configuration web server started on port %d", config.server_port);
    return server;
}

void http_config_server_stop(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}
