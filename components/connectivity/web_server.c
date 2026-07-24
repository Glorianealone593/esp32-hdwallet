// DibaVault — HTTP server (untrusted). GPL-3.0. dibachain.
// Serves the embedded UI + JSON API. Every key operation is delegated to the
// vault via VORIGIN_REMOTE_HTTP so remote policy applies (physical confirm,
// never reveal mnemonic over HTTP, etc.). This file does NOT include keystore.h.
#include "web_server.h"
#include "vault_ipc.h"
#include "net_config.h"
#include "wifi_mgr.h"
#include "rpc_client.h"
#include "ota.h"
#include "hal_config.h"
#include "chains.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "cJSON.h"

static const char *TAG="web";
static httpd_handle_t s_srv=NULL;

// ---- helpers ----
static char *read_body(httpd_req_t *r){
    int len=r->content_len; if(len<=0||len>4096) return NULL;
    char *b=malloc(len+1); if(!b) return NULL;
    int got=0; while(got<len){ int k=httpd_req_recv(r,b+got,len-got); if(k<=0){free(b);return NULL;} got+=k; }
    b[len]=0; return b;
}
static esp_err_t send_json(httpd_req_t *r,cJSON *j){
    char *s=cJSON_PrintUnformatted(j); httpd_resp_set_type(r,"application/json");
    httpd_resp_sendstr(r,s?s:"{}"); free(s); cJSON_Delete(j); return ESP_OK;
}
static esp_err_t send_err(httpd_req_t *r,const char *code,const char *msg){
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"error",code);
    cJSON_AddStringToObject(j,"message",msg?msg:code);
    httpd_resp_set_status(r,"400 Bad Request"); return send_json(r,j);
}
static const char *errname(dv_err_t e){
    switch(e){case DV_OK:return "ok";case DV_ERR_LOCKED:return "locked";
    case DV_ERR_BAD_PIN:return "bad_pin";case DV_ERR_PIN_LOCKOUT:return "pin_lockout";
    case DV_ERR_USER_REJECTED:return "user_rejected";case DV_ERR_TIMEOUT:return "timeout";
    case DV_ERR_NOT_PROVISIONED:return "not_provisioned";case DV_ERR_ALREADY_PROVISIONED:return "already_provisioned";
    case DV_ERR_BOUNDARY:return "boundary_denied";case DV_ERR_UNSUPPORTED_CHAIN:return "unsupported_chain";
    default:return "error";}
}
// decimal string -> big-endian bytes (<=32). Returns significant length.
static size_t dec_to_be(const char *dec,uint8_t out[32]){
    uint8_t b[32]={0};
    for(const char *p=dec;*p;p++){ if(*p<'0'||*p>'9')continue; int carry=*p-'0';
        for(int i=31;i>=0;i--){ int x=b[i]*10+carry; b[i]=x&0xff; carry=x>>8; } }
    size_t i=0; while(i<32&&b[i]==0)i++; size_t n=32-i; if(n==0){out[0]=0;return 0;}
    memmove(out,b+i,n); return n;
}
static dv_chain_t family_from_str(const char *s){
    if(!s)return DV_CHAIN_EVM;
    if(!strcmp(s,"bitcoin"))return DV_CHAIN_BITCOIN;
    if(!strcmp(s,"tron"))return DV_CHAIN_TRON;
    if(!strcmp(s,"solana"))return DV_CHAIN_SOLANA;
    return DV_CHAIN_EVM;
}

// ---- /api/status ----
static esp_err_t h_status(httpd_req_t *r){
    vault_req_t q={.kind=VREQ_GET_STATUS,.origin=VORIGIN_REMOTE_HTTP};
    vault_resp_t s; vault_ipc_request(&q,&s,5000);
    dv_hal_config_t hc; hal_config_load(&hc);
    cJSON *j=cJSON_CreateObject();
    cJSON_AddBoolToObject(j,"provisioned",s.u.status.provisioned);
    cJSON_AddBoolToObject(j,"locked",s.u.status.locked);
    cJSON_AddStringToObject(j,"fw_version",s.u.status.version);
    cJSON_AddNumberToObject(j,"attempts_left",s.u.status.pin_attempts_left);
    cJSON_AddBoolToObject(j,"airgap",wifi_mgr_is_airgapped());
    cJSON_AddNumberToObject(j,"mode",wifi_mgr_get_mode());
    cJSON_AddBoolToObject(j,"sta_connected",wifi_mgr_sta_connected());
    cJSON *hw=cJSON_AddObjectToObject(j,"hardware");
    cJSON_AddNumberToObject(hw,"display",hc.display);
    cJSON_AddNumberToObject(hw,"confirm",hc.confirm_mode);
    return send_json(r,j);
}

// ---- /api/hal (GET/POST) ----
static esp_err_t h_hal_get(httpd_req_t *r){
    dv_hal_config_t c; hal_config_load(&c);
    cJSON *j=cJSON_CreateObject();
    cJSON_AddNumberToObject(j,"display",c.display);
    cJSON_AddNumberToObject(j,"i2c_sda",c.i2c_sda); cJSON_AddNumberToObject(j,"i2c_scl",c.i2c_scl);
    cJSON_AddNumberToObject(j,"i2c_addr",c.i2c_addr);
    cJSON_AddNumberToObject(j,"confirm",c.confirm_mode);
    cJSON_AddNumberToObject(j,"btn_ok",c.btn_ok); cJSON_AddNumberToObject(j,"btn_cancel",c.btn_cancel);
    cJSON_AddNumberToObject(j,"btn_up",c.btn_up); cJSON_AddNumberToObject(j,"btn_down",c.btn_down);
    cJSON_AddBoolToObject(j,"active_low",c.btn_active_low);
    return send_json(r,j);
}
static esp_err_t h_hal_post(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    dv_hal_config_t c; hal_config_load(&c);
    #define GI(k,f) do{cJSON*x=cJSON_GetObjectItem(j,k); if(cJSON_IsNumber(x))c.f=(typeof(c.f))x->valueint;}while(0)
    GI("display",display); GI("i2c_sda",i2c_sda); GI("i2c_scl",i2c_scl); GI("i2c_addr",i2c_addr);
    GI("confirm",confirm_mode); GI("btn_ok",btn_ok); GI("btn_cancel",btn_cancel);
    GI("btn_up",btn_up); GI("btn_down",btn_down);
    cJSON *al=cJSON_GetObjectItem(j,"active_low"); if(cJSON_IsBool(al))c.btn_active_low=cJSON_IsTrue(al);
    #undef GI
    char reason[64]; dv_err_t e=hal_config_save(&c);
    cJSON_Delete(j);
    if(e!=DV_OK){ hal_config_validate(&c,reason,sizeof(reason)); return send_err(r,"invalid_hal",reason); }
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}

// ---- provisioning ----
static esp_err_t h_provision_new(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    vault_req_t q={.kind=VREQ_PROVISION_NEW,.origin=VORIGIN_REMOTE_HTTP};
    cJSON *wc=cJSON_GetObjectItem(j,"word_count"); q.u.newseed.word_count=cJSON_IsNumber(wc)?wc->valueint:12;
    cJSON *pin=cJSON_GetObjectItem(j,"pin"); if(cJSON_IsString(pin)) strlcpy(q.u.newseed.pin,pin->valuestring,sizeof(q.u.newseed.pin));
    cJSON *pp=cJSON_GetObjectItem(j,"passphrase"); if(cJSON_IsString(pp)) strlcpy(q.u.newseed.passphrase,pp->valuestring,sizeof(q.u.newseed.passphrase));
    cJSON_Delete(j);
    vault_resp_t s; dv_err_t e=vault_ipc_request(&q,&s,15000);
    if(e!=DV_OK) return send_err(r,errname(e),"provision failed");
    cJSON *out=cJSON_CreateObject(); cJSON_AddStringToObject(out,"mnemonic",s.u.reveal.mnemonic);
    return send_json(r,out);
}
static esp_err_t h_provision_import(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    vault_req_t q={.kind=VREQ_PROVISION_IMPORT,.origin=VORIGIN_REMOTE_HTTP};
    cJSON *m=cJSON_GetObjectItem(j,"mnemonic"); if(cJSON_IsString(m))strlcpy(q.u.import.mnemonic,m->valuestring,sizeof(q.u.import.mnemonic));
    cJSON *pin=cJSON_GetObjectItem(j,"pin"); if(cJSON_IsString(pin))strlcpy(q.u.import.pin,pin->valuestring,sizeof(q.u.import.pin));
    cJSON *pp=cJSON_GetObjectItem(j,"passphrase"); if(cJSON_IsString(pp))strlcpy(q.u.import.passphrase,pp->valuestring,sizeof(q.u.import.passphrase));
    cJSON_Delete(j);
    vault_resp_t s; dv_err_t e=vault_ipc_request(&q,&s,15000);
    if(e!=DV_OK) return send_err(r,errname(e),"import failed");
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}
static esp_err_t h_provision_confirm(httpd_req_t *r){
    // The seed is already sealed at /new; this just acknowledges the user wrote
    // the words down. Nothing to persist server-side.
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}

// ---- unlock / lock ----
static esp_err_t h_unlock(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    vault_req_t q={.kind=VREQ_UNLOCK,.origin=VORIGIN_REMOTE_HTTP};
    cJSON *pin=cJSON_GetObjectItem(j,"pin"); if(cJSON_IsString(pin))strlcpy(q.u.unlock.pin,pin->valuestring,sizeof(q.u.unlock.pin));
    cJSON_Delete(j);
    vault_resp_t s; dv_err_t e=vault_ipc_request(&q,&s,15000);
    cJSON *out=cJSON_CreateObject();
    cJSON_AddBoolToObject(out,"ok",e==DV_OK);
    cJSON_AddNumberToObject(out,"attempts_left",s.u.status.pin_attempts_left);
    if(e!=DV_OK) cJSON_AddStringToObject(out,"error",errname(e));
    return send_json(r,out);
}
static esp_err_t h_lock(httpd_req_t *r){
    vault_req_t q={.kind=VREQ_LOCK,.origin=VORIGIN_REMOTE_HTTP}; vault_resp_t s;
    vault_ipc_request(&q,&s,3000);
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}

// ---- networks ----
static esp_err_t h_networks_get(httpd_req_t *r){
    cJSON *arr=cJSON_CreateArray();
    for(size_t i=0;i<DV_NET_MAX;i++){ dv_network_t n;
        if(net_config_get(i,&n)!=DV_OK) continue;
        cJSON *o=cJSON_CreateObject();
        cJSON_AddNumberToObject(o,"idx",i); cJSON_AddNumberToObject(o,"family",n.family);
        cJSON_AddNumberToObject(o,"evm_chain_id",n.evm_chain_id);
        cJSON_AddStringToObject(o,"name",n.name); cJSON_AddStringToObject(o,"rpc_url",n.rpc_url);
        cJSON_AddStringToObject(o,"symbol",n.symbol); cJSON_AddNumberToObject(o,"decimals",n.decimals);
        cJSON_AddStringToObject(o,"explorer",n.explorer);
        cJSON_AddItemToArray(arr,o);
    }
    cJSON *j=cJSON_CreateObject(); cJSON_AddItemToObject(j,"networks",arr); return send_json(r,j);
}
static esp_err_t h_networks_post(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    dv_network_t n; memset(&n,0,sizeof(n)); n.used=true;
    cJSON *x;
    if((x=cJSON_GetObjectItem(j,"family"))){ n.family=cJSON_IsString(x)?family_from_str(x->valuestring):(dv_chain_t)x->valueint; }
    if((x=cJSON_GetObjectItem(j,"evm_chain_id"))&&cJSON_IsNumber(x)) n.evm_chain_id=(uint64_t)x->valuedouble;
    if((x=cJSON_GetObjectItem(j,"name"))&&cJSON_IsString(x)) strlcpy(n.name,x->valuestring,sizeof(n.name));
    if((x=cJSON_GetObjectItem(j,"rpc_url"))&&cJSON_IsString(x)) strlcpy(n.rpc_url,x->valuestring,sizeof(n.rpc_url));
    if((x=cJSON_GetObjectItem(j,"symbol"))&&cJSON_IsString(x)) strlcpy(n.symbol,x->valuestring,sizeof(n.symbol));
    if((x=cJSON_GetObjectItem(j,"decimals"))&&cJSON_IsNumber(x)) n.decimals=x->valueint;
    if((x=cJSON_GetObjectItem(j,"explorer"))&&cJSON_IsString(x)) strlcpy(n.explorer,x->valuestring,sizeof(n.explorer));
    cJSON_Delete(j);
    dv_err_t e=net_config_add(&n);
    if(e!=DV_OK) return send_err(r,"add_failed",NULL);
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}
static esp_err_t h_networks_delete(httpd_req_t *r){
    // URI: /api/networks/{idx}
    const char *p=strrchr(r->uri,'/'); size_t idx = p?atoi(p+1):999;
    dv_err_t e=net_config_remove(idx);
    if(e!=DV_OK) return send_err(r,"del_failed",NULL);
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}

// ---- accounts / balance ----
static esp_err_t h_accounts(httpd_req_t *r){
    char q[64]; size_t ni=0;
    if(httpd_req_get_url_query_str(r,q,sizeof(q))==ESP_OK){ char v[16];
        if(httpd_query_key_value(q,"net_idx",v,sizeof(v))==ESP_OK) ni=atoi(v); }
    dv_network_t n; if(net_config_get(ni,&n)!=DV_OK) return send_err(r,"bad_net",NULL);
    vault_req_t vq={.kind=VREQ_DERIVE_ACCOUNT,.origin=VORIGIN_REMOTE_HTTP,.chain=n.family,.evm_chain_id=n.evm_chain_id};
    chains_default_path(n.family,0,0,&vq.path);
    vault_resp_t s; dv_err_t e=vault_ipc_request(&vq,&s,10000);
    if(e!=DV_OK) return send_err(r,errname(e),"derive failed (unlock?)");
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"address",s.u.account.address);
    cJSON_AddStringToObject(j,"symbol",n.symbol);
    return send_json(r,j);
}
static esp_err_t h_balance(httpd_req_t *r){
    char q[160]; size_t ni=0; char addr[96]="";
    if(httpd_req_get_url_query_str(r,q,sizeof(q))==ESP_OK){ char v[16];
        if(httpd_query_key_value(q,"net_idx",v,sizeof(v))==ESP_OK) ni=atoi(v);
        httpd_query_key_value(q,"address",addr,sizeof(addr)); }
    dv_network_t n; if(net_config_get(ni,&n)!=DV_OK) return send_err(r,"bad_net",NULL);
    char bal[80]="0";
    if(rpc_get_balance(&n,addr,bal,sizeof(bal))!=DV_OK) return send_err(r,"rpc_failed","balance query failed");
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"balance",bal);
    cJSON_AddNumberToObject(j,"decimals",n.decimals); cJSON_AddStringToObject(j,"symbol",n.symbol);
    return send_json(r,j);
}

// Build a dv_unsigned_tx_t from a JSON body + network. Returns 0 on success.
static int build_unsigned(cJSON *j,dv_network_t *n,dv_unsigned_tx_t *tx){
    memset(tx,0,sizeof(*tx));
    tx->chain=n->family; tx->evm_chain_id=n->evm_chain_id;
    cJSON *x;
    if((x=cJSON_GetObjectItem(j,"to"))&&cJSON_IsString(x)) strlcpy(tx->to,x->valuestring,sizeof(tx->to));
    if((x=cJSON_GetObjectItem(j,"amount"))&&cJSON_IsString(x)) tx->value_len=dec_to_be(x->valuestring,tx->value);
    if((x=cJSON_GetObjectItem(j,"token_contract"))&&cJSON_IsString(x)&&x->valuestring[0])
        strlcpy(tx->token_contract,x->valuestring,sizeof(tx->token_contract));
    if((x=cJSON_GetObjectItem(j,"nonce"))&&cJSON_IsNumber(x)) tx->nonce=(uint64_t)x->valuedouble;
    if((x=cJSON_GetObjectItem(j,"eip1559"))&&cJSON_IsBool(x)) tx->eip1559=cJSON_IsTrue(x);
    return tx->to[0]?0:-1;
}

// ---- tx build (returns a review) ----
static esp_err_t h_tx_build(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    size_t ni=0; cJSON *x=cJSON_GetObjectItem(j,"net_idx"); if(cJSON_IsNumber(x))ni=x->valueint;
    dv_network_t n; if(net_config_get(ni,&n)!=DV_OK){cJSON_Delete(j);return send_err(r,"bad_net",NULL);}
    dv_unsigned_tx_t tx;
    if(build_unsigned(j,&n,&tx)){cJSON_Delete(j);return send_err(r,"bad_tx",NULL);}
    cJSON_Delete(j);
    rpc_prepare_tx(&n,&tx);
    const dv_chain_ops_t *ops=chains_get(n.family);
    dv_tx_review_t rev; memset(&rev,0,sizeof(rev));
    if(!ops||ops->describe(&tx,&rev)!=DV_OK) return send_err(r,"describe_failed",NULL);
    cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"to",rev.to); cJSON_AddStringToObject(o,"amount",rev.amount);
    cJSON_AddStringToObject(o,"symbol",rev.symbol); cJSON_AddStringToObject(o,"fee",rev.fee);
    cJSON_AddNumberToObject(o,"nonce",rev.nonce); cJSON_AddNumberToObject(o,"net_idx",ni);
    return send_json(r,o);
}

// ---- tx sign (requires physical confirmation in the vault) ----
static esp_err_t do_sign(httpd_req_t *r,bool broadcast){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    size_t ni=0; cJSON *x=cJSON_GetObjectItem(j,"net_idx"); if(cJSON_IsNumber(x))ni=x->valueint;
    dv_network_t n; if(net_config_get(ni,&n)!=DV_OK){cJSON_Delete(j);return send_err(r,"bad_net",NULL);}
    vault_req_t q={.kind=VREQ_SIGN_TX,.origin=VORIGIN_REMOTE_HTTP,.chain=n.family,.evm_chain_id=n.evm_chain_id};
    chains_default_path(n.family,0,0,&q.path);
    if(build_unsigned(j,&n,&q.u.tx)){cJSON_Delete(j);return send_err(r,"bad_tx",NULL);}
    cJSON_Delete(j);
    rpc_prepare_tx(&n,&q.u.tx);
    vault_resp_t s; dv_err_t e=vault_ipc_request(&q,&s,120000); // wait for user confirm
    if(e!=DV_OK) return send_err(r,errname(e),"sign failed / rejected");
    cJSON *o=cJSON_CreateObject();
    char hex[2100]; hex[0]=0; size_t L=s.u.signed_result.signed_tx_len;
    for(size_t i=0;i<L&&i*2<sizeof(hex)-2;i++) sprintf(hex+i*2,"%02x",s.u.signed_result.signed_tx[i]);
    cJSON_AddStringToObject(o,"signed_hex",hex);
    if(broadcast){ char txid[128]="";
        if(rpc_broadcast(&n,s.u.signed_result.signed_tx,L,txid,sizeof(txid))==DV_OK)
            cJSON_AddStringToObject(o,"txid",txid);
        else cJSON_AddStringToObject(o,"warning","signed ok but broadcast unsupported for this chain"); }
    return send_json(r,o);
}
static esp_err_t h_tx_sign(httpd_req_t *r){ return do_sign(r,false); }
static esp_err_t h_tx_broadcast(httpd_req_t *r){ return do_sign(r,true); }
// Offline signing = same as sign but we never broadcast and expect radio off.
static esp_err_t h_offline_sign(httpd_req_t *r){ return do_sign(r,false); }

// ---- wifi ----
static esp_err_t h_wifi_get(httpd_req_t *r){
    dv_wifi_cfg_t c; wifi_mgr_load_cfg(&c);
    cJSON *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"ap_ssid",c.ap_ssid);
    cJSON_AddStringToObject(j,"sta_ssid",c.sta_ssid);
    cJSON_AddBoolToObject(j,"sta_updates_only",c.sta_updates_only);
    cJSON_AddNumberToObject(j,"mode",wifi_mgr_get_mode());
    return send_json(r,j);
}
static esp_err_t h_wifi_post(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    // Air-gap toggle.
    cJSON *ag=cJSON_GetObjectItem(j,"airgap");
    if(cJSON_IsBool(ag)){ wifi_mgr_set_mode(cJSON_IsTrue(ag)?DV_WIFI_OFF:DV_WIFI_AP);
        cJSON_Delete(j); cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok); }
    dv_wifi_cfg_t c; wifi_mgr_load_cfg(&c); cJSON *x;
    if((x=cJSON_GetObjectItem(j,"ap_ssid"))&&cJSON_IsString(x)) strlcpy(c.ap_ssid,x->valuestring,sizeof(c.ap_ssid));
    if((x=cJSON_GetObjectItem(j,"ap_pass"))&&cJSON_IsString(x)) strlcpy(c.ap_pass,x->valuestring,sizeof(c.ap_pass));
    if((x=cJSON_GetObjectItem(j,"sta_ssid"))&&cJSON_IsString(x)) strlcpy(c.sta_ssid,x->valuestring,sizeof(c.sta_ssid));
    if((x=cJSON_GetObjectItem(j,"sta_pass"))&&cJSON_IsString(x)) strlcpy(c.sta_pass,x->valuestring,sizeof(c.sta_pass));
    if((x=cJSON_GetObjectItem(j,"sta_updates_only"))&&cJSON_IsBool(x)) c.sta_updates_only=cJSON_IsTrue(x);
    cJSON *conn=cJSON_GetObjectItem(j,"connect_sta");
    cJSON_Delete(j);
    wifi_mgr_save_cfg(&c);
    if(cJSON_IsTrue(conn)) wifi_mgr_set_mode(DV_WIFI_APSTA);
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true); return send_json(r,ok);
}

// ---- ota ----
static esp_err_t h_ota_check(httpd_req_t *r){
    dv_ota_status_t st; ota_check(NULL,&st);
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"current",st.current_version);
    cJSON_AddBoolToObject(j,"update_available",st.update_available);
    return send_json(r,j);
}
static esp_err_t h_ota_apply(httpd_req_t *r){
    char *b=read_body(r); if(!b) return send_err(r,"bad_body",NULL);
    cJSON *j=cJSON_Parse(b); free(b); if(!j) return send_err(r,"bad_json",NULL);
    cJSON *url=cJSON_GetObjectItem(j,"url");
    char image[200]=""; if(cJSON_IsString(url)) strlcpy(image,url->valuestring,sizeof(image));
    cJSON_Delete(j);
    if(!image[0]) return send_err(r,"no_url",NULL);
    dv_err_t e=ota_apply(image,NULL);
    if(e!=DV_OK) return send_err(r,"ota_failed","download/verify failed");
    cJSON *ok=cJSON_CreateObject(); cJSON_AddBoolToObject(ok,"ok",true);
    send_json(r,ok);
    ota_activate_and_reboot();
    return ESP_OK;
}

// ---- static files from SPIFFS ----
static esp_err_t h_static(httpd_req_t *r){
    const char *uri=r->uri; char path[300];
    if(!strcmp(uri,"/")) uri="/index.html";
    snprintf(path,sizeof(path),"/spiffs%s",uri);
    FILE *f=fopen(path,"r"); if(!f){ // SPA fallback
        f=fopen("/spiffs/index.html","r"); if(!f){ httpd_resp_send_404(r); return ESP_OK; } }
    const char *ct="text/plain";
    if(strstr(uri,".html"))ct="text/html"; else if(strstr(uri,".js"))ct="application/javascript";
    else if(strstr(uri,".css"))ct="text/css";
    httpd_resp_set_type(r,ct);
    // Assets are pre-gzipped at build time.
    if(strstr(uri,".js")||strstr(uri,".css")||strstr(uri,".html"))
        httpd_resp_set_hdr(r,"Content-Encoding","identity");
    char buf[1024]; size_t k;
    while((k=fread(buf,1,sizeof(buf),f))>0) httpd_resp_send_chunk(r,buf,k);
    fclose(f); httpd_resp_send_chunk(r,NULL,0); return ESP_OK;
}

static void reg(const char *uri,httpd_method_t m,esp_err_t(*h)(httpd_req_t*)){
    httpd_uri_t u={.uri=uri,.method=m,.handler=h}; httpd_register_uri_handler(s_srv,&u);
}

dv_err_t web_server_start(void){
    if(s_srv) return DV_OK;
    httpd_config_t c=HTTPD_DEFAULT_CONFIG();
    c.uri_match_fn=httpd_uri_match_wildcard; c.max_uri_handlers=32; c.stack_size=8192;
    if(httpd_start(&s_srv,&c)!=ESP_OK) return DV_ERR;
    reg("/api/status",HTTP_GET,h_status);
    reg("/api/hal",HTTP_GET,h_hal_get);        reg("/api/hal",HTTP_POST,h_hal_post);
    reg("/api/provision/new",HTTP_POST,h_provision_new);
    reg("/api/provision/import",HTTP_POST,h_provision_import);
    reg("/api/provision/confirm",HTTP_POST,h_provision_confirm);
    reg("/api/unlock",HTTP_POST,h_unlock);     reg("/api/lock",HTTP_POST,h_lock);
    reg("/api/networks",HTTP_GET,h_networks_get); reg("/api/networks",HTTP_POST,h_networks_post);
    reg("/api/networks/*",HTTP_DELETE,h_networks_delete);
    reg("/api/accounts",HTTP_GET,h_accounts);  reg("/api/balance",HTTP_GET,h_balance);
    reg("/api/tx/build",HTTP_POST,h_tx_build); reg("/api/tx/sign",HTTP_POST,h_tx_sign);
    reg("/api/tx/broadcast",HTTP_POST,h_tx_broadcast);
    reg("/api/offline/sign",HTTP_POST,h_offline_sign);
    reg("/api/wifi",HTTP_GET,h_wifi_get);      reg("/api/wifi",HTTP_POST,h_wifi_post);
    reg("/api/ota/check",HTTP_GET,h_ota_check);reg("/api/ota/apply",HTTP_POST,h_ota_apply);
    reg("/*",HTTP_GET,h_static);
    ESP_LOGI(TAG,"web server up");
    return DV_OK;
}
dv_err_t web_server_stop(void){ if(s_srv){httpd_stop(s_srv);s_srv=NULL;} return DV_OK; }
