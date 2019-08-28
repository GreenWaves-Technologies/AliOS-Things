/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */


#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <k_api.h>
#include <k_mm.h>
#include <hal/base.h>
#include <aos/kernel.h>
#include <aos/hal/uart.h>
#include "board.h"
#include "cores/TARGET_RISCV_32/pmsis_gcc.h"
#include "pmsis.h"
#include "pmsis/rtos/event_kernel/event_kernel.h"

#define AOS_START_STACK 512

extern int application_start(int argc, char **argv);
extern int vfs_init(void);
extern int vfs_device_init(void);
extern uart_dev_t uart_0;

void __systick_handler(void);

ktask_t *g_aos_app;
static void sys_init(void)
{
#if 0 // to be on the safe side!
#ifdef AOS_VFS
    vfs_init();
    vfs_device_init();
#endif

#ifdef CONFIG_AOS_CLI
    aos_cli_init();
#endif

#ifdef WITH_SAL
    sal_device_init();
#endif

// port to PMSIS on top?
#ifdef AOS_LOOP
    //aos_loop_init();
#endif
#endif
    
    // prepare to catch soc events + pmu init and core clock update 
    // (need to be in that order)
    pi_fc_event_handler_init(FC_SOC_EVENT_IRQN);
    pi_pmu_init();
    SystemCoreClockUpdate();

    // prepare systicks
    system_setup_systick(RHINO_CONFIG_TICKS_PER_SECOND);

    // prepare default event kernel/workqueue for pmsis drivers
    struct pmsis_event_kernel_wrap *wrap;
    pmsis_event_kernel_init(&wrap, pmsis_event_kernel_main);
    pmsis_event_set_default_scheduler(wrap);
    // just make sure irqs are enabled
    __enable_irq();

    // client application start
    application_start(0, NULL);
}

static void platform_init(void)
{
#ifdef __USE_DEBUG_CONSOLE__
    board_init_debug_console();
#endif
}

#define us2tick(us) \
    ((us * RHINO_CONFIG_TICKS_PER_SECOND + 999999) / 1000000)

void hal_reboot(void)
{
    event_system_reset();
}


void __systick_handler(void)
{
    krhino_intrpt_enter();
    krhino_tick_proc();
    krhino_intrpt_exit();
}

extern char __heapl2ram_start;
extern char __heapl2ram_size;

//#define PRINTF_USE_UART

int main(void)
{
    platform_init();
    aos_init();
    krhino_task_dyn_create(&g_aos_app, "aos-init", 0, AOS_DEFAULT_APP_PRI, 0, AOS_START_STACK*2, (task_entry_t)sys_init, 1);
#ifdef PRINTF_USE_UART
    hal_uart_init(&uart_0);
#endif

    // uses first_task_start
    aos_start();

    return 0;
}
