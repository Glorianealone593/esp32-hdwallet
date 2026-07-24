// DibaVault — Vault IPC task (SECURE CORE). GPL-3.0. dibachain.
//
// This is the ONLY code that both the untrusted connectivity layer and the
// secret-holding keystore touch. It runs on its own task and enforces the
// origin-based policy described in vault_ipc.h.
#include "vault_ipc.h"
#include "keystore.h"
#include "signer.h"
#include "chains.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "memzero.h"

static const char *TAG = "vault";

typedef struct {
    vault_req_t   req;
    QueueHandle_t reply;   // caller's one-shot reply queue
} vault_msg_t;

static QueueHandle_t s_reqq = NULL;

static bool is_local(vreq_origin_t o) {
    return o == VORIGIN_LOCAL_CONSOLE || o == VORIGIN_LOCAL_DISPLAY;
}

// Fill the address string for an account using the chain layer.
static void fill_address(dv_account_pub_t *a) {
    const dv_chain_ops_t *ops = chains_get(a->chain);
    if (ops && ops->format_address)
        ops->format_address(a->pubkey, a->pubkey_len, a->evm_chain_id,
                            a->address, sizeof(a->address));
}

static void handle(const vault_req_t *req, vault_resp_t *resp) {
    memset(resp, 0, sizeof(*resp));
    bool remote = (req->origin == VORIGIN_REMOTE_HTTP);

    switch (req->kind) {
    case VREQ_GET_STATUS:
        resp->u.status.provisioned = keystore_is_provisioned();
        resp->u.status.locked = !keystore_is_unlocked();
        resp->u.status.pin_attempts_left = keystore_pin_attempts_left();
        strlcpy(resp->u.status.version, DIBAVAULT_VERSION, sizeof(resp->u.status.version));
        resp->err = DV_OK;
        return;

    case VREQ_UNLOCK:
        resp->err = keystore_unlock(req->u.unlock.pin);
        resp->u.status.pin_attempts_left = keystore_pin_attempts_left();
        return;

    case VREQ_LOCK:
        keystore_lock();
        resp->err = DV_OK;
        return;

    case VREQ_DERIVE_ACCOUNT: {
        resp->err = keystore_derive_public(req->chain, &req->path,
                                           req->evm_chain_id, &resp->u.account);
        if (resp->err == DV_OK) fill_address(&resp->u.account);
        return;
    }

    case VREQ_LIST_ACCOUNTS: {
        // Derive index-0 account for each supported chain.
        size_t n = 0;
        for (dv_chain_t c = 0; c < DV_CHAIN__COUNT && n < 8; c++) {
            dv_path_t p;
            if (chains_default_path(c, 0, 0, &p) != DV_OK) continue;
            uint64_t cid = (c == DV_CHAIN_EVM) ? 1 : 0;
            if (keystore_derive_public(c, &p, cid, &resp->u.list.items[n]) == DV_OK) {
                fill_address(&resp->u.list.items[n]);
                n++;
            }
        }
        resp->u.list.count = n;
        resp->err = n ? DV_OK : DV_ERR_LOCKED;
        return;
    }

    case VREQ_SIGN_TX:
        resp->err = signer_sign_tx(&req->u.tx, &req->path, remote, resp);
        return;

    case VREQ_SIGN_MESSAGE:
        resp->err = signer_sign_message(req->chain, &req->path,
                                        req->u.msg.data, req->u.msg.len,
                                        req->u.msg.is_personal, remote, resp);
        return;

    // ---- provisioning: policy-gated ----
    case VREQ_PROVISION_NEW: {
        // Allowed from any origin ONLY while the device is unprovisioned. Once a
        // seed exists, HTTP can never trigger creation/overwrite.
        if (keystore_is_provisioned()) { resp->err = DV_ERR_ALREADY_PROVISIONED; return; }
        resp->err = keystore_provision_new(req->u.newseed.word_count,
                                           req->u.newseed.pin,
                                           req->u.newseed.passphrase[0] ? req->u.newseed.passphrase : NULL,
                                           resp->u.reveal.mnemonic,
                                           sizeof(resp->u.reveal.mnemonic));
        // The freshly generated words ARE returned here (first-run setup only).
        return;
    }

    case VREQ_PROVISION_IMPORT: {
        if (keystore_is_provisioned()) { resp->err = DV_ERR_ALREADY_PROVISIONED; return; }
        resp->err = keystore_provision_import(req->u.import.mnemonic,
                                              req->u.import.pin,
                                              req->u.import.passphrase[0] ? req->u.import.passphrase : NULL);
        return;
    }

    case VREQ_REVEAL_MNEMONIC:
        // HARD BOUNDARY: never reveal words to a remote (HTTP) caller.
        if (remote) { resp->err = DV_ERR_BOUNDARY; return; }
        resp->err = keystore_reveal_mnemonic(req->u.unlock.pin,
                                             resp->u.reveal.mnemonic,
                                             sizeof(resp->u.reveal.mnemonic));
        return;

    default:
        resp->err = DV_ERR_INVALID_ARG;
        return;
    }
}

static void vault_task(void *arg) {
    (void)arg;
    keystore_init();
    ESP_LOGI(TAG, "vault task up. provisioned=%d", keystore_is_provisioned());
    vault_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_reqq, &msg, portMAX_DELAY) != pdTRUE) continue;
        vault_resp_t resp;
        handle(&msg.req, &resp);
        if (msg.reply) xQueueSend(msg.reply, &resp, portMAX_DELAY);
        // Scrub any PINs/mnemonics that passed through the request copy.
        memzero(&msg, sizeof(msg));
        memzero(&resp, sizeof(resp));
    }
}

dv_err_t vault_ipc_start(void) {
    if (s_reqq) return DV_OK;
    s_reqq = xQueueCreate(4, sizeof(vault_msg_t));
    if (!s_reqq) return DV_ERR_NO_MEM;
    // Pin the vault to core 1 where available (keeps it off the WiFi/LWIP core 0).
    BaseType_t ok = xTaskCreatePinnedToCore(vault_task, "vault", 12288, NULL,
                                            configMAX_PRIORITIES - 3, NULL,
                                            (portNUM_PROCESSORS > 1) ? 1 : 0);
    return (ok == pdPASS) ? DV_OK : DV_ERR_NO_MEM;
}

dv_err_t vault_ipc_request(const vault_req_t *req, vault_resp_t *resp,
                           uint32_t timeout_ms) {
    if (!s_reqq) return DV_ERR;
    // Per-call reply queue keeps callers isolated from each other.
    QueueHandle_t reply = xQueueCreate(1, sizeof(vault_resp_t));
    if (!reply) return DV_ERR_NO_MEM;
    vault_msg_t msg;
    memcpy(&msg.req, req, sizeof(msg.req));
    msg.reply = reply;

    dv_err_t rc = DV_ERR_TIMEOUT;
    if (xQueueSend(s_reqq, &msg, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        if (xQueueReceive(reply, resp, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
            rc = resp->err;
    }
    memzero(&msg, sizeof(msg));
    vQueueDelete(reply);
    return rc;
}
