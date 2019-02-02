/*
 *  Copyright (C) 2017, Nayuta, Inc. All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an
 *  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 *  specific language governing permissions and limitations
 *  under the License.
 */
/** @file   ln_comtx_util.c
 *  @brief  ln_comtx_ex
 */
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>

#include "mbedtls/sha256.h"
//#include "mbedtls/ripemd160.h"
//#include "mbedtls/ecp.h"

#include "btc_keys.h"

#include "ln_comtx_util.h"
#include "ln_local.h"
#include "ln_signer.h"


/**************************************************************************
 * macros
 **************************************************************************/

#define M_SZ_OBSCURED_COMMIT_NUM            (6)

//https://github.com/lightningnetwork/lightning-rfc/blob/master/03-transactions.md#fee-calculation
#define M_FEE_HTLC_TIMEOUT_WEIGHT           (663ULL)
#define M_FEE_HTLC_SUCCESS_WEIGHT           (703ULL)
#define M_FEE_COMMIT_HTLC_WEIGHT            (172ULL)


/**************************************************************************
 * prototypes
 **************************************************************************/


/**************************************************************************
 * public functions
 **************************************************************************/

uint64_t HIDDEN ln_comtx_calc_obscured_commit_num_mask(const uint8_t *pOpenPayBasePt, const uint8_t *pAcceptPayBasePt)
{
    uint64_t obs = 0;
    uint8_t base[32];
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, pOpenPayBasePt, BTC_SZ_PUBKEY);
    mbedtls_sha256_update(&ctx, pAcceptPayBasePt, BTC_SZ_PUBKEY);
    mbedtls_sha256_finish(&ctx, base);
    mbedtls_sha256_free(&ctx);

    for (int lp = 0; lp < M_SZ_OBSCURED_COMMIT_NUM; lp++) {
        obs <<= 8;
        obs |= base[sizeof(base) - M_SZ_OBSCURED_COMMIT_NUM + lp];
    }

    return obs;
}


uint64_t HIDDEN ln_comtx_calc_obscured_commit_num(uint64_t ObscuredCommitNumBase, uint64_t CommitNum)
{
    return ObscuredCommitNumBase ^ CommitNum;
}


uint64_t HIDDEN ln_comtx_calc_commit_num_from_tx(uint32_t Sequence, uint32_t Locktime, uint64_t ObscuredCommitNumBase)
{
    uint64_t commit_num = ((uint64_t)(Sequence & 0xffffff)) << 24;
    commit_num |= (uint64_t)(Locktime & 0xffffff);
    return commit_num ^ ObscuredCommitNumBase;
}


void HIDDEN ln_comtx_htlc_info_init(ln_comtx_htlc_info_t *pHtlcInfo)
{
    pHtlcInfo->type = LN_COMTX_OUTPUT_TYPE_NONE;
    pHtlcInfo->add_htlc_idx = (uint16_t)-1;
    pHtlcInfo->cltv_expiry = 0;
    pHtlcInfo->amount_msat = 0;
    pHtlcInfo->payment_hash = NULL;
    utl_buf_init(&pHtlcInfo->wit_script);
}


void HIDDEN ln_comtx_htlc_info_free(ln_comtx_htlc_info_t *pHtlcInfo)
{
    utl_buf_free(&pHtlcInfo->wit_script);
}


void HIDDEN ln_comtx_base_fee_calc(
    ln_comtx_base_fee_info_t *pBaseFeeInfo,
    const ln_comtx_htlc_info_t **ppHtlcInfo,
    int Num)
{
    pBaseFeeInfo->htlc_success_fee = M_FEE_HTLC_SUCCESS_WEIGHT * pBaseFeeInfo->feerate_per_kw / 1000;
    pBaseFeeInfo->htlc_timeout_fee = M_FEE_HTLC_TIMEOUT_WEIGHT * pBaseFeeInfo->feerate_per_kw / 1000;
    pBaseFeeInfo->commit_fee = 0;
    uint64_t commit_fee_weight = LN_FEE_COMMIT_BASE_WEIGHT;
    uint64_t dust_msat = 0;

    for (int lp = 0; lp < Num; lp++) {
        switch (ppHtlcInfo[lp]->type) {
        case LN_COMTX_OUTPUT_TYPE_OFFERED:
            if (LN_MSAT2SATOSHI(ppHtlcInfo[lp]->amount_msat) >= pBaseFeeInfo->dust_limit_satoshi + pBaseFeeInfo->htlc_timeout_fee) {
                commit_fee_weight += M_FEE_COMMIT_HTLC_WEIGHT;
            } else {
                dust_msat += ppHtlcInfo[lp]->amount_msat;
            }
            break;
        case LN_COMTX_OUTPUT_TYPE_RECEIVED:
            if (LN_MSAT2SATOSHI(ppHtlcInfo[lp]->amount_msat) >= pBaseFeeInfo->dust_limit_satoshi + pBaseFeeInfo->htlc_success_fee) {
                commit_fee_weight += M_FEE_COMMIT_HTLC_WEIGHT;
            } else {
                dust_msat += ppHtlcInfo[lp]->amount_msat;
            }
            break;
        default:
            break;
        }
    }
    pBaseFeeInfo->commit_fee = commit_fee_weight * pBaseFeeInfo->feerate_per_kw / 1000;
    LOGD("pBaseFeeInfo->commit_fee= %" PRIu64 "(feerate_per_kw=%" PRIu32 ")\n", pBaseFeeInfo->commit_fee, pBaseFeeInfo->feerate_per_kw);


    //XXX: probably not correct
    //  the base fee should be added after it has been calculated (after being divided by 1000)
    //pBaseFeeInfo->_rough_actual_fee = (commit_fee_weight * pBaseFeeInfo->feerate_per_kw + dust_msat) / 1000;

    pBaseFeeInfo->_rough_actual_fee = pBaseFeeInfo->commit_fee + dust_msat / 1000;
}


bool HIDDEN ln_comtx_create(
    btc_tx_t *pTx,
    utl_buf_t *pSig,
    const ln_comtx_t *pCommitTx,
    bool ToLocalIsFounder,
    const ln_derkey_local_keys_t *pLocalKeys)
{
    uint64_t fee_local = ToLocalIsFounder ? pCommitTx->p_base_fee_info->commit_fee : 0;
    uint64_t fee_remote = ToLocalIsFounder ? 0 : pCommitTx->p_base_fee_info->commit_fee;

    //output

    //  to_local (P2WSH)
    if (pCommitTx->to_local.satoshi >= pCommitTx->p_base_fee_info->dust_limit_satoshi + fee_local) {
        LOGD("  add local: %" PRIu64 " - %" PRIu64 " sat\n", pCommitTx->to_local.satoshi, fee_local);
        if (!btc_sw_add_vout_p2wsh_wit(pTx, pCommitTx->to_local.satoshi - fee_local, pCommitTx->to_local.p_wit_script)) return false;
        pTx->vout[pTx->vout_cnt - 1].opt = LN_COMTX_OUTPUT_TYPE_TO_LOCAL;
    } else {
        LOGD("  [local output]below dust: %" PRIu64 " < %" PRIu64 " + %" PRIu64 "\n", pCommitTx->to_local.satoshi, pCommitTx->p_base_fee_info->dust_limit_satoshi, fee_local);
    }

    //  to_remote (P2WPKH)
    if (pCommitTx->to_remote.satoshi >= pCommitTx->p_base_fee_info->dust_limit_satoshi + fee_remote) {
        LOGD("  add P2WPKH remote: %" PRIu64 " sat - %" PRIu64 " sat\n", pCommitTx->to_remote.satoshi, fee_remote);
        LOGD("    remote.pubkey: ");
        DUMPD(pCommitTx->to_remote.pubkey, BTC_SZ_PUBKEY);
        if (!btc_sw_add_vout_p2wpkh_pub(pTx, pCommitTx->to_remote.satoshi - fee_remote, pCommitTx->to_remote.pubkey)) return false;
        pTx->vout[pTx->vout_cnt - 1].opt = LN_COMTX_OUTPUT_TYPE_TO_REMOTE;
    } else {
        LOGD("  [remote output]below dust: %" PRIu64 " < %" PRIu64 " + %" PRIu64 "\n", pCommitTx->to_remote.satoshi, pCommitTx->p_base_fee_info->dust_limit_satoshi, fee_remote);
    }

    //  HTLCs
    for (uint16_t lp = 0; lp < pCommitTx->htlc_info_num; lp++) {
        uint64_t fee;
        uint64_t output_sat = LN_MSAT2SATOSHI(pCommitTx->pp_htlc_info[lp]->amount_msat);
        LOGD("lp=%d\n", lp);
        switch (pCommitTx->pp_htlc_info[lp]->type) {
        case LN_COMTX_OUTPUT_TYPE_OFFERED:
            fee = pCommitTx->p_base_fee_info->htlc_timeout_fee;
            LOGD("  HTLC: offered=%" PRIu64 " sat, fee=%" PRIu64 "\n", output_sat, fee);
            break;
        case LN_COMTX_OUTPUT_TYPE_RECEIVED:
            fee = pCommitTx->p_base_fee_info->htlc_success_fee;
            LOGD("  HTLC: received=%" PRIu64 " sat, fee=%" PRIu64 "\n", output_sat, fee);
            break;
        default:
            LOGE("  HTLC: type=%d ???\n", pCommitTx->pp_htlc_info[lp]->type);
            return false;
        }
        if (output_sat >= pCommitTx->p_base_fee_info->dust_limit_satoshi + fee) {
            if (!btc_sw_add_vout_p2wsh_wit(pTx, output_sat, &pCommitTx->pp_htlc_info[lp]->wit_script)) return false;
            pTx->vout[pTx->vout_cnt - 1].opt = lp;
            LOGD("scirpt.len=%d\n", pCommitTx->pp_htlc_info[lp]->wit_script.len);
            //btc_script_print(pCommitTx->pp_htlc_info[lp]->wit_script.buf, pCommitTx->pp_htlc_info[lp]->wit_script.len);
        } else {
            LOGD("    [HTLC]below dust: %" PRIu64 " < %" PRIu64 "(dust_limit) + %" PRIu64 "(fee)\n", output_sat, pCommitTx->p_base_fee_info->dust_limit_satoshi, fee);
        }
    }

    //input
    btc_vin_t *vin = btc_tx_add_vin(pTx, pCommitTx->fund.txid, pCommitTx->fund.txid_index);
    vin->sequence = LN_SEQUENCE(pCommitTx->obscured_commit_num);

    //locktime
    pTx->locktime = LN_LOCKTIME(pCommitTx->obscured_commit_num);

    //sort vin/vout
    btc_tx_sort_bip69(pTx);

    //sign
    uint8_t sighash[BTC_SZ_HASH256];
    if (!btc_sw_sighash_p2wsh_wit(pTx, sighash, 0, pCommitTx->fund.satoshi, pCommitTx->fund.p_wit_script)) {
        LOGE("fail: calc sighash\n");
        return false;
    }
    if (!ln_signer_sign(pSig, sighash, pLocalKeys, LN_BASEPOINT_IDX_FUNDING)) {
        LOGE("fail: calc sighash\n");
        return false;
    }
    return true;
}


/********************************************************************
 * private functions
 ********************************************************************/