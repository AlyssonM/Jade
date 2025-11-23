#ifndef AMALGAMATED_BUILD
#include "../gui.h"
#include "../jade_assert.h"
#include "../process.h"
#include "../ui.h"
#include "../utils/cbor_rpc.h"
#include "../wallet.h"

#include "process_utils.h"

#include <esp_event.h>

bool show_confirm_address_activity(const char* address, bool default_selection);
bool show_confirm_address_with_qr_activity(const char* address, bool default_selection);

static void build_eth_bip44_path(size_t account, size_t change, size_t pointer, uint32_t* path, size_t max, size_t* written)
{
    JADE_ASSERT(path);
    JADE_ASSERT(max >= 5);
    JADE_ASSERT(written);
    path[0] = BIP32_INITIAL_HARDENED_CHILD + 44;
    path[1] = BIP32_INITIAL_HARDENED_CHILD + 60;
    path[2] = BIP32_INITIAL_HARDENED_CHILD + (uint32_t)account;
    path[3] = (uint32_t)change;
    path[4] = (uint32_t)pointer;
    *written = 5;
}

void get_evm_address_process(void* process_ptr)
{
    jade_process_t* process = process_ptr;

    ASSERT_CURRENT_MESSAGE(process, "get_evm_address");
    ASSERT_KEYCHAIN_UNLOCKED_BY_MESSAGE_SOURCE(process);
    GET_MSG_PARAMS(process);

    uint32_t path[16];
    size_t path_len = 0;
    const size_t max_path_len = sizeof(path) / sizeof(path[0]);

    if (rpc_has_field_data("path", &params)) {
        rpc_get_bip32_path("path", &params, path, max_path_len, &path_len);
        if (path_len != 5) {
            jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Invalid BIP44 path length for EVM");
            goto cleanup;
        }
    } else {
        size_t account = 0, change = 0, pointer = 0;
        if (!rpc_get_sizet("account", &params, &account) || !rpc_get_sizet("change", &params, &change)
            || !rpc_get_sizet("pointer", &params, &pointer)) {
            jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Missing account/change/pointer");
            goto cleanup;
        }
        build_eth_bip44_path(account, change, pointer, path, max_path_len, &path_len);
    }

    char address[64];
    if (!wallet_get_evm_address(path, path_len, address, sizeof(address))) {
        jade_process_reject_message(process, CBOR_RPC_INTERNAL_ERROR, "Failed to derive EVM address");
        goto cleanup;
    }

    const bool default_selection = false;
    if (!show_confirm_address_with_qr_activity(address, default_selection)) {
        jade_process_reject_message(process, CBOR_RPC_USER_CANCELLED, "User declined to confirm address");
        goto cleanup;
    }

    uint8_t buf[256];
    jade_process_reply_to_message_result(process->ctx, buf, sizeof(buf), address, cbor_result_string_cb);

cleanup:
    return;
}
#endif // AMALGAMATED_BUILD