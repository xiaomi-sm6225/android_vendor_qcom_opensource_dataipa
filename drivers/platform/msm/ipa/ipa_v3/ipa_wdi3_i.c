// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018 - 2021, The Linux Foundation. All rights reserved.
 */

#include "ipa_i.h"
#include <linux/ipa_wdi3.h>

#define UPDATE_RP_MODERATION_CONFIG 1
#define UPDATE_RP_MODERATION_THRESHOLD 8

#define IPA_WLAN_AGGR_PKT_LIMIT 1
#define IPA_WLAN_AGGR_BYTE_LIMIT 2 /*2 Kbytes Agger hard byte limit*/

#define IPA_WDI3_GSI_EVT_RING_INT_MODT 32
#define IPA_WDI3_MAX_VALUE_OF_BANK_ID 63

static void ipa3_wdi3_gsi_evt_ring_err_cb(struct gsi_evt_err_notify *notify)
{
	switch (notify->evt_id) {
	case GSI_EVT_OUT_OF_BUFFERS_ERR:
		IPAERR("Got GSI_EVT_OUT_OF_BUFFERS_ERR\n");
		break;
	case GSI_EVT_OUT_OF_RESOURCES_ERR:
		IPAERR("Got GSI_EVT_OUT_OF_RESOURCES_ERR\n");
		break;
	case GSI_EVT_UNSUPPORTED_INTER_EE_OP_ERR:
		IPAERR("Got GSI_EVT_UNSUPPORTED_INTER_EE_OP_ERR\n");
		break;
	case GSI_EVT_EVT_RING_EMPTY_ERR:
		IPAERR("Got GSI_EVT_EVT_RING_EMPTY_ERR\n");
		break;
	default:
		IPAERR("Unexpected err evt: %d\n", notify->evt_id);
	}
	ipa_assert();
}

static void ipa3_wdi3_gsi_chan_err_cb(struct gsi_chan_err_notify *notify)
{
	switch (notify->evt_id) {
	case GSI_CHAN_INVALID_TRE_ERR:
		IPAERR("Got GSI_CHAN_INVALID_TRE_ERR\n");
		break;
	case GSI_CHAN_NON_ALLOCATED_EVT_ACCESS_ERR:
		IPAERR("Got GSI_CHAN_NON_ALLOCATED_EVT_ACCESS_ERR\n");
		break;
	case GSI_CHAN_OUT_OF_BUFFERS_ERR:
		IPAERR("Got GSI_CHAN_OUT_OF_BUFFERS_ERR\n");
		break;
	case GSI_CHAN_OUT_OF_RESOURCES_ERR:
		IPAERR("Got GSI_CHAN_OUT_OF_RESOURCES_ERR\n");
		break;
	case GSI_CHAN_UNSUPPORTED_INTER_EE_OP_ERR:
		IPAERR("Got GSI_CHAN_UNSUPPORTED_INTER_EE_OP_ERR\n");
		break;
	case GSI_CHAN_HWO_1_ERR:
		IPAERR("Got GSI_CHAN_HWO_1_ERR\n");
		break;
	default:
		IPAERR("Unexpected err evt: %d\n", notify->evt_id);
	}
	ipa_assert();
}

static int ipa3_setup_wdi3_gsi_channel(u8 is_smmu_enabled,
	struct ipa_wdi_pipe_setup_info *info,
	struct ipa_wdi_pipe_setup_info_smmu *info_smmu, u8 dir,
	struct ipa3_ep_context *ep)
{
	struct gsi_evt_ring_props gsi_evt_ring_props;
	struct gsi_chan_props gsi_channel_props;
	union __packed gsi_channel_scratch ch_scratch;
	union __packed gsi_evt_scratch evt_scratch;
	const struct ipa_gsi_ep_config *gsi_ep_info;
	int result, len;
	unsigned long va;
	uint32_t addr_low, addr_high;

	if (!info || !info_smmu || !ep) {
		IPAERR("invalid input\n");
		return -EINVAL;
	}
	memset(&gsi_evt_ring_props, 0, sizeof(gsi_evt_ring_props));
	memset(&gsi_channel_props, 0, sizeof(gsi_channel_props));

	if(ipa_get_wdi_version() == IPA_WDI_3_V2) {
		gsi_channel_props.prot = GSI_CHAN_PROT_WDI3_V2;
		gsi_evt_ring_props.intf = GSI_EVT_CHTYPE_WDI3_V2_EV;
	}
	else {
		gsi_channel_props.prot = GSI_CHAN_PROT_WDI3;
		gsi_evt_ring_props.intf = GSI_EVT_CHTYPE_WDI3_EV;
	}

	/* setup event ring */

	if (ipa3_ctx->ipa_hw_type >= IPA_HW_v4_9) {
		gsi_evt_ring_props.intr = GSI_INTR_MSI;
		/* 32 (for Tx) and 8 (for Rx) */
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR) ||
			(dir == IPA_WDI3_TX2_DIR))
			gsi_evt_ring_props.re_size = GSI_EVT_RING_RE_SIZE_32B;
		else
			gsi_evt_ring_props.re_size = GSI_EVT_RING_RE_SIZE_8B;
	} else {
		gsi_evt_ring_props.intr = GSI_INTR_IRQ;
		/* 16 (for Tx) and 8 (for Rx) */
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR) ||
			(dir == IPA_WDI3_TX2_DIR))
			gsi_evt_ring_props.re_size = GSI_EVT_RING_RE_SIZE_16B;
		else
			gsi_evt_ring_props.re_size = GSI_EVT_RING_RE_SIZE_8B;
	}
	if (!is_smmu_enabled) {
		gsi_evt_ring_props.ring_len = info->event_ring_size;
		gsi_evt_ring_props.ring_base_addr =
			(u64)info->event_ring_base_pa;
	} else {
		len = info_smmu->event_ring_size;
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR)) {
			if (ipa_create_gsi_smmu_mapping((
				dir == IPA_WDI3_TX_DIR) ?
				IPA_WDI_CE_RING_RES : IPA_WDI_CE1_RING_RES,
				true, info->event_ring_base_pa,
				&info_smmu->event_ring_base, len,
				false, &va)) {
				IPAERR("failed to get smmu mapping\n");
				return -EFAULT;
			}
		} else if (dir == IPA_WDI3_TX2_DIR) {
                        if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_CE2_RING_RES,
                                true, info->event_ring_base_pa,
                                &info_smmu->event_ring_base, len,
                                false, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                return -EFAULT;
                        }
                } else if (dir == IPA_WDI3_RX_DIR) {
			if (ipa_create_gsi_smmu_mapping(
				IPA_WDI_RX_COMP_RING_RES, true,
				info->event_ring_base_pa,
				&info_smmu->event_ring_base, len,
				false, &va)) {
				IPAERR("failed to get smmu mapping\n");
				return -EFAULT;
			}
		} else {
			if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_RX2_COMP_RING_RES, true,
                                info->event_ring_base_pa,
                                &info_smmu->event_ring_base, len,
                                false, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                return -EFAULT;
                        }
                }
		gsi_evt_ring_props.ring_len = len;
		gsi_evt_ring_props.ring_base_addr = (u64)va;
	}
	gsi_evt_ring_props.int_modt = IPA_WDI3_GSI_EVT_RING_INT_MODT;
	gsi_evt_ring_props.int_modc = 1;
	gsi_evt_ring_props.exclusive = true;
	gsi_evt_ring_props.err_cb = ipa3_wdi3_gsi_evt_ring_err_cb;
	gsi_evt_ring_props.user_data = NULL;

	result = gsi_alloc_evt_ring(&gsi_evt_ring_props, ipa3_ctx->gsi_dev_hdl,
		&ep->gsi_evt_ring_hdl);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("fail to alloc RX event ring\n");
		result = -EFAULT;
		goto fail_smmu_mapping;
	}

	ep->gsi_mem_info.evt_ring_len = gsi_evt_ring_props.ring_len;
	ep->gsi_mem_info.evt_ring_base_addr =
		gsi_evt_ring_props.ring_base_addr;

	/* setup channel ring */
	if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR) ||
		(dir == IPA_WDI3_TX2_DIR))
		gsi_channel_props.dir = GSI_CHAN_DIR_FROM_GSI;
	else
		gsi_channel_props.dir = GSI_CHAN_DIR_TO_GSI;

	gsi_ep_info = ipa3_get_gsi_ep_info(ep->client);
	if (!gsi_ep_info) {
		IPAERR("Failed getting GSI EP info for client=%d\n",
		       ep->client);
		result = -EINVAL;
		goto fail_get_gsi_ep_info;
	} else
		gsi_channel_props.ch_id = gsi_ep_info->ipa_gsi_chan_num;

	gsi_channel_props.db_in_bytes = 0;
	gsi_channel_props.evt_ring_hdl = ep->gsi_evt_ring_hdl;

	if (ipa3_ctx->ipa_hw_type >= IPA_HW_v4_9) {
		/* 32 (for Tx) and 64 (for Rx) */
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR) ||
			(dir == IPA_WDI3_TX2_DIR))
			gsi_channel_props.re_size = GSI_CHAN_RE_SIZE_32B;
		else {
			if (gsi_channel_props.prot == GSI_CHAN_PROT_WDI3_V2)
				gsi_channel_props.re_size = GSI_CHAN_RE_SIZE_32B;
			else 
				gsi_channel_props.re_size = GSI_CHAN_RE_SIZE_64B;
		}

	} else
		gsi_channel_props.re_size = GSI_CHAN_RE_SIZE_16B;

	gsi_channel_props.use_db_eng = GSI_CHAN_DB_MODE;
	gsi_channel_props.max_prefetch = GSI_ONE_PREFETCH_SEG;
	gsi_channel_props.prefetch_mode =
		gsi_ep_info->prefetch_mode;
	gsi_channel_props.empty_lvl_threshold =
		gsi_ep_info->prefetch_threshold;
	gsi_channel_props.low_weight = 1;
	gsi_channel_props.err_cb = ipa3_wdi3_gsi_chan_err_cb;

	if (!is_smmu_enabled) {
		gsi_channel_props.ring_len = (u16)info->transfer_ring_size;
		gsi_channel_props.ring_base_addr =
			(u64)info->transfer_ring_base_pa;
	} else {
		len = info_smmu->transfer_ring_size;
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR)) {
			if (ipa_create_gsi_smmu_mapping((
				dir == IPA_WDI3_TX_DIR) ?
				IPA_WDI_TX_RING_RES : IPA_WDI_TX1_RING_RES,
				true, info->transfer_ring_base_pa,
				&info_smmu->transfer_ring_base, len,
				false, &va)) {
				IPAERR("failed to get smmu mapping\n");
				result = -EFAULT;
				goto fail_get_gsi_ep_info;
			}
		} else if (dir == IPA_WDI3_TX2_DIR) {
                        if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_TX2_RING_RES,
                                true, info->transfer_ring_base_pa,
                                &info_smmu->transfer_ring_base, len,
                                false, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                result = -EFAULT;
                                goto fail_get_gsi_ep_info;
                        }
                } else if (dir == IPA_WDI3_RX_DIR) {
			if (ipa_create_gsi_smmu_mapping(
				IPA_WDI_RX_RING_RES, true,
				info->transfer_ring_base_pa,
				&info_smmu->transfer_ring_base, len,
				false, &va)) {
				IPAERR("failed to get smmu mapping\n");
				result = -EFAULT;
				goto fail_get_gsi_ep_info;
			}
		} else {
			if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_RX2_RING_RES, true,
                                info->transfer_ring_base_pa,
                                &info_smmu->transfer_ring_base, len,
                                false, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                result = -EFAULT;
                                goto fail_get_gsi_ep_info;
                        }
		}
		gsi_channel_props.ring_len = len;
		gsi_channel_props.ring_base_addr = (u64)va;
	}

	result = gsi_alloc_channel(&gsi_channel_props, ipa3_ctx->gsi_dev_hdl,
		&ep->gsi_chan_hdl);
	if (result != GSI_STATUS_SUCCESS)
		goto fail_get_gsi_ep_info;

	ep->gsi_mem_info.chan_ring_len = gsi_channel_props.ring_len;
	ep->gsi_mem_info.chan_ring_base_addr =
		gsi_channel_props.ring_base_addr;

	/* write event scratch */
	memset(&evt_scratch, 0, sizeof(evt_scratch));
	evt_scratch.wdi3.update_rp_moderation_config =
		UPDATE_RP_MODERATION_CONFIG;
	result = gsi_write_evt_ring_scratch(ep->gsi_evt_ring_hdl, evt_scratch);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to write evt ring scratch\n");
		goto fail_write_scratch;
	}

	if (!is_smmu_enabled) {
		IPADBG("smmu disabled\n");
		if (info->is_evt_rn_db_pcie_addr == true)
			IPADBG_LOW("is_evt_rn_db_pcie_addr is PCIE addr\n");
		else
			IPADBG_LOW("is_evt_rn_db_pcie_addr is DDR addr\n");
		IPADBG_LOW("LSB 0x%x\n",
			(u32)info->event_ring_doorbell_pa);
		IPADBG_LOW("MSB 0x%x\n",
			(u32)((u64)info->event_ring_doorbell_pa >> 32));
	} else {
		IPADBG("smmu enabled\n");
		if (info_smmu->is_evt_rn_db_pcie_addr == true)
			IPADBG_LOW("is_evt_rn_db_pcie_addr is PCIE addr\n");
		else
			IPADBG_LOW("is_evt_rn_db_pcie_addr is DDR addr\n");
		IPADBG_LOW("LSB 0x%x\n",
			(u32)info_smmu->event_ring_doorbell_pa);
		IPADBG_LOW("MSB 0x%x\n",
			(u32)((u64)info_smmu->event_ring_doorbell_pa >> 32));
	}

	if (!is_smmu_enabled) {
		addr_low = (u32)info->event_ring_doorbell_pa;
		addr_high = (u32)((u64)info->event_ring_doorbell_pa >> 32);
	} else {
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR)) {
			if (ipa_create_gsi_smmu_mapping((
				dir == IPA_WDI3_TX_DIR) ?
				IPA_WDI_CE_DB_RES : IPA_WDI_CE1_DB_RES,
				true, info_smmu->event_ring_doorbell_pa,
				NULL, 4, true, &va)) {
				IPAERR("failed to get smmu mapping\n");
				result = -EFAULT;
				goto fail_write_scratch;
			}
		} else if (dir == IPA_WDI3_TX2_DIR) {
                        if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_CE2_DB_RES,
                                true, info_smmu->event_ring_doorbell_pa,
                                NULL, 4, true, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                result = -EFAULT;
                                goto fail_write_scratch;
                        }
                } else if (dir == IPA_WDI3_RX_DIR) {
			if (ipa_create_gsi_smmu_mapping(
				IPA_WDI_RX_COMP_RING_WP_RES,
				true, info_smmu->event_ring_doorbell_pa,
				NULL, 4, true, &va)) {
				IPAERR("failed to get smmu mapping\n");
				result = -EFAULT;
				goto fail_write_scratch;
			}
		} else {
			if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_RX2_COMP_RING_WP_RES,
                                true, info_smmu->event_ring_doorbell_pa,
                                NULL, 4, true, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                result = -EFAULT;
                                goto fail_write_scratch;
                        }
		}
		addr_low = (u32)va;
		addr_high = (u32)((u64)va >> 32);
	}

	/*
	 * Arch specific:
	 * pcie addr which are not via smmu, use pa directly!
	 * pcie and DDR via 2 different port
	 * assert bit 40 to indicate it is pcie addr
	 * WDI-3.0, MSM --> pcie via smmu
	 * WDI-3.0, MDM --> pcie not via smmu + dual port
	 * assert bit 40 in case
	 */
	if ((ipa3_ctx->platform_type == IPA_PLAT_TYPE_MDM) &&
		is_smmu_enabled) {
		/*
		 * Ir-respective of smmu enabled don't use IOVA addr
		 * since pcie not via smmu in MDM's
		 */
		if (info_smmu->is_evt_rn_db_pcie_addr == true) {
			addr_low = (u32)info_smmu->event_ring_doorbell_pa;
			addr_high =
				(u32)((u64)info_smmu->event_ring_doorbell_pa
				>> 32);
		}
	}

	/*
	 * GSI recomendation to set bit-40 for (mdm targets && pcie addr)
	 * from wdi-3.0 interface document
	 */
	if (!is_smmu_enabled) {
		if ((ipa3_ctx->platform_type == IPA_PLAT_TYPE_MDM) &&
			info->is_evt_rn_db_pcie_addr)
			addr_high |= (1 << 8);
	} else {
		if ((ipa3_ctx->platform_type == IPA_PLAT_TYPE_MDM) &&
			info_smmu->is_evt_rn_db_pcie_addr)
			addr_high |= (1 << 8);
	}

	gsi_wdi3_write_evt_ring_db(ep->gsi_evt_ring_hdl,
			addr_low,
			addr_high);

	/* write channel scratch */
	memset(&ch_scratch, 0, sizeof(ch_scratch));
	ch_scratch.wdi3.update_rp_moderation_threshold =
		UPDATE_RP_MODERATION_THRESHOLD;
	if ((dir == IPA_WDI3_RX_DIR) || (dir == IPA_WDI3_RX2_DIR)) {
		if (!is_smmu_enabled)
			ch_scratch.wdi3.rx_pkt_offset = info->pkt_offset;
		else
			ch_scratch.wdi3.rx_pkt_offset = info_smmu->pkt_offset;
		/* this metadata reg offset need to be in words */
		ch_scratch.wdi3.endp_metadata_reg_offset =
			ipahal_get_reg_mn_ofst(IPA_ENDP_INIT_HDR_METADATA_n, 0,
				gsi_ep_info->ipa_ep_num) / 4;
	}

	if (!is_smmu_enabled) {
		IPADBG_LOW("smmu disabled\n");
		if (info->is_txr_rn_db_pcie_addr == true)
			IPADBG_LOW("is_txr_rn_db_pcie_addr is PCIE addr\n");
		else
			IPADBG_LOW("is_txr_rn_db_pcie_addr is DDR addr\n");
		IPADBG_LOW("LSB 0x%x\n",
			(u32)info->transfer_ring_doorbell_pa);
		IPADBG_LOW("MSB 0x%x\n",
			(u32)((u64)info->transfer_ring_doorbell_pa >> 32));
	} else {
		IPADBG_LOW("smmu eabled\n");
		if (info_smmu->is_txr_rn_db_pcie_addr == true)
			IPADBG_LOW("is_txr_rn_db_pcie_addr is PCIE addr\n");
		else
			IPADBG_LOW("is_txr_rn_db_pcie_addr is DDR addr\n");
		IPADBG_LOW("LSB 0x%x\n",
			(u32)info_smmu->transfer_ring_doorbell_pa);
		IPADBG_LOW("MSB 0x%x\n",
			(u32)((u64)info_smmu->transfer_ring_doorbell_pa >> 32));
	}

	if (!is_smmu_enabled) {
		ch_scratch.wdi3.wifi_rp_address_low =
			(u32)info->transfer_ring_doorbell_pa;
		ch_scratch.wdi3.wifi_rp_address_high =
			(u32)((u64)info->transfer_ring_doorbell_pa >> 32);
	} else {
		if ((dir == IPA_WDI3_TX_DIR) || (dir == IPA_WDI3_TX1_DIR)) {
			if (ipa_create_gsi_smmu_mapping((
				dir == IPA_WDI3_TX_DIR) ?
				IPA_WDI_TX_DB_RES : IPA_WDI_TX1_DB_RES,
				true, info_smmu->transfer_ring_doorbell_pa,
				NULL, 4, true, &va)) {
				IPAERR("failed to get smmu mapping\n");
				result = -EFAULT;
				goto fail_write_scratch;
			}
			ch_scratch.wdi3.wifi_rp_address_low = (u32)va;
			ch_scratch.wdi3.wifi_rp_address_high =
				(u32)((u64)va >> 32);
		} else if (dir == IPA_WDI3_TX2_DIR) {
                        if (ipa_create_gsi_smmu_mapping(
                                IPA_WDI_TX2_DB_RES,
                                true, info_smmu->transfer_ring_doorbell_pa,
                                NULL, 4, true, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                result = -EFAULT;
                                goto fail_write_scratch;
                        }
                        ch_scratch.wdi3.wifi_rp_address_low = (u32)va;
                        ch_scratch.wdi3.wifi_rp_address_high =
                                (u32)((u64)va >> 32);
                } else if (dir == IPA_WDI3_RX_DIR){
			if (ipa_create_gsi_smmu_mapping(IPA_WDI_RX_RING_RP_RES,
				true, info_smmu->transfer_ring_doorbell_pa,
				NULL, 4, true, &va)) {
				IPAERR("failed to get smmu mapping\n");
				result = -EFAULT;
				goto fail_write_scratch;
			}
			ch_scratch.wdi3.wifi_rp_address_low = (u32)va;
			ch_scratch.wdi3.wifi_rp_address_high =
				(u32)((u64)va >> 32);
		} else {
			if (ipa_create_gsi_smmu_mapping(IPA_WDI_RX2_RING_RP_RES,
                                true, info_smmu->transfer_ring_doorbell_pa,
                                NULL, 4, true, &va)) {
                                IPAERR("failed to get smmu mapping\n");
                                result = -EFAULT;
                                goto fail_write_scratch;
                        }
                        ch_scratch.wdi3.wifi_rp_address_low = (u32)va;
                        ch_scratch.wdi3.wifi_rp_address_high =
                                (u32)((u64)va >> 32);
		}
	}

	/*
	 * Arch specific:
	 * pcie addr which are not via smmu, use pa directly!
	 * pcie and DDR via 2 different port
	 * assert bit 40 to indicate it is pcie addr
	 * WDI-3.0, MSM --> pcie via smmu
	 * WDI-3.0, MDM --> pcie not via smmu + dual port
	 * assert bit 40 in case
	 */
	if ((ipa3_ctx->platform_type == IPA_PLAT_TYPE_MDM) &&
		is_smmu_enabled) {
		/*
		 * Ir-respective of smmu enabled don't use IOVA addr
		 * since pcie not via smmu in MDM's
		 */
		if (info_smmu->is_txr_rn_db_pcie_addr == true) {
			ch_scratch.wdi3.wifi_rp_address_low =
				(u32)info_smmu->transfer_ring_doorbell_pa;
			ch_scratch.wdi3.wifi_rp_address_high =
				(u32)((u64)info_smmu->transfer_ring_doorbell_pa
				>> 32);
		}
	}

	/*
	 * GSI recomendation to set bit-40 for (mdm targets && pcie addr)
	 * from wdi-3.0 interface document
	 */
	if (!is_smmu_enabled) {
		if ((ipa3_ctx->platform_type == IPA_PLAT_TYPE_MDM) &&
			info->is_txr_rn_db_pcie_addr)
			ch_scratch.wdi3.wifi_rp_address_high =
			(u32)((u32)ch_scratch.wdi3.wifi_rp_address_high |
			(1 << 8));
	} else {
		if ((ipa3_ctx->platform_type == IPA_PLAT_TYPE_MDM) &&
			info_smmu->is_txr_rn_db_pcie_addr)
			ch_scratch.wdi3.wifi_rp_address_high =
			(u32)((u32)ch_scratch.wdi3.wifi_rp_address_high |
			(1 << 8));
	}

	if(ipa_get_wdi_version() == IPA_WDI_3_V2) {

		ch_scratch.wdi3_v2.wifi_rp_address_high =
			ch_scratch.wdi3.wifi_rp_address_high;

		ch_scratch.wdi3_v2.wifi_rp_address_low =
			ch_scratch.wdi3.wifi_rp_address_low;

		ch_scratch.wdi3_v2.update_rp_moderation_threshold =
			ch_scratch.wdi3.update_rp_moderation_threshold;


		if ( dir == IPA_WDI3_RX_DIR) {

			ch_scratch.wdi3_v2.rx_pkt_offset = ch_scratch.wdi3.rx_pkt_offset;
			ch_scratch.wdi3_v2.endp_metadata_reg_offset =
						ch_scratch.wdi3.endp_metadata_reg_offset;
		} else {


				if(is_smmu_enabled) {
					if(info_smmu->rx_bank_id > IPA_WDI3_MAX_VALUE_OF_BANK_ID) {
						IPAERR("Incorrect bank id value %d Exceeding the 6bit range\n", info_smmu->rx_bank_id);
						goto fail_write_scratch;
					}
					ch_scratch.wdi3_v2.bank_id = info_smmu->rx_bank_id;
				}
				else {
					if(info->rx_bank_id > IPA_WDI3_MAX_VALUE_OF_BANK_ID) {
						IPAERR("Incorrect bank id value %d Exceeding the 6bit range\n", info->rx_bank_id);
						goto fail_write_scratch;
					}

					ch_scratch.wdi3_v2.bank_id = info->rx_bank_id;
				}
		}

		ch_scratch.wdi3_v2.qmap_id = 0;
		ch_scratch.wdi3_v2.reserved1 = 0;
		ch_scratch.wdi3_v2.reserved2 = 0;
	}

	result = gsi_write_channel_scratch(ep->gsi_chan_hdl, ch_scratch);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to write evt ring scratch\n");
		goto fail_write_scratch;
	}
	return 0;

fail_write_scratch:
	gsi_dealloc_channel(ep->gsi_chan_hdl);
	ep->gsi_chan_hdl = ~0;
fail_get_gsi_ep_info:
	gsi_dealloc_evt_ring(ep->gsi_evt_ring_hdl);
	ep->gsi_evt_ring_hdl = ~0;
fail_smmu_mapping:
	ipa3_release_wdi3_gsi_smmu_mappings(dir);
	return result;
}

int ipa3_conn_wdi3_pipes(struct ipa_wdi_conn_in_params *in,
	struct ipa_wdi_conn_out_params *out,
	ipa_wdi_meter_notifier_cb wdi_notify)
{
	enum ipa_client_type rx_client;
	enum ipa_client_type tx_client;
	enum ipa_client_type tx1_client;
	struct ipa3_ep_context *ep_rx;
	struct ipa3_ep_context *ep_tx;
	struct ipa3_ep_context *ep_tx1;
	int ipa_ep_idx_rx;
	int ipa_ep_idx_tx;
	int ipa_ep_idx_tx1 = IPA_EP_NOT_ALLOCATED;
	int result = 0;
	u32 gsi_db_addr_low, gsi_db_addr_high;
	void __iomem *db_addr;
	u32 evt_ring_db_addr_low, evt_ring_db_addr_high, db_val = 0;
	u8 rx_dir, tx_dir;

	/* wdi3 only support over gsi */
	if (ipa_get_wdi_version() < IPA_WDI_3) {
		IPAERR("wdi3 over uc offload not supported");
		WARN_ON(1);
		return -EFAULT;
	}

	if (in == NULL || out == NULL) {
		IPAERR("invalid input\n");
		return -EINVAL;
	}

	if (in->is_smmu_enabled == false) {
		rx_client = in->u_rx.rx.client;
		tx_client = in->u_tx.tx.client;
	} else {
		rx_client = in->u_rx.rx_smmu.client;
		tx_client = in->u_tx.tx_smmu.client;
	}

	ipa_ep_idx_rx = ipa_get_ep_mapping(rx_client);
	ipa_ep_idx_tx = ipa_get_ep_mapping(tx_client);

	if (ipa_ep_idx_rx == -1 || ipa_ep_idx_tx == -1) {
		IPAERR("fail to alloc EP.\n");
		return -EFAULT;
	}
	if (ipa_ep_idx_rx >= ipa3_get_max_num_pipes() ||
		ipa_ep_idx_tx >= ipa3_get_max_num_pipes()) {
		IPAERR("ep out of range.\n");
		return -EFAULT;
	}

	ep_rx = &ipa3_ctx->ep[ipa_ep_idx_rx];
	ep_tx = &ipa3_ctx->ep[ipa_ep_idx_tx];

	if (ep_rx->valid || ep_tx->valid) {
		IPAERR("EP already allocated.\n");
		return -EFAULT;
	}

	memset(ep_rx, 0, offsetof(struct ipa3_ep_context, sys));
	memset(ep_tx, 0, offsetof(struct ipa3_ep_context, sys));

	if (in->is_tx1_used &&
		ipa3_ctx->is_wdi3_tx1_needed) {
		tx1_client = (in->is_smmu_enabled) ?
			in->u_tx1.tx_smmu.client : in->u_tx1.tx.client;
		ipa_ep_idx_tx1 = ipa_get_ep_mapping(tx1_client);

		if (ipa_ep_idx_tx1 == IPA_EP_NOT_ALLOCATED ||
			ipa_ep_idx_tx1 >= IPA3_MAX_NUM_PIPES) {
			IPAERR("fail to alloc ep2 tx clnt %d not supprtd %d",
				tx1_client, ipa_ep_idx_tx1);
			return -EINVAL;
		} else {
			ep_tx1 = &ipa3_ctx->ep[ipa_ep_idx_tx1];
			if (ep_tx1->valid) {
				IPAERR("EP already allocated.\n");
				return -EFAULT;
			}
		}
		memset(ep_tx1, 0, offsetof(struct ipa3_ep_context, sys));
	}

	IPA_ACTIVE_CLIENTS_INC_SIMPLE();

#ifdef IPA_WAN_MSG_IPv6_ADDR_GW_LEN
	if (wdi_notify)
		ipa3_ctx->uc_wdi_ctx.stats_notify = wdi_notify;
	else
		IPADBG("wdi_notify is null\n");
#endif

	/* setup rx ep cfg */
	ep_rx->valid = 1;
	ep_rx->client = rx_client;
	result = ipa3_disable_data_path(ipa_ep_idx_rx);
	if (result) {
		IPAERR("disable data path failed res=%d clnt=%d.\n", result,
			ipa_ep_idx_rx);
		IPA_ACTIVE_CLIENTS_DEC_SIMPLE();
		return -EFAULT;
	}
	ep_rx->client_notify = in->notify;
	ep_rx->priv = in->priv;

	if (in->is_smmu_enabled == false)
		memcpy(&ep_rx->cfg, &in->u_rx.rx.ipa_ep_cfg,
			sizeof(ep_rx->cfg));
	else
		memcpy(&ep_rx->cfg, &in->u_rx.rx_smmu.ipa_ep_cfg,
			sizeof(ep_rx->cfg));

	if (ipa3_cfg_ep(ipa_ep_idx_rx, &ep_rx->cfg)) {
		IPAERR("fail to setup rx pipe cfg\n");
		result = -EFAULT;
		goto fail;
	}

	/* setup RX gsi channel */
	rx_dir = (rx_client == IPA_CLIENT_WLAN2_PROD) ?
			IPA_WDI3_RX_DIR : IPA_WDI3_RX2_DIR;

	if (ipa3_setup_wdi3_gsi_channel(in->is_smmu_enabled,
		&in->u_rx.rx, &in->u_rx.rx_smmu, rx_dir,
		ep_rx)) {
		IPAERR("fail to setup wdi3 gsi rx channel\n");
		result = -EFAULT;
		goto fail;
	}
	if (gsi_query_channel_db_addr(ep_rx->gsi_chan_hdl,
		&gsi_db_addr_low, &gsi_db_addr_high)) {
		IPAERR("failed to query gsi rx db addr\n");
		result = -EFAULT;
		goto fail;
	}
	/* only 32 bit lsb is used */
	out->rx_uc_db_pa = (phys_addr_t)(gsi_db_addr_low);
	IPADBG("out->rx_uc_db_pa %llu\n", out->rx_uc_db_pa);

	ipa3_install_dflt_flt_rules(ipa_ep_idx_rx);
	IPADBG("client %d (ep: %d) connected\n", rx_client,
		ipa_ep_idx_rx);

	/* setup tx ep cfg */
	ep_tx->valid = 1;
	ep_tx->client = tx_client;
	result = ipa3_disable_data_path(ipa_ep_idx_tx);
	if (result) {
		IPAERR("disable data path failed res=%d ep=%d.\n", result,
			ipa_ep_idx_tx);
		result = -EFAULT;
		goto fail;
	}

	if (in->is_smmu_enabled == false)
		memcpy(&ep_tx->cfg, &in->u_tx.tx.ipa_ep_cfg,
			sizeof(ep_tx->cfg));
	else
		memcpy(&ep_tx->cfg, &in->u_tx.tx_smmu.ipa_ep_cfg,
			sizeof(ep_tx->cfg));

	ep_tx->cfg.aggr.aggr_en = IPA_ENABLE_AGGR;
	ep_tx->cfg.aggr.aggr = IPA_GENERIC;
	ep_tx->cfg.aggr.aggr_byte_limit = IPA_WLAN_AGGR_BYTE_LIMIT;
	ep_tx->cfg.aggr.aggr_pkt_limit = IPA_WLAN_AGGR_PKT_LIMIT;
	ep_tx->cfg.aggr.aggr_hard_byte_limit_en = IPA_ENABLE_AGGR;
	if (ipa3_cfg_ep(ipa_ep_idx_tx, &ep_tx->cfg)) {
		IPAERR("fail to setup tx pipe cfg\n");
		result = -EFAULT;
		goto fail;
	}

	/* setup TX gsi channel */
	tx_dir = (tx_client == IPA_CLIENT_WLAN2_CONS) ?
                        IPA_WDI3_TX_DIR : IPA_WDI3_TX2_DIR;

	if (ipa3_setup_wdi3_gsi_channel(in->is_smmu_enabled,
		&in->u_tx.tx, &in->u_tx.tx_smmu, tx_dir,
		ep_tx)) {
		IPAERR("fail to setup wdi3 gsi tx channel\n");
		result = -EFAULT;
		goto fail;
	}
	if (gsi_query_channel_db_addr(ep_tx->gsi_chan_hdl,
		&gsi_db_addr_low, &gsi_db_addr_high)) {
		IPAERR("failed to query gsi tx db addr\n");
		result = -EFAULT;
		goto fail;
	}
	/* only 32 bit lsb is used */
	out->tx_uc_db_pa = (phys_addr_t)(gsi_db_addr_low);
	IPADBG("out->tx_uc_db_pa %llu\n", out->tx_uc_db_pa);
	IPADBG("client %d (ep: %d) connected\n", tx_client,
		ipa_ep_idx_tx);

	/* ring initial event ring dbs */
	gsi_query_evt_ring_db_addr(ep_rx->gsi_evt_ring_hdl,
		&evt_ring_db_addr_low, &evt_ring_db_addr_high);
	IPADBG("evt_ring_hdl %lu, db_addr_low %u db_addr_high %u\n",
		ep_rx->gsi_evt_ring_hdl, evt_ring_db_addr_low,
		evt_ring_db_addr_high);

	/* only 32 bit lsb is used */
	db_addr = ioremap((phys_addr_t)(evt_ring_db_addr_low), 4);
	/*
	 * IPA/GSI driver should ring the event DB once after
	 * initialization of the event, with a value that is
	 * outside of the ring range. Eg: ring base = 0x1000,
	 * ring size = 0x100 => AP can write value > 0x1100
	 * into the doorbell address. Eg: 0x 1110.
	 * Use event ring base addr + event ring size + 1 element size.
	 */
	db_val = (u32)ep_rx->gsi_mem_info.evt_ring_base_addr;
	db_val += ((in->is_smmu_enabled) ? in->u_rx.rx_smmu.event_ring_size :
		in->u_rx.rx.event_ring_size);
	db_val += GSI_EVT_RING_RE_SIZE_8B;
	iowrite32(db_val, db_addr);
	IPADBG("RX base_addr 0x%x evt wp val: 0x%x\n",
		ep_rx->gsi_mem_info.evt_ring_base_addr, db_val);

	gsi_query_evt_ring_db_addr(ep_tx->gsi_evt_ring_hdl,
		&evt_ring_db_addr_low, &evt_ring_db_addr_high);

	/* only 32 bit lsb is used */
	db_addr = ioremap((phys_addr_t)(evt_ring_db_addr_low), 4);
	/*
	 * IPA/GSI driver should ring the event DB once after
	 * initialization of the event, with a value that is
	 * outside of the ring range. Eg: ring base = 0x1000,
	 * ring size = 0x100 => AP can write value > 0x1100
	 * into the doorbell address. Eg: 0x 1110
	 * Use event ring base addr + event ring size + 1 element size.
	 */
	db_val = (u32)ep_tx->gsi_mem_info.evt_ring_base_addr;
	db_val += ((in->is_smmu_enabled) ? in->u_tx.tx_smmu.event_ring_size :
		in->u_tx.tx.event_ring_size);
	db_val += ((ipa3_ctx->ipa_hw_type >= IPA_HW_v4_9) ?
		GSI_EVT_RING_RE_SIZE_32B : GSI_EVT_RING_RE_SIZE_16B);
	iowrite32(db_val, db_addr);
	IPADBG("db_addr %u  TX base_addr 0x%x evt wp val: 0x%x\n",
		evt_ring_db_addr_low,
		ep_tx->gsi_mem_info.evt_ring_base_addr, db_val);

	/* setup tx1 ep cfg */
	if (in->is_tx1_used &&
		ipa3_ctx->is_wdi3_tx1_needed && (ipa_ep_idx_tx1 !=
		IPA_EP_NOT_ALLOCATED) && (ipa_ep_idx_tx1 <
		IPA3_MAX_NUM_PIPES)) {
		ep_tx1->valid = 1;
		ep_tx1->client = tx1_client;
		result = ipa3_disable_data_path(ipa_ep_idx_tx1);
		if (result) {
			IPAERR("disable data path failed res=%d ep=%d.\n",
				result, ipa_ep_idx_tx1);
			result = -EFAULT;
			goto fail;
		}

		if (in->is_smmu_enabled == false)
			memcpy(&ep_tx1->cfg, &in->u_tx1.tx.ipa_ep_cfg,
				sizeof(ep_tx1->cfg));
		else
			memcpy(&ep_tx1->cfg, &in->u_tx1.tx_smmu.ipa_ep_cfg,
				sizeof(ep_tx1->cfg));

		ep_tx1->cfg.aggr.aggr_en = IPA_ENABLE_AGGR;
		ep_tx1->cfg.aggr.aggr = IPA_GENERIC;
		ep_tx1->cfg.aggr.aggr_byte_limit = IPA_WLAN_AGGR_BYTE_LIMIT;
		ep_tx1->cfg.aggr.aggr_pkt_limit = IPA_WLAN_AGGR_PKT_LIMIT;
		ep_tx1->cfg.aggr.aggr_hard_byte_limit_en = IPA_ENABLE_AGGR;
		if (ipa3_cfg_ep(ipa_ep_idx_tx1, &ep_tx1->cfg)) {
			IPAERR("fail to setup tx pipe cfg\n");
			result = -EFAULT;
			goto fail;
		}

		/* setup TX1 gsi channel */
		if (ipa3_setup_wdi3_gsi_channel(in->is_smmu_enabled,
			&in->u_tx1.tx, &in->u_tx1.tx_smmu, IPA_WDI3_TX1_DIR,
			ep_tx1)) {
			IPAERR("fail to setup wdi3 gsi tx1 channel\n");
			result = -EFAULT;
			goto fail;
		}

		if (gsi_query_channel_db_addr(ep_tx1->gsi_chan_hdl,
			&gsi_db_addr_low, &gsi_db_addr_high)) {
			IPAERR("failed to query gsi tx1 db addr\n");
			result = -EFAULT;
			goto fail;
		}

		/* only 32 bit lsb is used */
		out->tx1_uc_db_pa = (phys_addr_t)(gsi_db_addr_low);
		IPADBG("out->tx1_uc_db_pa %llu\n", out->tx1_uc_db_pa);
		IPADBG("client %d (ep: %d) connected\n", tx1_client,
			ipa_ep_idx_tx1);

		/* ring initial event ring dbs */
		gsi_query_evt_ring_db_addr(ep_tx1->gsi_evt_ring_hdl,
			&evt_ring_db_addr_low, &evt_ring_db_addr_high);
		/* only 32 bit lsb is used */
		db_addr = ioremap((phys_addr_t)(evt_ring_db_addr_low), 4);
		/*
		 * IPA/GSI driver should ring the event DB once after
		 * initialization of the event, with a value that is
		 * outside of the ring range. Eg: ring base = 0x1000,
		 * ring size = 0x100 => AP can write value > 0x1100
		 * into the doorbell address. Eg: 0x 1110
		 * Use event ring base addr + event ring size + 1 element size.
		 */
		db_val = (u32)ep_tx1->gsi_mem_info.evt_ring_base_addr;
		db_val += ((in->is_smmu_enabled) ?
					in->u_tx1.tx_smmu.event_ring_size :
					in->u_tx1.tx.event_ring_size);
		db_val += GSI_EVT_RING_RE_SIZE_16B;
		iowrite32(db_val, db_addr);
		IPADBG("db_addr %u  TX1 base_addr 0x%x evt wp val: 0x%x\n",
			evt_ring_db_addr_low,
			ep_tx1->gsi_mem_info.evt_ring_base_addr, db_val);
	}

fail:
	IPA_ACTIVE_CLIENTS_DEC_SIMPLE();
	return result;
}
EXPORT_SYMBOL(ipa3_conn_wdi3_pipes);

int ipa3_disconn_wdi3_pipes(int ipa_ep_idx_tx, int ipa_ep_idx_rx,
	int ipa_ep_idx_tx1)
{
	struct ipa3_ep_context *ep_tx, *ep_rx, *ep_tx1;
	enum ipa_client_type rx_client;
	enum ipa_client_type tx_client;
	int result = 0;

	/* wdi3 only support over gsi */
	if (ipa_get_wdi_version() < IPA_WDI_3) {
		IPAERR("wdi3 over uc offload not supported");
		WARN_ON(1);
		return -EFAULT;
	}

	IPADBG("ep_tx = %d\n", ipa_ep_idx_tx);
	IPADBG("ep_rx = %d\n", ipa_ep_idx_rx);
	IPADBG("ep_tx1 = %d\n", ipa_ep_idx_tx1);

	if (ipa_ep_idx_tx < 0 || ipa_ep_idx_tx >= ipa3_get_max_num_pipes() ||
		ipa_ep_idx_rx < 0 ||
		ipa_ep_idx_rx >= ipa3_get_max_num_pipes()) {
		IPAERR("invalid ipa ep index\n");
		return -EINVAL;
	}

	ep_tx = &ipa3_ctx->ep[ipa_ep_idx_tx];
	ep_rx = &ipa3_ctx->ep[ipa_ep_idx_rx];
	rx_client = ipa3_get_client_mapping(ipa_ep_idx_rx);
	tx_client = ipa3_get_client_mapping(ipa_ep_idx_tx);
	IPA_ACTIVE_CLIENTS_INC_EP(ipa3_get_client_mapping(ipa_ep_idx_tx));
	/* tear down tx1 pipe */
	if (ipa_ep_idx_tx1 >= 0) {
		ep_tx1 = &ipa3_ctx->ep[ipa_ep_idx_tx1];
		result = ipa3_reset_gsi_channel(ipa_ep_idx_tx1);
		if (result != GSI_STATUS_SUCCESS) {
			IPAERR("failed to reset gsi channel: %d.\n", result);
			goto exit;
		}
		result = gsi_reset_evt_ring(ep_tx1->gsi_evt_ring_hdl);
		if (result != GSI_STATUS_SUCCESS) {
			IPAERR("failed to reset evt ring: %d.\n", result);
			goto exit;
		}
		result = ipa3_release_gsi_channel(ipa_ep_idx_tx1);
		if (result) {
			IPAERR("failed to release gsi channel: %d\n", result);
			goto exit;
		}
		ipa3_release_wdi3_gsi_smmu_mappings(IPA_WDI3_TX1_DIR);

		memset(ep_tx1, 0, sizeof(struct ipa3_ep_context));
		IPADBG("tx client (ep: %d) disconnected\n", ipa_ep_idx_tx1);
	}

	/* tear down tx pipe */
	result = ipa3_reset_gsi_channel(ipa_ep_idx_tx);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to reset gsi channel: %d.\n", result);
		goto exit;
	}
	result = gsi_reset_evt_ring(ep_tx->gsi_evt_ring_hdl);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to reset evt ring: %d.\n", result);
		goto exit;
	}
	result = ipa3_release_gsi_channel(ipa_ep_idx_tx);
	if (result) {
		IPAERR("failed to release gsi channel: %d\n", result);
		goto exit;
	}
	if (tx_client == IPA_CLIENT_WLAN2_CONS)
		ipa3_release_wdi3_gsi_smmu_mappings(IPA_WDI3_TX_DIR);
	else
		ipa3_release_wdi3_gsi_smmu_mappings(IPA_WDI3_TX2_DIR);

	memset(ep_tx, 0, sizeof(struct ipa3_ep_context));
	IPADBG("tx client (ep: %d) disconnected\n", ipa_ep_idx_tx);

	/* tear down rx pipe */
	result = ipa3_reset_gsi_channel(ipa_ep_idx_rx);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to reset gsi channel: %d.\n", result);
		goto exit;
	}
	result = gsi_reset_evt_ring(ep_rx->gsi_evt_ring_hdl);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to reset evt ring: %d.\n", result);
		goto exit;
	}
	result = ipa3_release_gsi_channel(ipa_ep_idx_rx);
	if (result) {
		IPAERR("failed to release gsi channel: %d\n", result);
		goto exit;
	}
	if (rx_client == IPA_CLIENT_WLAN2_PROD)
		ipa3_release_wdi3_gsi_smmu_mappings(IPA_WDI3_RX_DIR);
	else
		ipa3_release_wdi3_gsi_smmu_mappings(IPA_WDI3_RX2_DIR);

	if (ipa3_ctx->ipa_hw_type >= IPA_HW_v4_5)
		ipa3_uc_debug_stats_dealloc(IPA_HW_PROTOCOL_WDI3);
	ipa3_delete_dflt_flt_rules(ipa_ep_idx_rx);
	memset(ep_rx, 0, sizeof(struct ipa3_ep_context));
	IPADBG("rx client (ep: %d) disconnected\n", ipa_ep_idx_rx);

exit:
	IPA_ACTIVE_CLIENTS_DEC_EP(ipa3_get_client_by_pipe(ipa_ep_idx_tx));
	return result;
}
EXPORT_SYMBOL(ipa3_disconn_wdi3_pipes);

int ipa3_enable_wdi3_pipes(int ipa_ep_idx_tx, int ipa_ep_idx_rx,
	int ipa_ep_idx_tx1)
{
	struct ipa3_ep_context *ep_tx, *ep_rx;
	struct ipa3_ep_context *ep_tx1 = NULL;
	int result = 0;
	struct ipa_ep_cfg_holb holb_cfg;
	u32 holb_max_cnt = ipa3_ctx->uc_ctx.holb_monitor.max_cnt_wlan;

	/* wdi3 only support over gsi */
	if (ipa_get_wdi_version() < IPA_WDI_3) {
		IPAERR("wdi3 over uc offload not supported");
		WARN_ON(1);
		return -EFAULT;
	}

	IPADBG("ep_tx = %d\n", ipa_ep_idx_tx);
	IPADBG("ep_rx = %d\n", ipa_ep_idx_rx);
	IPADBG("ep_tx1 = %d\n", ipa_ep_idx_tx1);

	ep_tx = &ipa3_ctx->ep[ipa_ep_idx_tx];
	ep_rx = &ipa3_ctx->ep[ipa_ep_idx_rx];
	if (ipa_ep_idx_tx1 >= 0)
		ep_tx1 = &ipa3_ctx->ep[ipa_ep_idx_tx1];

	IPA_ACTIVE_CLIENTS_INC_EP(ipa3_get_client_mapping(ipa_ep_idx_tx));

	/* start uC event ring */
	if (ipa3_ctx->ipa_hw_type >= IPA_HW_v4_5) {
		if (ipa3_ctx->uc_ctx.uc_loaded &&
			!ipa3_ctx->uc_ctx.uc_event_ring_valid) {
			if (ipa3_uc_setup_event_ring())	{
				IPAERR("failed to set uc_event ring\n");
				IPA_ACTIVE_CLIENTS_DEC_EP(ipa3_get_client_mapping(ipa_ep_idx_tx));
				return -EFAULT;
			}
		} else
			IPAERR("uc-loaded %d, ring-valid %d\n",
			ipa3_ctx->uc_ctx.uc_loaded,
			ipa3_ctx->uc_ctx.uc_event_ring_valid);
	}

	/* enable data path */
	result = ipa3_enable_data_path(ipa_ep_idx_rx);
	if (result) {
		IPAERR("enable data path failed res=%d clnt=%d\n", result,
			ipa_ep_idx_rx);
		goto exit;
	}

	result = ipa3_enable_data_path(ipa_ep_idx_tx);
	if (result) {
		IPAERR("enable data path failed res=%d clnt=%d\n", result,
			ipa_ep_idx_tx);
		goto fail_enable_path1;
	}

	/* Enable and config HOLB TO for both tx pipes */
	if (ipa_ep_idx_tx1 >= 0) {
		result = ipa3_enable_data_path(ipa_ep_idx_tx1);
		if (result) {
			IPAERR("enable data path failed res=%d clnt=%d\n",
				result, ipa_ep_idx_tx1);
			goto fail_enable_path2;
		}
		memset(&holb_cfg, 0, sizeof(holb_cfg));
		holb_cfg.en = IPA_HOLB_TMR_EN;
		holb_cfg.tmr_val = ipa3_ctx->ipa_wdi3_5g_holb_timeout;
		IPADBG("Configuring HOLB TO on tx return = %d\n",
			ipa3_cfg_ep_holb(ipa_ep_idx_tx, &holb_cfg));
		holb_cfg.tmr_val = ipa3_ctx->ipa_wdi3_2g_holb_timeout;
		IPADBG("Configuring HOLB TO on tx1 return = %d\n",
			ipa3_cfg_ep_holb(ipa_ep_idx_tx1, &holb_cfg));
	}

	/* start gsi tx channel */
	result = gsi_start_channel(ep_tx->gsi_chan_hdl);
	if (result) {
		IPAERR("failed to start gsi tx channel\n");
		goto fail_start_channel1;
	}

	/* start gsi tx1 channel */
	if (ipa_ep_idx_tx1 >= 0) {
		result = gsi_start_channel(ep_tx1->gsi_chan_hdl);
		if (result) {
			IPAERR("failed to start gsi tx1 channel\n");
			goto fail_start_channel2;
		}
	}

	result = ipa3_uc_client_add_holb_monitor(ep_tx->gsi_chan_hdl,
					HOLB_MONITOR_MASK, holb_max_cnt,
					IPA_EE_AP);
	if (result)
		IPAERR("Add HOLB monitor failed for gsi ch %d\n",
				ep_tx->gsi_chan_hdl);

	/* start gsi rx channel */
	result = gsi_start_channel(ep_rx->gsi_chan_hdl);
	if (result) {
		IPAERR("failed to start gsi rx channel\n");
		goto fail_start_channel3;
	}
	/* start uC gsi dbg stats monitor */
	if (ipa3_ctx->ipa_hw_type >= IPA_HW_v4_5) {
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[0].ch_id
			= ep_rx->gsi_chan_hdl;
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[0].dir
			= DIR_PRODUCER;
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[1].ch_id
			= ep_tx->gsi_chan_hdl;
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[1].dir
			= DIR_CONSUMER;
		if (ipa_ep_idx_tx1 >= 0) {
			ipa3_ctx->gsi_info[
				IPA_HW_PROTOCOL_WDI3].ch_id_info[2].ch_id
				= ep_tx1->gsi_chan_hdl;
			ipa3_ctx->gsi_info[
				IPA_HW_PROTOCOL_WDI3].ch_id_info[2].dir
				= DIR_CONSUMER;
		}
		ipa3_uc_debug_stats_alloc(
			ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3]);
	}
	goto exit;

fail_start_channel3:
	if (ipa_ep_idx_tx1 >= 0)
		gsi_stop_channel(ep_tx1->gsi_chan_hdl);
fail_start_channel2:
	gsi_stop_channel(ep_tx->gsi_chan_hdl);
fail_start_channel1:
	if (ipa_ep_idx_tx1 >= 0)
		ipa3_disable_data_path(ipa_ep_idx_tx1);
fail_enable_path2:
	ipa3_disable_data_path(ipa_ep_idx_tx);
fail_enable_path1:
	ipa3_disable_data_path(ipa_ep_idx_rx);
exit:
	IPA_ACTIVE_CLIENTS_DEC_EP(ipa3_get_client_mapping(ipa_ep_idx_tx));
	return result;
}
EXPORT_SYMBOL(ipa3_enable_wdi3_pipes);

int ipa3_disable_wdi3_pipes(int ipa_ep_idx_tx, int ipa_ep_idx_rx,
	int ipa_ep_idx_tx1)
{
	int result = 0;
	struct ipa3_ep_context *ep;
	u32 source_pipe_bitmask = 0;
	u32 source_pipe_reg_idx = 0;
	bool disable_force_clear = false;
	struct ipahal_ep_cfg_ctrl_scnd ep_ctrl_scnd = { 0 };

	/* wdi3 only support over gsi */
	if (ipa_get_wdi_version() < IPA_WDI_3) {
		IPAERR("wdi3 over uc offload not supported");
		WARN_ON(1);
		return -EFAULT;
	}

	IPA_ACTIVE_CLIENTS_INC_SIMPLE();

	/* disable tx data path */
	result = ipa3_disable_data_path(ipa_ep_idx_tx);
	if (result) {
		IPAERR("disable data path failed res=%d clnt=%d.\n", result,
			ipa_ep_idx_tx);
		result = -EFAULT;
		goto fail;
	}

	/* disable tx1 data path */
	if (ipa_ep_idx_tx1 >= 0) {
		result = ipa3_disable_data_path(ipa_ep_idx_tx1);
		if (result) {
			IPAERR("disable data path failed res=%d clnt=%d.\n", result,
				ipa_ep_idx_tx1);
			result = -EFAULT;
			goto fail;
		}
	}

	/* disable rx data path */
	result = ipa3_disable_data_path(ipa_ep_idx_rx);
	if (result) {
		IPAERR("disable data path failed res=%d clnt=%d.\n", result,
			ipa_ep_idx_rx);
		result = -EFAULT;
		goto fail;
	}
	/*
	 * For WDI 3.0 need to ensure pipe will be empty before suspend
	 * as IPA uC will fail to suspend the pipe otherwise.
	 */
	ep = &ipa3_ctx->ep[ipa_ep_idx_rx];
	if (IPA_CLIENT_IS_PROD(ep->client)) {
		source_pipe_bitmask = ipahal_get_ep_bit(ipa_ep_idx_rx);
		source_pipe_reg_idx = ipahal_get_ep_reg_idx(ipa_ep_idx_rx);
		result = ipa3_enable_force_clear(ipa_ep_idx_rx,
				false, source_pipe_bitmask,
					source_pipe_reg_idx);
		if (result) {
			/*
			 * assuming here modem SSR, AP can remove
			 * the delay in this case
			 */
			IPAERR("failed to force clear %d\n", result);
			IPAERR("remove delay from SCND reg\n");
			if (ipa3_ctx->ipa_endp_delay_wa_v2) {
				ipa3_remove_secondary_flow_ctrl(
							ep->gsi_chan_hdl);
			} else {
				ep_ctrl_scnd.endp_delay = false;
				ipahal_write_reg_n_fields(
						IPA_ENDP_INIT_CTRL_SCND_n,
						ipa_ep_idx_rx,
						&ep_ctrl_scnd);
			}
		} else {
			disable_force_clear = true;
		}
	}

	/* stop gsi rx channel */
	result = ipa3_stop_gsi_channel(ipa_ep_idx_rx);
	if (result) {
		IPAERR("failed to stop gsi rx channel\n");
		result = -EFAULT;
		goto fail;
	}

	/* stop gsi tx channel */
	result = ipa3_stop_gsi_channel(ipa_ep_idx_tx);
	if (result) {
		IPAERR("failed to stop gsi tx channel\n");
		result = -EFAULT;
		goto fail;
	}
	/* stop gsi tx1 channel */
	if (ipa_ep_idx_tx1 >= 0) {
		result = ipa3_stop_gsi_channel(ipa_ep_idx_tx1);
		if (result) {
			IPAERR("failed to stop gsi tx1 channel\n");
			result = -EFAULT;
			goto fail;
		}
	}
	/* stop uC gsi dbg stats monitor */
	if (ipa3_ctx->ipa_hw_type >= IPA_HW_v4_5) {
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[0].ch_id
			= 0xff;
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[0].dir
			= DIR_PRODUCER;
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[1].ch_id
			= 0xff;
		ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3].ch_id_info[1].dir
			= DIR_CONSUMER;
		if (ipa_ep_idx_tx1 >= 0) {
			ipa3_ctx->gsi_info[
				IPA_HW_PROTOCOL_WDI3].ch_id_info[2].ch_id
				= 0xff;
			ipa3_ctx->gsi_info[
				IPA_HW_PROTOCOL_WDI3].ch_id_info[2].dir
				= DIR_CONSUMER;
		}
		ipa3_uc_debug_stats_alloc(
			ipa3_ctx->gsi_info[IPA_HW_PROTOCOL_WDI3]);
	}
	if (disable_force_clear)
		ipa3_disable_force_clear(ipa_ep_idx_rx);

fail:
	IPA_ACTIVE_CLIENTS_DEC_SIMPLE();
	return result;

}
EXPORT_SYMBOL(ipa3_disable_wdi3_pipes);

int ipa3_write_qmapid_wdi3_gsi_pipe(u32 clnt_hdl, u8 qmap_id)
{
	int result = 0;
	struct ipa3_ep_context *ep;
	union __packed gsi_wdi3_channel_scratch2_reg scratch2_reg;

	memset(&scratch2_reg, 0, sizeof(scratch2_reg));
	if (clnt_hdl >= ipa3_ctx->ipa_num_pipes ||
		ipa3_ctx->ep[clnt_hdl].valid == 0) {
		IPAERR_RL("bad parm, %d\n", clnt_hdl);
		return -EINVAL;
	}
	ep = &ipa3_ctx->ep[clnt_hdl];
	IPA_ACTIVE_CLIENTS_INC_EP(ipa3_get_client_mapping(clnt_hdl));
	result = gsi_read_wdi3_channel_scratch2_reg(ep->gsi_chan_hdl,
			&scratch2_reg);

	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to read channel scratch2 reg %d\n", result);
		goto exit;
	}

	scratch2_reg.wdi.qmap_id = qmap_id;
	result = gsi_write_wdi3_channel_scratch2_reg(ep->gsi_chan_hdl,
			scratch2_reg);
	if (result != GSI_STATUS_SUCCESS) {
		IPAERR("failed to write channel scratch2 reg %d\n", result);
		goto exit;
	}

exit:
	IPA_ACTIVE_CLIENTS_DEC_EP(ipa3_get_client_mapping(clnt_hdl));
	return result;
}

/**
 * ipa3_get_wdi3_gsi_stats() - Query WDI3 gsi stats from uc
 * @stats:	[inout] stats blob from client populated by driver
 *
 * Returns:	0 on success, negative on failure
 *
 * @note Cannot be called from atomic context
 *
 */
int ipa3_get_wdi3_gsi_stats(struct ipa_uc_dbg_ring_stats *stats)
{
	int i;

	if (!ipa3_ctx->wdi3_ctx.dbg_stats.uc_dbg_stats_mmio)
		return -EINVAL;

	IPA_ACTIVE_CLIENTS_INC_SIMPLE();
	for (i = 0; i < MAX_WDI3_CHANNELS; i++) {
		stats->u.ring[i].ringFull = ioread32(
			ipa3_ctx->wdi3_ctx.dbg_stats.uc_dbg_stats_mmio
			+ i * IPA3_UC_DEBUG_STATS_OFF +
			IPA3_UC_DEBUG_STATS_RINGFULL_OFF);
		stats->u.ring[i].ringEmpty = ioread32(
			ipa3_ctx->wdi3_ctx.dbg_stats.uc_dbg_stats_mmio
			+ i * IPA3_UC_DEBUG_STATS_OFF +
			IPA3_UC_DEBUG_STATS_RINGEMPTY_OFF);
		stats->u.ring[i].ringUsageHigh = ioread32(
			ipa3_ctx->wdi3_ctx.dbg_stats.uc_dbg_stats_mmio
			+ i * IPA3_UC_DEBUG_STATS_OFF +
			IPA3_UC_DEBUG_STATS_RINGUSAGEHIGH_OFF);
		stats->u.ring[i].ringUsageLow = ioread32(
			ipa3_ctx->wdi3_ctx.dbg_stats.uc_dbg_stats_mmio
			+ i * IPA3_UC_DEBUG_STATS_OFF +
			IPA3_UC_DEBUG_STATS_RINGUSAGELOW_OFF);
		stats->u.ring[i].RingUtilCount = ioread32(
			ipa3_ctx->wdi3_ctx.dbg_stats.uc_dbg_stats_mmio
			+ i * IPA3_UC_DEBUG_STATS_OFF +
			IPA3_UC_DEBUG_STATS_RINGUTILCOUNT_OFF);
	}
	IPA_ACTIVE_CLIENTS_DEC_SIMPLE();

	return 0;
}
