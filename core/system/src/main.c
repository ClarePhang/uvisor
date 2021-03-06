/*
 * Copyright (c) 2013-2016, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <uvisor.h>
#include "debug.h"
#include "page_allocator.h"
#include "svc.h"
#include "unvic.h"
#include "vmpu.h"

UVISOR_NOINLINE void uvisor_init_pre(uint32_t const * const user_vtor)
{
    /* Reset the uVisor own BSS section. */
    memset(&__bss_start__, 0, VMPU_REGION_SIZE(&__bss_start__, &__bss_end__));

    /* Initialize the uVisor own data. */
    memcpy(&__data_start__, &__data_start_src__, VMPU_REGION_SIZE(&__data_start__, &__data_end__));

    /* Initialize the unprivileged NVIC module. */
    unvic_init(user_vtor);

    /* Initialize the debugging features. */
    DEBUG_INIT();
}

static void sanity_check_priv_sys_hooks(UvisorPrivSystemHooks const * priv_sys_hooks)
{
    /* Check that the table is in flash. */
    if (!vmpu_public_flash_addr((uint32_t) priv_sys_hooks) ||
        !vmpu_public_flash_addr((uint32_t) priv_sys_hooks + sizeof(priv_sys_hooks))) {
        HALT_ERROR(SANITY_CHECK_FAILED, "priv_sys_hooks (0x%08x) not entirely in public flash\n", priv_sys_hooks);
    }

    /*
     * Check that each hook is in flash, if the hook is non-0.
     */
    if (__uvisor_config.priv_sys_hooks->priv_svc_0 &&
        !vmpu_public_flash_addr((uint32_t) __uvisor_config.priv_sys_hooks->priv_svc_0)) {
        HALT_ERROR(SANITY_CHECK_FAILED, "priv_svc_0 (0x%08x) not entirely in public flash\n",
                   __uvisor_config.priv_sys_hooks->priv_svc_0);
    }

    if (__uvisor_config.priv_sys_hooks->priv_pendsv &&
        !vmpu_public_flash_addr((uint32_t) __uvisor_config.priv_sys_hooks->priv_pendsv)) {
        HALT_ERROR(SANITY_CHECK_FAILED, "priv_pendsv (0x%08x) not entirely in public flash\n",
                   __uvisor_config.priv_sys_hooks->priv_pendsv);
    }

    if (__uvisor_config.priv_sys_hooks->priv_systick &&
        !vmpu_public_flash_addr((uint32_t) __uvisor_config.priv_sys_hooks->priv_systick)) {
        HALT_ERROR(SANITY_CHECK_FAILED, "priv_systick (0x%08x) not entirely in public flash\n",
                   __uvisor_config.priv_sys_hooks->priv_pendsv);
    }

    if (__uvisor_config.priv_sys_hooks->priv_os_suspend &&
        !vmpu_public_flash_addr((uint32_t) __uvisor_config.priv_sys_hooks->priv_os_suspend)) {
        HALT_ERROR(SANITY_CHECK_FAILED, "priv_os_suspend (0x%08x) not entirely in public flash\n",
                   __uvisor_config.priv_sys_hooks->priv_os_suspend);
    }

    if (__uvisor_config.priv_sys_hooks->priv_uvisor_semaphore_post &&
        !vmpu_public_flash_addr((uint32_t) __uvisor_config.priv_sys_hooks->priv_uvisor_semaphore_post)) {
        HALT_ERROR(SANITY_CHECK_FAILED, "priv_uvisor_semaphore_post (0x%08x) not entirely in public flash\n",
                   __uvisor_config.priv_sys_hooks->priv_uvisor_semaphore_post);
    }
}

static void load_priv_sys_hooks(void)
{
    /* Make sure the hook table is sane. */
    sanity_check_priv_sys_hooks(__uvisor_config.priv_sys_hooks);

    /*
     * Register each hook.
     */
    if (__uvisor_config.priv_sys_hooks->priv_svc_0) {
        g_priv_sys_hooks.priv_svc_0 = __uvisor_config.priv_sys_hooks->priv_svc_0;
    }

    if (__uvisor_config.priv_sys_hooks->priv_pendsv) {
        g_priv_sys_hooks.priv_pendsv = __uvisor_config.priv_sys_hooks->priv_pendsv;
    }

    if (__uvisor_config.priv_sys_hooks->priv_systick) {
        g_priv_sys_hooks.priv_systick = __uvisor_config.priv_sys_hooks->priv_systick;
    }

    if (__uvisor_config.priv_sys_hooks->priv_os_suspend) {
        g_priv_sys_hooks.priv_os_suspend = __uvisor_config.priv_sys_hooks->priv_os_suspend;
    }

    if (__uvisor_config.priv_sys_hooks->priv_uvisor_semaphore_post) {
        g_priv_sys_hooks.priv_uvisor_semaphore_post = __uvisor_config.priv_sys_hooks->priv_uvisor_semaphore_post;
    }
}

UVISOR_NOINLINE void uvisor_init_post(void)
{
    /* Inititalize the MPU. */
    vmpu_init_post();

    /* Initialize the page allocator. */
    page_allocator_init(__uvisor_config.page_start, __uvisor_config.page_end, __uvisor_config.page_size);

    /* Initialize the SVCall interface. */
    svc_init();

    /* Load the privileged system hooks. */
    load_priv_sys_hooks();

    DPRINTF("uvisor initialized\n");
}

void main_entry(void)
{
    /* Return immediately if the magic is invalid or uVisor is disabled.
     * This ensures that no uVisor feature that could halt the system is
     * active in disabled mode (for example, printing debug messages to the
     * semihosting port). */
    if (__uvisor_config.magic != UVISOR_MAGIC || !__uvisor_config.mode || *(__uvisor_config.mode) == 0) {
        return;
    }

    /* Early uVisor initialization. */
    uvisor_init_pre((uint32_t *) SCB->VTOR);

    /* Run basic sanity checks. */
    if (vmpu_init_pre() == 0) {
        /* Disable all IRQs to perform atomic pointers swaps. */
        __disable_irq();

        /* Swap the vector tables. */
        SCB->VTOR = (uint32_t) &g_isr_vector;

        /* Swap the stack pointers. */
        __set_PSP(__get_MSP());
        __set_MSP((uint32_t) &__stack_top__);

        /* Re-enable all IRQs. */
        __enable_irq();

        /* Finish the uVisor initialization */
        uvisor_init_post();

        /* Switch to unprivileged mode.
         * This is possible as the uVisor code is readable in unprivileged mode. */
        __set_CONTROL(__get_CONTROL() | 3);
        __ISB();
        __DSB();
    }
}
