#ifndef AMALGAMATED_BUILD
#include "../jade_assert.h"
#include "../process.h"
#include "../utils/cbor_rpc.h"
#include "../wallet.h"
#include "../utils/shake256.h"

#include "process_utils.h"

#include <wally_core.h>

static void write_text_result(jade_process_t* process, const char* text)
{
    uint8_t buf[512];
    jade_process_reply_to_message_result(process->ctx, buf, sizeof(buf), text, cbor_result_string_cb);
}

static bool hex_to_bytes(const char* hex, size_t hex_len, uint8_t** out, size_t* out_len)
{
    JADE_ASSERT(out);
    JADE_ASSERT(out_len);
    if (!hex || hex_len == 0) {
        return false;
    }
    size_t written = 0;
    *out = JADE_MALLOC(hex_len / 2 + 1);
    if (wally_hex_to_bytes(hex, *out, hex_len / 2 + 1, &written) != WALLY_OK || written * 2 != hex_len) {
        free(*out);
        *out = NULL;
        return false;
    }
    *out_len = written;
    return true;
}

// Minimal RLP helpers
static void rlp_write_len(uint8_t prefix_base, size_t len, uint8_t** out, size_t* out_len)
{
    if (len < 56) {
        (*out)[(*out_len)++] = prefix_base + (uint8_t)len;
    } else {
        uint8_t tmp[8];
        size_t n = 0;
        size_t v = len;
        while (v) {
            tmp[n++] = (uint8_t)(v & 0xFF);
            v >>= 8;
        }
        (*out)[(*out_len)++] = prefix_base + 55 + (uint8_t)n;
        for (size_t i = 0; i < n; ++i) {
            (*out)[(*out_len)++] = tmp[n - 1 - i];
        }
    }
}

static void rlp_encode_bytes(const uint8_t* bytes, size_t len, uint8_t** out, size_t* out_len)
{
    if (len == 1 && bytes[0] < 0x80) {
        (*out)[(*out_len)++] = bytes[0];
        return;
    }
    rlp_write_len(0x80, len, out, out_len);
    memcpy(*out + *out_len, bytes, len);
    *out_len += len;
}

static void rlp_encode_uint64(uint64_t v, uint8_t** out, size_t* out_len)
{
    if (v == 0) {
        uint8_t z = 0x80;
        (*out)[(*out_len)++] = z;
        return;
    }
    uint8_t buf[8];
    size_t n = 0;
    while (v) {
        buf[n++] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
    uint8_t be[8];
    for (size_t i = 0; i < n; ++i) be[i] = buf[n - 1 - i];
    rlp_encode_bytes(be, n, out, out_len);
}

static void rlp_start_list(size_t payload_len, uint8_t** out, size_t* out_len)
{
    rlp_write_len(0xC0, payload_len, out, out_len);
}

// Build typed tx (0x02) payload list without yParity/signature
static bool build_eip1559_payload(uint64_t chainId, uint64_t nonce, uint64_t maxPriorityFeePerGas,
    uint64_t maxFeePerGas, uint64_t gasLimit, const uint8_t* to20, const uint8_t* value_be, size_t value_len,
    const uint8_t* data, size_t data_len, uint8_t** out, size_t* out_len)
{
    size_t cap = 1024 + data_len;
    *out = JADE_MALLOC(cap);
    *out_len = 0;

    uint8_t* list = JADE_MALLOC(cap);
    size_t list_len = 0;

    rlp_encode_uint64(chainId, &list, &list_len);
    rlp_encode_uint64(nonce, &list, &list_len);
    rlp_encode_uint64(maxPriorityFeePerGas, &list, &list_len);
    rlp_encode_uint64(maxFeePerGas, &list, &list_len);
    rlp_encode_uint64(gasLimit, &list, &list_len);
    rlp_encode_bytes(to20, 20, &list, &list_len);
    rlp_encode_bytes(value_be, value_len, &list, &list_len);
    rlp_encode_bytes(data, data_len, &list, &list_len);
    list[list_len++] = 0xC0;

    rlp_start_list(list_len, out, out_len);
    memcpy(*out + *out_len, list, list_len);
    *out_len += list_len;
    free(list);
    return true;
}

void sign_evm_tx_process(void* process_ptr)
{
    jade_process_t* process = process_ptr;

    ASSERT_CURRENT_MESSAGE(process, "sign_evm_tx");
    ASSERT_KEYCHAIN_UNLOCKED_BY_MESSAGE_SOURCE(process);
    GET_MSG_PARAMS(process);

    uint32_t path[16];
    size_t path_len = 0;
    const size_t max_path_len = sizeof(path) / sizeof(path[0]);
    rpc_get_bip32_path("path", &params, path, max_path_len, &path_len);
    if (path_len != 5) {
        jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Invalid BIP44 path length for EVM");
        goto cleanup;
    }

    uint64_t chainId = 1, nonce = 0, maxPriorityFeePerGas = 0, maxFeePerGas = 0, gasLimit = 0;
    rpc_get_uint64_t("chain_id", &params, &chainId);
    if (!(chainId == 1 || chainId == 137 || chainId == 42161)) {
        jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Unsupported chain_id");
        goto cleanup;
    }
    rpc_get_uint64_t("nonce", &params, &nonce);
    rpc_get_uint64_t("max_priority_fee_per_gas", &params, &maxPriorityFeePerGas);
    rpc_get_uint64_t("max_fee_per_gas", &params, &maxFeePerGas);
    rpc_get_uint64_t("gas_limit", &params, &gasLimit);

    const char* to_hex = NULL; size_t to_len = 0;
    rpc_get_string_ptr("to", &params, &to_hex, &to_len);
    if (!to_hex || to_len != 40) {
        jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Invalid 'to' address hex");
        goto cleanup;
    }
    uint8_t* to_bytes = NULL; size_t to_bytes_len = 0;
    if (!hex_to_bytes(to_hex, to_len, &to_bytes, &to_bytes_len) || to_bytes_len != 20) {
        jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Invalid 'to' address bytes");
        goto cleanup;
    }

    const char* value_hex = NULL; size_t value_len = 0;
    rpc_get_string_ptr("value", &params, &value_hex, &value_len);
    if (!value_hex) { value_hex = ""; value_len = 0; }
    uint8_t* value_bytes = NULL; size_t value_bytes_len = 0;
    if (value_len && !hex_to_bytes(value_hex, value_len, &value_bytes, &value_bytes_len)) {
        jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Invalid 'value' hex");
        goto cleanup;
    }

    const char* data_hex = NULL; size_t data_len = 0;
    rpc_get_string_ptr("data", &params, &data_hex, &data_len);
    uint8_t* data_bytes = NULL; size_t data_bytes_len = 0;
    if (data_hex && data_len) {
        if (!hex_to_bytes(data_hex, data_len, &data_bytes, &data_bytes_len)) {
            jade_process_reject_message(process, CBOR_RPC_BAD_PARAMETERS, "Invalid 'data' hex");
            goto cleanup;
        }
    }

    uint8_t* payload = NULL; size_t payload_len = 0;
    if (!build_eip1559_payload(chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to_bytes,
            value_bytes ? value_bytes : (const uint8_t*)"", value_bytes ? value_bytes_len : 0,
            data_bytes ? data_bytes : (const uint8_t*)"", data_bytes ? data_bytes_len : 0, &payload, &payload_len)) {
        jade_process_reject_message(process, CBOR_RPC_INTERNAL_ERROR, "Failed to build EIP-1559 payload");
        goto cleanup;
    }

    // Typed tx prefix 0x02
    size_t typed_len = 1 + payload_len;
    uint8_t* typed = JADE_MALLOC(typed_len);
    typed[0] = 0x02;
    memcpy(typed + 1, payload, payload_len);

    uint8_t hash[32];
    keccak_256(typed, typed_len, hash);

    uint8_t sig_rec[65]; size_t sig_rec_len = 65;
    uint8_t y_parity = 0;
    if (!wallet_sign_evm_hash(hash, sizeof(hash), path, path_len, sig_rec, sizeof(sig_rec), &sig_rec_len, &y_parity)) {
        jade_process_reject_message(process, CBOR_RPC_INTERNAL_ERROR, "Failed to sign EVM hash");
        goto cleanup;
    }

    // Build signed list: payload items + yParity + r + s
    uint8_t r[32], s[32];
    memcpy(r, sig_rec, 32);
    memcpy(s, sig_rec + 32, 32);

    size_t sl_cap = typed_len + 128;
    uint8_t* sl = JADE_MALLOC(sl_cap);
    size_t sl_len = 0;

    // Copy original payload list content
    memcpy(sl + sl_len, payload, payload_len);
    sl_len += payload_len;

    // Append yParity, r, s inside the list by re-encoding
    // For simplicity, reconstruct list
    uint8_t* final = JADE_MALLOC(sl_cap);
    size_t final_len = 0;

    // Decode not implemented; instead rebuild list: we re-run encoding steps
    // Note: This simplified implementation assumes same order as build_eip1559_payload.
    uint8_t* rebuilt = NULL; size_t rebuilt_len = 0;
    build_eip1559_payload(chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to_bytes,
        value_bytes ? value_bytes : (const uint8_t*)"", value_bytes ? value_bytes_len : 0,
        data_bytes ? data_bytes : (const uint8_t*)"", data_bytes ? data_bytes_len : 0, &rebuilt, &rebuilt_len);

    
    rlp_encode_uint64(y_parity, &final, &final_len);
    rlp_encode_bytes(r, sizeof(r), &final, &final_len);
    rlp_encode_bytes(s, sizeof(s), &final, &final_len);

    // Wrap rebuilt + signature items into a single list
    size_t signed_inner_len = rebuilt_len + final_len;
    uint8_t* signed_inner = JADE_MALLOC(signed_inner_len);
    memcpy(signed_inner, rebuilt, rebuilt_len);
    memcpy(signed_inner + rebuilt_len, final, final_len);

    uint8_t* signed_list = JADE_MALLOC(signed_inner_len + 16);
    size_t signed_list_len = 0;
    rlp_start_list(signed_inner_len, &signed_list, &signed_list_len);
    memcpy(signed_list + signed_list_len, signed_inner, signed_inner_len);
    signed_list_len += signed_inner_len;

    // Prefix with 0x02
    uint8_t* signed_tx = JADE_MALLOC(1 + signed_list_len);
    signed_tx[0] = 0x02;
    memcpy(signed_tx + 1, signed_list, signed_list_len);

    char* tx_hex = NULL;
    JADE_WALLY_VERIFY(wally_hex_from_bytes(signed_tx, 1 + signed_list_len, &tx_hex));
    write_text_result(process, tx_hex);
    JADE_WALLY_VERIFY(wally_free_string(tx_hex));

cleanup:
    return;
}
#endif // AMALGAMATED_BUILD