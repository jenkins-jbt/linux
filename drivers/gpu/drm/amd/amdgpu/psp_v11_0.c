/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ras.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v11_0.h"

#include "mp/mp_11_0_offset.h"
#include "mp/mp_11_0_sh_mask.h"
#include "gc/gc_9_0_offset.h"
#include "sdma0/sdma0_4_0_offset.h"
#include "nbio/nbio_7_4_offset.h"

#include "oss/osssys_4_0_offset.h"
#include "oss/osssys_4_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/vega20_sos.bin");
MODULE_FIRMWARE("amdgpu/vega20_asd.bin");
MODULE_FIRMWARE("amdgpu/vega20_ta.bin");
MODULE_FIRMWARE("amdgpu/navi10_sos.bin");
MODULE_FIRMWARE("amdgpu/navi10_asd.bin");
MODULE_FIRMWARE("amdgpu/navi10_ta.bin");
MODULE_FIRMWARE("amdgpu/navi14_sos.bin");
MODULE_FIRMWARE("amdgpu/navi14_asd.bin");
MODULE_FIRMWARE("amdgpu/navi14_ta.bin");
MODULE_FIRMWARE("amdgpu/navi12_sos.bin");
MODULE_FIRMWARE("amdgpu/navi12_asd.bin");
MODULE_FIRMWARE("amdgpu/navi12_ta.bin");
MODULE_FIRMWARE("amdgpu/arcturus_sos.bin");
MODULE_FIRMWARE("amdgpu/arcturus_asd.bin");
MODULE_FIRMWARE("amdgpu/arcturus_ta.bin");
MODULE_FIRMWARE("amdgpu/sienna_cichlid_sos.bin");
MODULE_FIRMWARE("amdgpu/sienna_cichlid_ta.bin");
MODULE_FIRMWARE("amdgpu/navy_flounder_sos.bin");
MODULE_FIRMWARE("amdgpu/navy_flounder_asd.bin");

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024
/* navi10 reg offset define */
#define mmRLC_GPM_UCODE_ADDR_NV10	0x5b61
#define mmRLC_GPM_UCODE_DATA_NV10	0x5b62
#define mmSDMA0_UCODE_ADDR_NV10		0x5880
#define mmSDMA0_UCODE_DATA_NV10		0x5881
/* memory training timeout define */
#define MEM_TRAIN_SEND_MSG_TIMEOUT_US	3000000

/* For large FW files the time to complete can be very long */
#define USBC_PD_POLLING_LIMIT_S 240

static int psp_v11_0_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	const char *chip_name;
	char fw_name[30];
	int err = 0;
	const struct ta_firmware_header_v1_0 *ta_hdr;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		chip_name = "vega20";
		break;
	case CHIP_NAVI10:
		chip_name = "navi10";
		break;
	case CHIP_NAVI14:
		chip_name = "navi14";
		break;
	case CHIP_NAVI12:
		chip_name = "navi12";
		break;
	case CHIP_ARCTURUS:
		chip_name = "arcturus";
		break;
	case CHIP_SIENNA_CICHLID:
		chip_name = "sienna_cichlid";
		break;
	case CHIP_NAVY_FLOUNDER:
		chip_name = "navy_flounder";
		break;
	default:
		BUG();
	}

	err = psp_init_sos_microcode(psp, chip_name);
	if (err)
		return err;

	if (adev->asic_type != CHIP_SIENNA_CICHLID &&
	    adev->asic_type != CHIP_NAVY_FLOUNDER) {
		err = psp_init_asd_microcode(psp, chip_name);
		if (err)
			return err;
	}

	switch (adev->asic_type) {
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_ta.bin", chip_name);
		err = request_firmware(&adev->psp.ta_fw, fw_name, adev->dev);
		if (err) {
			release_firmware(adev->psp.ta_fw);
			adev->psp.ta_fw = NULL;
			dev_info(adev->dev,
				 "psp v11.0: Failed to load firmware \"%s\"\n", fw_name);
		} else {
			err = amdgpu_ucode_validate(adev->psp.ta_fw);
			if (err)
				goto out2;

			ta_hdr = (const struct ta_firmware_header_v1_0 *)adev->psp.ta_fw->data;
			adev->psp.ta_xgmi_ucode_version = le32_to_cpu(ta_hdr->ta_xgmi_ucode_version);
			adev->psp.ta_xgmi_ucode_size = le32_to_cpu(ta_hdr->ta_xgmi_size_bytes);
			adev->psp.ta_xgmi_start_addr = (uint8_t *)ta_hdr +
				le32_to_cpu(ta_hdr->header.ucode_array_offset_bytes);
			adev->psp.ta_fw_version = le32_to_cpu(ta_hdr->header.ucode_version);
			adev->psp.ta_ras_ucode_version = le32_to_cpu(ta_hdr->ta_ras_ucode_version);
			adev->psp.ta_ras_ucode_size = le32_to_cpu(ta_hdr->ta_ras_size_bytes);
			adev->psp.ta_ras_start_addr = (uint8_t *)adev->psp.ta_xgmi_start_addr +
				le32_to_cpu(ta_hdr->ta_ras_offset_bytes);
		}
		break;
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		if (amdgpu_sriov_vf(adev))
			break;
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_ta.bin", chip_name);
		err = request_firmware(&adev->psp.ta_fw, fw_name, adev->dev);
		if (err) {
			release_firmware(adev->psp.ta_fw);
			adev->psp.ta_fw = NULL;
			dev_info(adev->dev,
				 "psp v11.0: Failed to load firmware \"%s\"\n", fw_name);
		} else {
			err = amdgpu_ucode_validate(adev->psp.ta_fw);
			if (err)
				goto out2;

			ta_hdr = (const struct ta_firmware_header_v1_0 *)adev->psp.ta_fw->data;
			adev->psp.ta_hdcp_ucode_version = le32_to_cpu(ta_hdr->ta_hdcp_ucode_version);
			adev->psp.ta_hdcp_ucode_size = le32_to_cpu(ta_hdr->ta_hdcp_size_bytes);
			adev->psp.ta_hdcp_start_addr = (uint8_t *)ta_hdr +
				le32_to_cpu(ta_hdr->header.ucode_array_offset_bytes);

			adev->psp.ta_fw_version = le32_to_cpu(ta_hdr->header.ucode_version);

			adev->psp.ta_dtm_ucode_version = le32_to_cpu(ta_hdr->ta_dtm_ucode_version);
			adev->psp.ta_dtm_ucode_size = le32_to_cpu(ta_hdr->ta_dtm_size_bytes);
			adev->psp.ta_dtm_start_addr = (uint8_t *)adev->psp.ta_hdcp_start_addr +
				le32_to_cpu(ta_hdr->ta_dtm_offset_bytes);
		}
		break;
	case CHIP_SIENNA_CICHLID:
		err = psp_init_ta_microcode(&adev->psp, chip_name);
		if (err)
			return err;
		break;
	case CHIP_NAVY_FLOUNDER:
		break;
	default:
		BUG();
	}

	return 0;

out2:
	release_firmware(adev->psp.ta_fw);
	adev->psp.ta_fw = NULL;
	return err;
}

int psp_v11_0_wait_for_bootloader(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;

	int ret;
	int retry_loop;

	for (retry_loop = 0; retry_loop < 10; retry_loop++) {
		/* Wait for bootloader to signify that is
		    ready having bit 31 of C2PMSG_35 set to 1 */
		ret = psp_wait_for(psp,
				   SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
				   0x80000000,
				   0x80000000,
				   false);

		if (ret == 0)
			return 0;
	}

	return ret;
}

static bool psp_v11_0_is_sos_alive(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t sol_reg;

	sol_reg = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81);

	return sol_reg != 0x0;
}

static int psp_v11_0_bootloader_load_kdb(struct psp_context *psp)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check tOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v11_0_is_sos_alive(psp))
		return 0;

	ret = psp_v11_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy PSP KDB binary to memory */
	memcpy(psp->fw_pri_buf, psp->kdb_start_addr, psp->kdb_bin_size);

	/* Provide the PSP KDB to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_KEY_DATABASE;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	ret = psp_v11_0_wait_for_bootloader(psp);

	return ret;
}

static int psp_v11_0_bootloader_load_spl(struct psp_context *psp)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check tOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v11_0_is_sos_alive(psp))
		return 0;

	ret = psp_v11_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy PSP SPL binary to memory */
	memcpy(psp->fw_pri_buf, psp->spl_start_addr, psp->spl_bin_size);

	/* Provide the PSP SPL to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_TOS_SPL_TABLE;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	ret = psp_v11_0_wait_for_bootloader(psp);

	return ret;
}

static int psp_v11_0_bootloader_load_sysdrv(struct psp_context *psp)
{
	int ret;
	uint32_t psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v11_0_is_sos_alive(psp))
		return 0;

	ret = psp_v11_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy PSP System Driver binary to memory */
	memcpy(psp->fw_pri_buf, psp->sys_start_addr, psp->sys_bin_size);

	/* Provide the sys driver to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SYSDRV;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);

	ret = psp_v11_0_wait_for_bootloader(psp);

	return ret;
}

static int psp_v11_0_bootloader_load_sos(struct psp_context *psp)
{
	int ret;
	unsigned int psp_gfxdrv_command_reg = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	if (psp_v11_0_is_sos_alive(psp))
		return 0;

	ret = psp_v11_0_wait_for_bootloader(psp);
	if (ret)
		return ret;

	memset(psp->fw_pri_buf, 0, PSP_1_MEG);

	/* Copy Secure OS binary to PSP memory */
	memcpy(psp->fw_pri_buf, psp->sos_start_addr, psp->sos_bin_size);

	/* Provide the PSP secure OS to bootloader */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36,
	       (uint32_t)(psp->fw_pri_mc_addr >> 20));
	psp_gfxdrv_command_reg = PSP_BL__LOAD_SOSDRV;
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35,
	       psp_gfxdrv_command_reg);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_81),
			   RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_81),
			   0, true);

	return ret;
}

static void psp_v11_0_reroute_ih(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t tmp;

	/* Change IH ring for VMC */
	tmp = REG_SET_FIELD(0, IH_CLIENT_CFG_DATA, CREDIT_RETURN_ADDR, 0x1244b);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, CLIENT_TYPE, 1);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, RING_ID, 1);

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, 3);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, tmp);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, GFX_CTRL_CMD_ID_GBR_IH_SET);

	mdelay(20);
	psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
		     0x80000000, 0x8000FFFF, false);

	/* Change IH ring for UMC */
	tmp = REG_SET_FIELD(0, IH_CLIENT_CFG_DATA, CREDIT_RETURN_ADDR, 0x1216b);
	tmp = REG_SET_FIELD(tmp, IH_CLIENT_CFG_DATA, RING_ID, 1);

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, 4);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, tmp);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, GFX_CTRL_CMD_ID_GBR_IH_SET);

	mdelay(20);
	psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
		     0x80000000, 0x8000FFFF, false);
}

static int psp_v11_0_ring_init(struct psp_context *psp,
			      enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring;
	struct amdgpu_device *adev = psp->adev;

	if ((!amdgpu_sriov_vf(adev)) &&
	    (adev->asic_type != CHIP_SIENNA_CICHLID) &&
	    (adev->asic_type != CHIP_NAVY_FLOUNDER))
		psp_v11_0_reroute_ih(psp);

	ring = &psp->km_ring;

	ring->ring_type = ring_type;

	/* allocate 4k Page of Local Frame Buffer memory for ring */
	ring->ring_size = 0x1000;
	ret = amdgpu_bo_create_kernel(adev, ring->ring_size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->firmware.rbuf,
				      &ring->ring_mem_mc_addr,
				      (void **)&ring->ring_mem);
	if (ret) {
		ring->ring_size = 0;
		return ret;
	}

	return 0;
}

static int psp_v11_0_ring_stop(struct psp_context *psp,
			      enum psp_ring_type ring_type)
{
	int ret = 0;
	struct amdgpu_device *adev = psp->adev;

	/* Write the ring destroy command*/
	if (amdgpu_sriov_vf(adev))
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
				     GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING);
	else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64,
				     GFX_CTRL_CMD_ID_DESTROY_RINGS);

	/* there might be handshake issue with hardware which needs delay */
	mdelay(20);

	/* Wait for response flag (bit 31) */
	if (amdgpu_sriov_vf(adev))
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
				   0x80000000, 0x80000000, false);
	else
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);

	return ret;
}

static int psp_v11_0_ring_create(struct psp_context *psp,
				enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		ret = psp_v11_0_ring_stop(psp, ring_type);
		if (ret) {
			DRM_ERROR("psp_v11_0_ring_stop_sriov failed!\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_102 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_103 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_103, psp_ring_reg);

		/* Write the ring initialization command to C2PMSG_101 */
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101,
					     GFX_CTRL_CMD_ID_INIT_GPCOM_RING);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_101),
				   0x80000000, 0x8000FFFF, false);

	} else {
		/* Wait for sOS ready for ring creation */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);
		if (ret) {
			DRM_ERROR("Failed to wait for sOS ready for ring creation\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_69 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_69, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_70 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_70, psp_ring_reg);
		/* Write size of ring to C2PMSG_71 */
		psp_ring_reg = ring->ring_size;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_71, psp_ring_reg);
		/* Write the ring initialization command to C2PMSG_64 */
		psp_ring_reg = ring_type;
		psp_ring_reg = psp_ring_reg << 16;
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_64, psp_ring_reg);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64),
				   0x80000000, 0x8000FFFF, false);
	}

	return ret;
}


static int psp_v11_0_ring_destroy(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v11_0_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static int psp_v11_0_mode1_reset(struct psp_context *psp)
{
	int ret;
	uint32_t offset;
	struct amdgpu_device *adev = psp->adev;

	offset = SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_64);

	ret = psp_wait_for(psp, offset, 0x80000000, 0x8000FFFF, false);

	if (ret) {
		DRM_INFO("psp is not working correctly before mode1 reset!\n");
		return -EINVAL;
	}

	/*send the mode 1 reset command*/
	WREG32(offset, GFX_CTRL_CMD_ID_MODE1_RST);

	msleep(500);

	offset = SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_33);

	ret = psp_wait_for(psp, offset, 0x80000000, 0x80000000, false);

	if (ret) {
		DRM_INFO("psp mode 1 reset failed!\n");
		return -EINVAL;
	}

	DRM_INFO("psp mode1 reset succeed \n");

	return 0;
}

static int psp_v11_0_memory_training_send_msg(struct psp_context *psp, int msg)
{
	int ret;
	int i;
	uint32_t data_32;
	int max_wait;
	struct amdgpu_device *adev = psp->adev;

	data_32 = (psp->mem_train_ctx.c2p_train_data_offset >> 20);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36, data_32);
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, msg);

	max_wait = MEM_TRAIN_SEND_MSG_TIMEOUT_US / adev->usec_timeout;
	for (i = 0; i < max_wait; i++) {
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
				   0x80000000, 0x80000000, false);
		if (ret == 0)
			break;
	}
	if (i < max_wait)
		ret = 0;
	else
		ret = -ETIME;

	DRM_DEBUG("training %s %s, cost %d @ %d ms\n",
		  (msg == PSP_BL__DRAM_SHORT_TRAIN) ? "short" : "long",
		  (ret == 0) ? "succeed" : "failed",
		  i, adev->usec_timeout/1000);
	return ret;
}

/*
 * save and restore proces
 */
static int psp_v11_0_memory_training(struct psp_context *psp, uint32_t ops)
{
	struct psp_memory_training_context *ctx = &psp->mem_train_ctx;
	uint32_t *pcache = (uint32_t*)ctx->sys_cache;
	struct amdgpu_device *adev = psp->adev;
	uint32_t p2c_header[4];
	uint32_t sz;
	void *buf;
	int ret;

	if (ctx->init == PSP_MEM_TRAIN_NOT_SUPPORT) {
		DRM_DEBUG("Memory training is not supported.\n");
		return 0;
	} else if (ctx->init != PSP_MEM_TRAIN_INIT_SUCCESS) {
		DRM_ERROR("Memory training initialization failure.\n");
		return -EINVAL;
	}

	if (psp_v11_0_is_sos_alive(psp)) {
		DRM_DEBUG("SOS is alive, skip memory training.\n");
		return 0;
	}

	amdgpu_device_vram_access(adev, ctx->p2c_train_data_offset, p2c_header, sizeof(p2c_header), false);
	DRM_DEBUG("sys_cache[%08x,%08x,%08x,%08x] p2c_header[%08x,%08x,%08x,%08x]\n",
		  pcache[0], pcache[1], pcache[2], pcache[3],
		  p2c_header[0], p2c_header[1], p2c_header[2], p2c_header[3]);

	if (ops & PSP_MEM_TRAIN_SEND_SHORT_MSG) {
		DRM_DEBUG("Short training depends on restore.\n");
		ops |= PSP_MEM_TRAIN_RESTORE;
	}

	if ((ops & PSP_MEM_TRAIN_RESTORE) &&
	    pcache[0] != MEM_TRAIN_SYSTEM_SIGNATURE) {
		DRM_DEBUG("sys_cache[0] is invalid, restore depends on save.\n");
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	if (p2c_header[0] == MEM_TRAIN_SYSTEM_SIGNATURE &&
	    !(pcache[0] == MEM_TRAIN_SYSTEM_SIGNATURE &&
	      pcache[3] == p2c_header[3])) {
		DRM_DEBUG("sys_cache is invalid or out-of-date, need save training data to sys_cache.\n");
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	if ((ops & PSP_MEM_TRAIN_SAVE) &&
	    p2c_header[0] != MEM_TRAIN_SYSTEM_SIGNATURE) {
		DRM_DEBUG("p2c_header[0] is invalid, save depends on long training.\n");
		ops |= PSP_MEM_TRAIN_SEND_LONG_MSG;
	}

	if (ops & PSP_MEM_TRAIN_SEND_LONG_MSG) {
		ops &= ~PSP_MEM_TRAIN_SEND_SHORT_MSG;
		ops |= PSP_MEM_TRAIN_SAVE;
	}

	DRM_DEBUG("Memory training ops:%x.\n", ops);

	if (ops & PSP_MEM_TRAIN_SEND_LONG_MSG) {
		/*
		 * Long traing will encroach certain mount of bottom VRAM,
		 * saving the content of this bottom VRAM to system memory
		 * before training, and restoring it after training to avoid
		 * VRAM corruption.
		 */
		sz = GDDR6_MEM_TRAINING_ENCROACHED_SIZE;

		if (adev->gmc.visible_vram_size < sz || !adev->mman.aper_base_kaddr) {
			DRM_ERROR("visible_vram_size %llx or aper_base_kaddr %p is not initialized.\n",
				  adev->gmc.visible_vram_size,
				  adev->mman.aper_base_kaddr);
			return -EINVAL;
		}

		buf = vmalloc(sz);
		if (!buf) {
			DRM_ERROR("failed to allocate system memory.\n");
			return -ENOMEM;
		}

		memcpy_fromio(buf, adev->mman.aper_base_kaddr, sz);
		ret = psp_v11_0_memory_training_send_msg(psp, PSP_BL__DRAM_LONG_TRAIN);
		if (ret) {
			DRM_ERROR("Send long training msg failed.\n");
			vfree(buf);
			return ret;
		}

		memcpy_toio(adev->mman.aper_base_kaddr, buf, sz);
		adev->nbio.funcs->hdp_flush(adev, NULL);
		vfree(buf);
	}

	if (ops & PSP_MEM_TRAIN_SAVE) {
		amdgpu_device_vram_access(psp->adev, ctx->p2c_train_data_offset, ctx->sys_cache, ctx->train_data_size, false);
	}

	if (ops & PSP_MEM_TRAIN_RESTORE) {
		amdgpu_device_vram_access(psp->adev, ctx->c2p_train_data_offset, ctx->sys_cache, ctx->train_data_size, true);
	}

	if (ops & PSP_MEM_TRAIN_SEND_SHORT_MSG) {
		ret = psp_v11_0_memory_training_send_msg(psp, (amdgpu_force_long_training > 0) ?
							 PSP_BL__DRAM_LONG_TRAIN : PSP_BL__DRAM_SHORT_TRAIN);
		if (ret) {
			DRM_ERROR("send training msg failed.\n");
			return ret;
		}
	}
	ctx->training_cnt++;
	return 0;
}

static uint32_t psp_v11_0_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev))
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102);
	else
		data = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67);

	return data;
}

static void psp_v11_0_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_102, value);
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_101, GFX_CTRL_CMD_ID_CONSUME_CMD);
	} else
		WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_67, value);
}

static int psp_v11_0_load_usbc_pd_fw(struct psp_context *psp, dma_addr_t dma_addr)
{
	struct amdgpu_device *adev = psp->adev;
	uint32_t reg_status;
	int ret, i = 0;

	/* Write lower 32-bit address of the PD Controller FW */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36, lower_32_bits(dma_addr));
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			     0x80000000, 0x80000000, false);
	if (ret)
		return ret;

	/* Fireup interrupt so PSP can pick up the lower address */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, 0x800000);
	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			     0x80000000, 0x80000000, false);
	if (ret)
		return ret;

	reg_status = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35);

	if ((reg_status & 0xFFFF) != 0) {
		DRM_ERROR("Lower address load failed - MP0_SMN_C2PMSG_35.Bits [15:0] = %02x...\n",
				reg_status & 0xFFFF);
		return -EIO;
	}

	/* Write upper 32-bit address of the PD Controller FW */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36, upper_32_bits(dma_addr));

	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
			     0x80000000, 0x80000000, false);
	if (ret)
		return ret;

	/* Fireup interrupt so PSP can pick up the upper address */
	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, 0x4000000);

	/* FW load takes very long time */
	do {
		msleep(1000);
		reg_status = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35);

		if (reg_status & 0x80000000)
			goto done;

	} while (++i < USBC_PD_POLLING_LIMIT_S);

	return -ETIME;
done:

	if ((reg_status & 0xFFFF) != 0) {
		DRM_ERROR("Upper address load failed - MP0_SMN_C2PMSG_35.Bits [15:0] = x%04x\n",
				reg_status & 0xFFFF);
		return -EIO;
	}

	return 0;
}

static int psp_v11_0_read_usbc_pd_fw(struct psp_context *psp, uint32_t *fw_ver)
{
	struct amdgpu_device *adev = psp->adev;
	int ret;

	WREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_35, C2PMSG_CMD_GFX_USB_PD_FW_VER);

	ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, mmMP0_SMN_C2PMSG_35),
				     0x80000000, 0x80000000, false);
	if (!ret)
		*fw_ver = RREG32_SOC15(MP0, 0, mmMP0_SMN_C2PMSG_36);

	return ret;
}

static const struct psp_funcs psp_v11_0_funcs = {
	.init_microcode = psp_v11_0_init_microcode,
	.bootloader_load_kdb = psp_v11_0_bootloader_load_kdb,
	.bootloader_load_spl = psp_v11_0_bootloader_load_spl,
	.bootloader_load_sysdrv = psp_v11_0_bootloader_load_sysdrv,
	.bootloader_load_sos = psp_v11_0_bootloader_load_sos,
	.ring_init = psp_v11_0_ring_init,
	.ring_create = psp_v11_0_ring_create,
	.ring_stop = psp_v11_0_ring_stop,
	.ring_destroy = psp_v11_0_ring_destroy,
	.mode1_reset = psp_v11_0_mode1_reset,
	.mem_training = psp_v11_0_memory_training,
	.ring_get_wptr = psp_v11_0_ring_get_wptr,
	.ring_set_wptr = psp_v11_0_ring_set_wptr,
	.load_usbc_pd_fw = psp_v11_0_load_usbc_pd_fw,
	.read_usbc_pd_fw = psp_v11_0_read_usbc_pd_fw
};

void psp_v11_0_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v11_0_funcs;
}
