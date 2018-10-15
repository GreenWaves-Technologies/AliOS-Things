/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */

#ifndef BZ_CORE_H
#define BZ_CORE_H

#include <stdint.h>

#include "common.h"
#include "ble_service.h"
#include "transport.h"
#include "auth.h"
#include "extcmd.h"
#include "bzopt.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ALI_COMPANY_ID 0x01A8
#define ALI_PROTOCOL_ID 0x05

#define MAX_ADV_DATA_LEN 16

typedef struct {
    ble_ais_t ais;
    transport_t transport;
#if BZ_ENABLE_AUTH
    auth_t auth;
#endif
    extcmd_t ext;
    ali_event_handler_t event_handler;
    void *p_evt_context;
    uint16_t conn_handle;
    uint8_t adv_data[MAX_ADV_DATA_LEN];
    uint16_t adv_data_len;
} core_t;

extern core_t *g_ali;

ret_code_t core_init(void *p_ali, ali_init_t const *p_init);
void core_reset(void *p_ali);
// TODO: rm transport_packet from core
ret_code_t transport_packet(uint8_t type, void *p_ali_ext, uint8_t cmd,
                            uint8_t *p_data, uint16_t length);
ret_code_t get_bz_adv_data(uint8_t *p_data, uint16_t *length);
void notify_evt_no_data(core_t *p_ali, uint8_t evt_type);

void core_handle_err(uint8_t src, uint8_t code);

#ifdef __cplusplus
}
#endif

#endif // BZ_CORE_H
