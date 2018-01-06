//
//  IntelWifi_trans-gen2.cpp
//  IntelWifi
//
//  Created by Roman Peshkov on 04/01/2018.
//  Copyright © 2018 Roman Peshkov. All rights reserved.
//

/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/


#include "IntelWifi.hpp"

/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via iwl_pcie_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int IntelWifi::iwl_pcie_gen2_apm_init(struct iwl_trans *trans)
{
    int ret = 0;
    
    IWL_DEBUG_INFO(trans, "Init card's basic functions\n");
    
    /*
     * Use "set_bit" below rather than "write", to preserve any hardware
     * bits already set by default after reset.
     */
    
    /*
     * Disable L0s without affecting L1;
     * don't wait for ICH L0s (ICH bug W/A)
     */
    io->iwl_set_bit(CSR_GIO_CHICKEN_BITS,
                CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);
    
    /* Set FH wait threshold to maximum (HW error during stress W/A) */
    io->iwl_set_bit(CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);
    
    /*
     * Enable HAP INTA (interrupt from management bus) to
     * wake device's PCI Express link L1a -> L0s
     */
    io->iwl_set_bit(CSR_HW_IF_CONFIG_REG,
                CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);
    
    iwl_pcie_apm_config(trans);
    
    /*
     * Set "initialization complete" bit to move adapter from
     * D0U* --> D0A* (powered-up active) state.
     */
    io->iwl_set_bit(CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
    
    /*
     * Wait for clock stabilization; once stabilized, access to
     * device-internal resources is supported, e.g. iwl_write_prph()
     * and accesses to uCode SRAM.
     */
    ret = io->iwl_poll_bit(CSR_GP_CNTRL,
                       CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
                       CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
    if (ret < 0) {
        IWL_DEBUG_INFO(trans, "Failed to init the card\n");
        return ret;
    }
    
    set_bit(STATUS_DEVICE_ENABLED, &trans->status);
    
    return 0;
}

void IntelWifi::iwl_pcie_gen2_apm_stop(struct iwl_trans *trans, bool op_mode_leave)
{
    IWL_DEBUG_INFO(trans, "Stop card, put in low power state\n");
    
    if (op_mode_leave) {
        if (!test_bit(STATUS_DEVICE_ENABLED, &trans->status))
            iwl_pcie_gen2_apm_init(trans);
        
        /* inform ME that we are leaving */
        io->iwl_set_bit(CSR_DBG_LINK_PWR_MGMT_REG,
                    CSR_RESET_LINK_PWR_MGMT_DISABLED);
        io->iwl_set_bit(CSR_HW_IF_CONFIG_REG,
                    CSR_HW_IF_CONFIG_REG_PREPARE |
                    CSR_HW_IF_CONFIG_REG_ENABLE_PME);
        IODelay(1);
        io->iwl_clear_bit(CSR_DBG_LINK_PWR_MGMT_REG,
                      CSR_RESET_LINK_PWR_MGMT_DISABLED);
        IODelay(5);
    }
    
    clear_bit(STATUS_DEVICE_ENABLED, &trans->status);
    
    /* Stop device's DMA activity */
    iwl_pcie_apm_stop_master(trans);
    
    iwl_pcie_sw_reset(trans);
    
    /*
     * Clear "initialization complete" bit to move adapter from
     * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
     */
    io->iwl_clear_bit(CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

void IntelWifi::_iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans, bool low_power)
{
    struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    
    //lockdep_assert_held(&trans_pcie->mutex);
    
    if (trans_pcie->is_down)
        return;
    
    trans_pcie->is_down = true;
    
    /* tell the device to stop sending interrupts */
    iwl_disable_interrupts(trans);
    
    /* device going down, Stop using ICT table */
    iwl_pcie_disable_ict(trans);
    
    /*
     * If a HW restart happens during firmware loading,
     * then the firmware loading might call this function
     * and later it might be called again due to the
     * restart. So don't process again if the device is
     * already dead.
     */
    if (test_and_clear_bit(STATUS_DEVICE_ENABLED, &trans->status)) {
        IWL_DEBUG_INFO(trans,
                       "DEVICE_ENABLED bit was set and is now cleared\n");
        // TODO: Implement
//        iwl_pcie_gen2_tx_stop(trans);
//        iwl_pcie_rx_stop(trans);
    }

    // TODO: Implement
//    iwl_pcie_ctxt_info_free_paging(trans);
//    iwl_pcie_ctxt_info_free(trans);
    
    /* Make sure (redundant) we've released our request to stay awake */
    io->iwl_clear_bit(CSR_GP_CNTRL,
                  CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    
    /* Stop the device, and put it in low power state */
    iwl_pcie_gen2_apm_stop(trans, false);
    
    iwl_pcie_sw_reset(trans);
    
    /*
     * Upon stop, the IVAR table gets erased, so msi-x won't
     * work. This causes a bug in RF-KILL flows, since the interrupt
     * that enables radio won't fire on the correct irq, and the
     * driver won't be able to handle the interrupt.
     * Configure the IVAR table again after reset.
     */
    iwl_pcie_conf_msix_hw(trans_pcie);
    
    /*
     * Upon stop, the APM issues an interrupt if HW RF kill is set.
     * This is a bug in certain verions of the hardware.
     * Certain devices also keep sending HW RF kill interrupt all
     * the time, unless the interrupt is ACKed even if the interrupt
     * should be masked. Re-ACK all the interrupts here.
     */
    iwl_disable_interrupts(trans);
    
    /* clear all status bits */
    clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
    clear_bit(STATUS_INT_ENABLED, &trans->status);
    clear_bit(STATUS_TPOWER_PMI, &trans->status);
    
    /*
     * Even if we stop the HW, we still want the RF kill
     * interrupt
     */
    iwl_enable_rfkill_int(trans);
    
    /* re-take ownership to prevent other users from stealing the device */
    iwl_pcie_prepare_card_hw(trans);
}

void IntelWifi::iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans, bool low_power)
{
    struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    bool was_in_rfkill;
    
    //mutex_lock(&trans_pcie->mutex);
    IOLockLock(trans_pcie->mutex);
    trans_pcie->opmode_down = true;
    was_in_rfkill = test_bit(STATUS_RFKILL_OPMODE, &trans->status);
    _iwl_trans_pcie_gen2_stop_device(trans, low_power);
    iwl_trans_pcie_handle_stop_rfkill(trans, was_in_rfkill);
    //mutex_unlock(&trans_pcie->mutex);
    IOLockUnlock(trans_pcie->mutex);
}

int IntelWifi::iwl_pcie_gen2_nic_init(struct iwl_trans *trans)
{
    struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    
    /* TODO: most of the logic can be removed in A0 - but not in Z0 */
    //spin_lock(&trans_pcie->irq_lock);
    IOSimpleLockLock(trans_pcie->irq_lock);
    iwl_pcie_gen2_apm_init(trans);
    //spin_unlock(&trans_pcie->irq_lock);
    IOSimpleLockUnlock(trans_pcie->irq_lock);
    
    iwl_op_mode_nic_config(trans->op_mode);
    
    /* Allocate the RX queue, or reset if it is already allocated */
    // TODO: Implement
//    if (iwl_pcie_gen2_rx_init(trans))
//        return -ENOMEM;
    
    /* Allocate or reset and init all Tx and Command queues */
    // TODO: Implement
//    if (iwl_pcie_gen2_tx_init(trans))
//        return -ENOMEM;
    
    /* enable shadow regs in HW */
    io->iwl_set_bit(CSR_MAC_SHADOW_REG_CTRL, 0x800FFFFF);
    IWL_DEBUG_INFO(trans, "Enabling shadow registers in device\n");
    
    return 0;
}

void IntelWifi::iwl_trans_pcie_gen2_fw_alive(struct iwl_trans *trans, u32 scd_addr)
{
    struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    
    iwl_pcie_reset_ict(trans);
    
    /* make sure all queue are not stopped/used */
    // TODO: Implement
//    memset(trans_pcie->queue_stopped, 0, sizeof(trans_pcie->queue_stopped));
//    memset(trans_pcie->queue_used, 0, sizeof(trans_pcie->queue_used));
    
    /* now that we got alive we can free the fw image & the context info.
     * paging memory cannot be freed included since FW will still use it
     */
    // TODO: Implement
    //iwl_pcie_ctxt_info_free(trans);
}

int IntelWifi::iwl_trans_pcie_gen2_start_fw(struct iwl_trans *trans,
                                 const struct fw_img *fw, bool run_in_rfkill)
{
    struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
    bool hw_rfkill;
    int ret;
    
    /* This may fail if AMT took ownership of the device */
    if (iwl_pcie_prepare_card_hw(trans)) {
        IWL_WARN(trans, "Exit HW not ready\n");
        ret = -EIO;
        goto out;
    }
    
    iwl_enable_rfkill_int(trans);
    
    io->iwl_write32(CSR_INT, 0xFFFFFFFF);
    
    /*
     * We enabled the RF-Kill interrupt and the handler may very
     * well be running. Disable the interrupts to make sure no other
     * interrupt can be fired.
     */
    iwl_disable_interrupts(trans);
    
    /* Make sure it finished running */
    // TODO: Implement
    //iwl_pcie_synchronize_irqs(trans);
    
    //mutex_lock(&trans_pcie->mutex);
    IOLockLock(trans_pcie->mutex);
    
    /* If platform's RF_KILL switch is NOT set to KILL */
    hw_rfkill = iwl_pcie_check_hw_rf_kill(trans);
    if (hw_rfkill && !run_in_rfkill) {
        ret = -ERFKILL;
        goto out;
    }
    
    /* Someone called stop_device, don't try to start_fw */
    if (trans_pcie->is_down) {
        IWL_WARN(trans,
                 "Can't start_fw since the HW hasn't been started\n");
        ret = -EIO;
        goto out;
    }
    
    /* make sure rfkill handshake bits are cleared */
    io->iwl_write32(CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
    io->iwl_write32(CSR_UCODE_DRV_GP1_CLR,
                CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
    
    /* clear (again), then enable host interrupts */
    io->iwl_write32(CSR_INT, 0xFFFFFFFF);
    
    ret = iwl_pcie_gen2_nic_init(trans);
    if (ret) {
        IWL_ERR(trans, "Unable to init nic\n");
        goto out;
    }
    
    // TODO: Implement
    //ret = iwl_pcie_ctxt_info_init(trans, fw);
    if (ret)
        goto out;
    
    /* re-check RF-Kill state since we may have missed the interrupt */
    hw_rfkill = iwl_pcie_check_hw_rf_kill(trans);
    if (hw_rfkill && !run_in_rfkill)
        ret = -ERFKILL;
    
out:
    //mutex_unlock(&trans_pcie->mutex);
    IOLockUnlock(trans_pcie->mutex);
    return ret;
}

