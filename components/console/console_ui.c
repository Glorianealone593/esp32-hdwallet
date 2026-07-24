// DibaVault — serial/USB console (LOCAL origin). GPL-3.0. dibachain.
// Physically-attached interface: may reveal the mnemonic (with PIN) and factory
// reset — things the remote web UI can never do.
#include "console_ui.h"
#include "vault_ipc.h"
#include "net_config.h"
#include "chains.h"
#include <string.h>
#include <stdio.h>
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "linenoise/linenoise.h"

static const char *TAG="console";

static int cmd_status(int argc,char**argv){
    vault_req_t q={.kind=VREQ_GET_STATUS,.origin=VORIGIN_LOCAL_CONSOLE}; vault_resp_t s;
    vault_ipc_request(&q,&s,3000);
    printf("DibaVault %s\n provisioned=%d locked=%d attempts_left=%d\n",
           s.u.status.version,s.u.status.provisioned,s.u.status.locked,s.u.status.pin_attempts_left);
    return 0;
}

static struct { struct arg_int *words; struct arg_str *pin; struct arg_end *end; } new_args;
static int cmd_setup_new(int argc,char**argv){
    int e=arg_parse(argc,argv,(void**)&new_args); if(e){arg_print_errors(stdout,new_args.end,argv[0]);return 1;}
    vault_req_t q={.kind=VREQ_PROVISION_NEW,.origin=VORIGIN_LOCAL_CONSOLE};
    q.u.newseed.word_count=new_args.words->count?new_args.words->ival[0]:12;
    strlcpy(q.u.newseed.pin,new_args.pin->sval[0],sizeof(q.u.newseed.pin));
    vault_resp_t s; dv_err_t r=vault_ipc_request(&q,&s,15000);
    if(r!=DV_OK){ printf("error %d\n",r); return 1; }
    printf("\n=== WRITE THESE %d WORDS DOWN (shown once) ===\n%s\n=== dibachain cannot recover them ===\n\n",
           q.u.newseed.word_count,s.u.reveal.mnemonic);
    return 0;
}

static struct { struct arg_str *pin; struct arg_end *end; } pin_args;
static int cmd_unlock(int argc,char**argv){
    if(arg_parse(argc,argv,(void**)&pin_args)){arg_print_errors(stdout,pin_args.end,argv[0]);return 1;}
    vault_req_t q={.kind=VREQ_UNLOCK,.origin=VORIGIN_LOCAL_CONSOLE};
    strlcpy(q.u.unlock.pin,pin_args.pin->sval[0],sizeof(q.u.unlock.pin));
    vault_resp_t s; dv_err_t r=vault_ipc_request(&q,&s,15000);
    printf(r==DV_OK?"unlocked\n":"failed (attempts left %d)\n",s.u.status.pin_attempts_left);
    return 0;
}
static int cmd_lock(int argc,char**argv){
    vault_req_t q={.kind=VREQ_LOCK,.origin=VORIGIN_LOCAL_CONSOLE}; vault_resp_t s;
    vault_ipc_request(&q,&s,3000); printf("locked\n"); return 0;
}
static int cmd_reveal(int argc,char**argv){
    if(arg_parse(argc,argv,(void**)&pin_args)){arg_print_errors(stdout,pin_args.end,argv[0]);return 1;}
    vault_req_t q={.kind=VREQ_REVEAL_MNEMONIC,.origin=VORIGIN_LOCAL_CONSOLE};
    strlcpy(q.u.unlock.pin,pin_args.pin->sval[0],sizeof(q.u.unlock.pin));
    vault_resp_t s; dv_err_t r=vault_ipc_request(&q,&s,10000);
    if(r!=DV_OK){ printf("error %d\n",r); return 1; }
    printf("mnemonic: %s\n",s.u.reveal.mnemonic);
    return 0;
}
static int cmd_addresses(int argc,char**argv){
    vault_req_t q={.kind=VREQ_LIST_ACCOUNTS,.origin=VORIGIN_LOCAL_CONSOLE}; vault_resp_t s;
    dv_err_t r=vault_ipc_request(&q,&s,10000);
    if(r!=DV_OK){ printf("locked? error %d\n",r); return 1; }
    const char *names[]={"BTC","EVM","TRX","SOL"};
    for(size_t i=0;i<s.u.list.count;i++)
        printf(" %-4s %s\n",names[s.u.list.items[i].chain],s.u.list.items[i].address);
    return 0;
}
static int cmd_networks(int argc,char**argv){
    for(size_t i=0;i<32;i++){ dv_network_t n; if(net_config_get(i,&n)!=DV_OK)continue;
        printf(" [%d] %-16s %s\n",(int)i,n.name,n.rpc_url); }
    return 0;
}
static int cmd_wipe(int argc,char**argv){
    printf("Type 'WIPE' to factory reset: ");
    char line[16]={0}; if(fgets(line,sizeof(line),stdin)&&strncmp(line,"WIPE",4)==0){
        // Wipe is a keystore-local op; expose via a dedicated local request would
        // be ideal. For now, guide the user to re-flash / erase_flash.
        printf("Run 'idf.py erase-flash' or hold-reset per docs to fully wipe.\n");
    } else printf("cancelled\n");
    return 0;
}

dv_err_t console_ui_start(void){
    esp_console_repl_t *repl=NULL;
    esp_console_repl_config_t rc=ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt="dibavault>"; rc.max_cmdline_length=512;
    esp_console_dev_uart_config_t uc=ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if(esp_console_new_repl_uart(&uc,&rc,&repl)!=ESP_OK) return DV_ERR;

    new_args.words=arg_int0("w","words","<12|18|24>","word count"); new_args.pin=arg_str1(NULL,NULL,"<pin>","PIN"); new_args.end=arg_end(2);
    pin_args.pin=arg_str1(NULL,NULL,"<pin>","PIN"); pin_args.end=arg_end(2);

    const esp_console_cmd_t cmds[]={
        {"status","show wallet status",NULL,cmd_status,NULL},
        {"setup-new","create a new wallet: setup-new [-w 12] <pin>",NULL,cmd_setup_new,&new_args},
        {"unlock","unlock: unlock <pin>",NULL,cmd_unlock,&pin_args},
        {"lock","lock the vault",NULL,cmd_lock,NULL},
        {"reveal","show seed words: reveal <pin>",NULL,cmd_reveal,&pin_args},
        {"addresses","list receive addresses",NULL,cmd_addresses,NULL},
        {"networks","list configured networks",NULL,cmd_networks,NULL},
        {"wipe","factory reset",NULL,cmd_wipe,NULL},
    };
    for(size_t i=0;i<sizeof(cmds)/sizeof(cmds[0]);i++) esp_console_cmd_register(&cmds[i]);
    esp_console_register_help_command();
    esp_console_start_repl(repl);
    ESP_LOGI(TAG,"console REPL started");
    return DV_OK;
}
