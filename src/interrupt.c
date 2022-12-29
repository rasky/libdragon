/**
 * @file interrupt.c
 * @brief Interrupt Controller
 * @ingroup interrupt
 */
#include <malloc.h>
#include "libdragon.h"
#include "regsinternal.h"

/**
 * @defgroup interrupt Interrupt Controller
 * @ingroup lowlevel
 * @brief N64 interrupt registering and servicing routines.
 *
 * The N64 interrupt controller provides a software interface to
 * register for interrupts from the various systems in the N64.
 * Most interrupts on the N64 coordinate through the MIPS interface
 * (MI) to allow interrupts to be handled at one spot.  A notable
 * exception is the timer interrupt which is generated by the MIPS
 * r4300 itself and not the N64 hardware.
 *
 * The interrupt controller is automatically initialized before
 * main is called. By default, all interrupts are enabled and any
 * registered callback can be called when an interrupt occurs.
 * Each of the N64-generated interrupts is maskable using the various
 * set accessors.
 *
 * Interrupts can be enabled or disabled as a whole on the N64 using
 * #enable_interrupts and #disable_interrupts.  It is assumed that
 * once the interrupt system is activated, these will always be called
 * in pairs.  Calling #enable_interrupts without first calling
 * #disable_interrupts is considered a violation of this assumption
 * and should be avoided.  Calling #disable_interrupts when interrupts
 * are already disabled will have no effect.  Calling #enable_interrupts
 * again to restore from a critical section will not enable interrupts
 * if interrupts were not enabled when calling #disable_interrupts.
 * In this manner, it is safe to nest calls to disable and enable
 * interrupts.
 *
 * @{
 */

/** @brief SP interrupt bit */
#define MI_INTR_SP 0x01
/** @brief SI interrupt bit */
#define MI_INTR_SI 0x02
/** @brief AI interrupt bit */
#define MI_INTR_AI 0x04
/** @brief VI interrupt bit */
#define MI_INTR_VI 0x08
/** @brief PI interrupt bit */
#define MI_INTR_PI 0x10
/** @brief DP interrupt bit */
#define MI_INTR_DP 0x20

/** @brief SP mask bit */
#define MI_MASK_SP 0x01
/** @brief SI mask bit */
#define MI_MASK_SI 0x02
/** @brief AI mask bit */
#define MI_MASK_AI 0x04
/** @brief VI mask bit */
#define MI_MASK_VI 0x08
/** @brief PI mask bit */
#define MI_MASK_PI 0x10
/** @brief DP mask bit */
#define MI_MASK_DP 0x20

/** @brief Clear SP mask */
#define MI_MASK_CLR_SP 0x0001
/** @brief Set SP mask */
#define MI_MASK_SET_SP 0x0002
/** @brief Clear SI mask */
#define MI_MASK_CLR_SI 0x0004
/** @brief Set SI mask */
#define MI_MASK_SET_SI 0x0008
/** @brief Clear AI mask */
#define MI_MASK_CLR_AI 0x0010
/** @brief Set AI mask */
#define MI_MASK_SET_AI 0x0020
/** @brief Clear VI mask */
#define MI_MASK_CLR_VI 0x0040
/** @brief Set VI mask */
#define MI_MASK_SET_VI 0x0080
/** @brief Clear PI mask */
#define MI_MASK_CLR_PI 0x0100
/** @brief Set PI mask */
#define MI_MASK_SET_PI 0x0200
/** @brief Clear DP mask */
#define MI_MASK_CLR_DP 0x0400
/** @brief Set DP mask */
#define MI_MASK_SET_DP 0x0800

/** @brief Bit to set to clear the PI interrupt */
#define PI_CLEAR_INTERRUPT 0x02
/** @brief Bit to set to clear the SI interrupt */
#define SI_CLEAR_INTERRUPT 0
/** @brief Bit to set to clear the SP interrupt */
#define SP_CLEAR_INTERRUPT 0x08
/** @brief Bit to set to clear the DP interrupt */
#define DP_CLEAR_INTERRUPT 0x0800
/** @brief Bit to set to clear the AI interrupt */
#define AI_CLEAR_INTERRUPT 0

/** @brief Number of nested disable interrupt calls
 *
 * This will represent the number of disable interrupt calls made on the system.
 * If this is set to 0, interrupts are enabled.  A number higher than 0 represents
 * that many disable calls that were nested, and consequently the number of
 * interrupt enable calls that need to be made to re-enable interrupts.  A negative
 * number means that the interrupt system hasn't been initialized yet.
 */
static int __interrupt_depth = -1;

/** @brief Value of the status register at the moment interrupts
 *         got disabled.
 */
static int __interrupt_sr = 0;

/** @brief tick at which interrupts were disabled. */
uint32_t interrupt_disabled_tick = 0;

/**
 * @brief Structure of an interrupt callback
 */
typedef struct callback_link
{
    /** @brief Callback function */
    void (*callback)();
    /** @brief Pointer to next callback */
    struct callback_link * next;
} _callback_link;

/** @brief Static structure to address AI registers */
static volatile struct AI_regs_s * const AI_regs = (struct AI_regs_s *)0xa4500000;
/** @brief Static structure to address MI registers */
static volatile struct MI_regs_s * const MI_regs = (struct MI_regs_s *)0xa4300000;
/** @brief Static structure to address VI registers */
static volatile struct VI_regs_s * const VI_regs = (struct VI_regs_s *)0xa4400000;
/** @brief Static structure to address PI registers */
static volatile struct PI_regs_s * const PI_regs = (struct PI_regs_s *)0xa4600000;
/** @brief Static structure to address SI registers */
static volatile struct SI_regs_s * const SI_regs = (struct SI_regs_s *)0xa4800000;
/** @brief Static structure to address SP registers */
static volatile struct SP_regs_s * const SP_regs = (struct SP_regs_s *)0xa4040000;

/** @brief Linked list of AI callbacks */
struct callback_link * AI_callback = 0;
/** @brief Linked list of VI callbacks */
struct callback_link * VI_callback = 0;
/** @brief Linked list of PI callbacks */
struct callback_link * PI_callback = 0;
/** @brief Linked list of DP callbacks */
struct callback_link * DP_callback = 0;
/** @brief Linked list of SI callbacks */
struct callback_link * SI_callback = 0;
/** @brief Linked list of SP callbacks */
struct callback_link * SP_callback = 0;
/** @brief Linked list of TI callbacks */
struct callback_link * TI_callback = 0;
/** @brief Linked list of CART callbacks */
struct callback_link * CART_callback = 0;

/** @brief Maximum number of reset handlers that can be registered. */
#define MAX_RESET_HANDLERS 4

/** @brief Pre-NMI exception handlers */
static void (*__prenmi_handlers[MAX_RESET_HANDLERS])(void);
/** @brief Tick at which the pre-NMI was triggered */
static uint32_t __prenmi_tick;

static int last_cart_interrupt_count = 0;

/** 
 * @brief Call each callback in a linked list of callbacks
 *
 * @param[in] head
 *            Pointer to the head of a callback linke list
 */
static void __call_callback( struct callback_link * head )
{
    /* Call each registered callback */
    while( head )
    {
        if( head->callback )
        {
        	head->callback();
        }

        /* Go to next */
	    head=head->next;
    }
}

/**
 * @brief Add a callback to a linked list of callbacks
 *
 * @param[in,out] head
 *                Pointer to the head of a linked list to add to
 * @param[in]     callback
 *                Function to call when executing callbacks in this linked list
 */
static void __register_callback( struct callback_link ** head, void (*callback)() )
{
    if( head )
    {
        /* Add to beginning of linked list */
        struct callback_link *next = *head;
        (*head) = malloc(sizeof(struct callback_link));

        if( *head )
        {
            (*head)->next=next;
            (*head)->callback=callback;
        }
    }
}

/**
 * @brief Remove a callback from a linked list of callbacks
 *
 * @param[in,out] head
 *                Pointer to the head of a linked list to remove from
 * @param[in]     callback
 *                Function to search for and remove from callback list
 */
static void __unregister_callback( struct callback_link ** head, void (*callback)() )
{
    if( head )
    {
        /* Try to find callback this matches */
        struct callback_link *last = 0;
        struct callback_link *cur  = *head;

        while( cur )
        {
            if( cur->callback == callback )
            {
                /* We found it!  Try to remove it from the list */
                if( last )
                {
                    /* This is somewhere in the linked list */
                    last->next = cur->next;
                }
                else
                {
                    /* This is the first node */
                    *head = cur->next;
                }

                /* Free memory */
                free( cur );

                /* Exit early */
                break;
            }

            /* Go to next entry */
            last = cur;
            cur = cur->next;
        }
    }
}

/**
 * @brief Handle an MI interrupt
 *
 * @note This function handles most of the interrupts on the system as
 *       they come through the MI.
 */
void __MI_handler(void)
{
    unsigned long status = MI_regs->intr & MI_regs->mask;

    if( status & MI_INTR_SP )
    {
        /* Clear interrupt */
        SP_regs->status=SP_CLEAR_INTERRUPT;

        __call_callback(SP_callback);
    }

    if( status & MI_INTR_SI )
    {
        /* Clear interrupt */
        SI_regs->status=SI_CLEAR_INTERRUPT;

        __call_callback(SI_callback);
    }

    if( status & MI_INTR_AI )
    {
        /* Clear interrupt */
    	AI_regs->status=AI_CLEAR_INTERRUPT;

	    __call_callback(AI_callback);
    }

    if( status & MI_INTR_VI )
    {
        /* Clear interrupt */
    	VI_regs->cur_line=VI_regs->cur_line;

    	__call_callback(VI_callback);
    }

    if( status & MI_INTR_PI )
    {
        /* Clear interrupt */
        PI_regs->status=PI_CLEAR_INTERRUPT;

        __call_callback(PI_callback);
    }

    if( status & MI_INTR_DP )
    {
        /* Clear interrupt */
        MI_regs->mode=DP_CLEAR_INTERRUPT;

        __call_callback(DP_callback);
    }
}

/**
 * @brief Handle a timer interrupt
 */
void __TI_handler(void)
{
	/* NOTE: the timer interrupt is already acknowledged in inthandler.S */
    __call_callback(TI_callback);
}

/**
 * @brief Handle a CART interrupt
 */
void __CART_handler(void)
{
    /* Call the registered callbacks */
    __call_callback(CART_callback);

    #ifndef NDEBUG
     /* CART interrupts must be acknowledged by handlers. If the handler fails
       to do so, the console freezes because the interrupt will retrigger
       continuously. Since a freeze is always bad for debugging, try to 
       detect it, and show a proper assertion screen. */
    if (!(C0_CAUSE() & C0_INTERRUPT_CART))
        last_cart_interrupt_count = 0;
    else
        assertf(++last_cart_interrupt_count < 128, "CART interrupt deadlock: a CART interrupt is continuously triggering, with no ack");
    #endif
}


/**
 * @brief Handle a RESET (pre-NMI) interrupt.
 * 
 * Calls the handlers registered by #register_RESET_handler.
 */
void __RESET_handler( void )
{
	/* This function will be called many times becuase there is no way
	   to acknowledge the pre-NMI interrupt. So make sure it does nothing
	   after the first call. */
	if (__prenmi_tick) return;

	/* Store the tick at which we saw the exception. Make sure
	 * we never store 0 as we use that for "no reset happened". */
	__prenmi_tick = TICKS_READ() | 1;

	/* Call the registered handlers. */
	for (int i=0;i<MAX_RESET_HANDLERS;i++)
	{
		if (__prenmi_handlers[i])
			__prenmi_handlers[i]();
	}
}

/**
 * @brief Register an AI callback
 *
 * @param[in] callback
 *            Function to call when an AI interrupt occurs
 */
void register_AI_handler( void (*callback)() )
{
    __register_callback(&AI_callback,callback);
}

/**
 * @brief Unregister an AI callback
 *
 * @param[in] callback
 *            Function that should no longer be called on AI interrupts
 */
void unregister_AI_handler( void (*callback)() )
{
    __unregister_callback(&AI_callback,callback);
}

/**
 * @brief Register a VI callback
 *
 * @param[in] callback
 *            Function to call when a VI interrupt occurs
 */
void register_VI_handler( void (*callback)() )
{
    __register_callback(&VI_callback,callback);
}

/**
 * @brief Unregister a VI callback
 *
 * @param[in] callback
 *            Function that should no longer be called on VI interrupts
 */
void unregister_VI_handler( void (*callback)() )
{
    __unregister_callback(&VI_callback,callback);
}

/**
 * @brief Register a PI callback
 *
 * @param[in] callback
 *            Function to call when a PI interrupt occurs
 */
void register_PI_handler( void (*callback)() )
{
    __register_callback(&PI_callback,callback);
}

/**
 * @brief Unegister a PI callback
 *
 * @param[in] callback
 *            Function that should no longer be called on PI interrupts
 */
void unregister_PI_handler( void (*callback)() )
{
    __unregister_callback(&PI_callback,callback);
}

/**
 * @brief Register a DP callback
 *
 * @param[in] callback
 *            Function to call when a DP interrupt occurs
 */
void register_DP_handler( void (*callback)() )
{
    __register_callback(&DP_callback,callback);
}

/**
 * @brief Unregister a DP callback
 *
 * @param[in] callback
 *            Function that should no longer be called on DP interrupts
 */
void unregister_DP_handler( void (*callback)() )
{
    __unregister_callback(&DP_callback,callback);
}

/**
 * @brief Register a SI callback
 *
 * @param[in] callback
 *            Function to call when a SI interrupt occurs
 */
void register_SI_handler( void (*callback)() )
{
    __register_callback(&SI_callback,callback);
}

/**
 * @brief Unegister a SI callback
 *
 * @param[in] callback
 *            Function that should no longer be called on SI interrupts
 */
void unregister_SI_handler( void (*callback)() )
{
    __unregister_callback(&SI_callback,callback);
}

/**
 * @brief Register a SP callback
 *
 * @param[in] callback
 *            Function to call when a SP interrupt occurs
 */
void register_SP_handler( void (*callback)() )
{
    __register_callback(&SP_callback,callback);
}

/**
 * @brief Unegister a SP callback
 *
 * @param[in] callback
 *            Function that should no longer be called on SP interrupts
 */
void unregister_SP_handler( void (*callback)() )
{
    __unregister_callback(&SP_callback,callback);
}


/**
 * @brief Register a timer callback
 * 
 * The callback will be used when the timer interrupt is triggered by the CPU.
 * This happens when the COP0 COUNT register reaches the same value of the
 * COP0 COMPARE register.
 * 
 * This function is useful only if you want to do your own low level programming
 * of the internal CPU timer and handle the interrupt yourself. In this case,
 * also remember to activate the timer interrupt using #set_TI_interrupt.
 * 
 * @note If you use the timer library (#timer_init and #new_timer), you do not
 * need to call this function, as timer interrupt are already handled by the timer
 * library.
 *
 * @param[in] callback
 *            Function to call when a timer interrupt occurs
 */
void register_TI_handler( void (*callback)() )
{
    __register_callback(&TI_callback,callback);
}

/**
 * @brief Unregister a timer callback
 *
 * @note If you use the timer library (#timer_init and #new_timer), you do not
 * need to call this function, as timer interrupt are already handled by the timer
 * library.
 *
 * @param[in] callback
 *            Function that should no longer be called on timer interrupts
 */
void unregister_TI_handler( void (*callback)() )
{
    __unregister_callback(&TI_callback,callback);
}

/**
 * @brief Register a CART interrupt callback.
 * 
 * The callback will be called when a CART interrupt is triggered. CART interrupts
 * are interrupts triggered by devices attached to the PI bus (aka CART bus),
 * for instance the 64DD, or the modem cassette.
 * 
 * CART interrupts are disabled by default in libdragon. Use #set_CART_interrupt
 * to enable/disable them.
 * 
 * Notice that there is no generic way to acknowledge those interrupts, so if
 * you activate CART interrupts, make also sure to register an handler that
 * acknowledge them, otherwise the interrupt will deadlock the console.
 * 
 * @param[in] callback
 *            Function that should no longer be called on CART interrupts
 */
void register_CART_handler( void (*callback)() )
{
    __register_callback(&CART_callback,callback);
}

/**
 * @brief Unregister a CART interrupt callback
 *
 * @param[in] callback
 *            Function that should no longer be called on CART interrupts
 */
void unregister_CART_handler( void (*callback)() )
{
    __unregister_callback(&CART_callback,callback);
}

/**
 * @brief Register a handler that will be called when the user
 *        presses the RESET button. 
 * 
 * The N64 sends an interrupt when the RESET button is pressed,
 * and then actually resets the console after about ~500ms (but less
 * on some models, see #RESET_TIME_LENGTH).
 * 
 * Registering a handler can be used to perform a clean reset.
 * Technically, at the hardware level, it is important that the RCP
 * is completely idle when the reset happens, or it might freeze
 * and require a power-cycle to unfreeze. This means that any
 * I/O, audio, video activity must cease before #RESET_TIME_LENGTH
 * has elapsed.
 * 
 * This entry point can be used by the game code to basically
 * halts itself and stops issuing commands. Libdragon itself will
 * register handlers to halt internal modules so to provide a basic
 * good reset experience.
 * 
 * Handlers can use #exception_reset_time to read how much has passed
 * since the RESET button was pressed.
 * 
 * @param callback    Callback to invoke when the reset button is pressed.
 * 
 * @note  Reset handlers are called under interrupt.
 * 
 */
void register_RESET_handler( void (*callback)() )
{
	for (int i=0;i<MAX_RESET_HANDLERS;i++)
	{		
		if (!__prenmi_handlers[i])
		{
			__prenmi_handlers[i] = callback;
			return;
		}
	}
	assertf(0, "Too many pre-NMI handlers\n");
}

/**
 * @brief Unregister a RESET interrupt callback
 *
 * @param[in] callback
 *            Function that should no longer be called on RESET interrupts
 */
void unregister_RESET_handler( void (*callback)() )
{
    for (int i=0;i<MAX_RESET_HANDLERS;i++)
    {		
        if (__prenmi_handlers[i] == callback)
        {
            __prenmi_handlers[i] = NULL;
            return;
        }
    }
    assertf(0, "Reset handler not found\n");
}

/**
 * @brief Enable or disable the AI interrupt
 *
 * @param[in] active
 *            Flag to specify whether the AI interrupt should be active
 */
void set_AI_interrupt(int active)
{
    if( active )
    {
        MI_regs->mask=MI_MASK_SET_AI;
    }
    else
    {
        MI_regs->mask=MI_MASK_CLR_AI;
    }
}

/**
 * @brief Enable or disable the VI interrupt
 *
 * @param[in] active
 *            Flag to specify whether the VI interrupt should be active
 * @param[in] line
 *            The vertical line that causes this interrupt to fire.  Ignored
 *            when setting the interrupt inactive
 */
void set_VI_interrupt(int active, unsigned long line)
{
    if( active )
    {
    	MI_regs->mask=MI_MASK_SET_VI;
	    VI_regs->v_int=line;
    }
    else
    {
        MI_regs->mask=MI_MASK_CLR_VI;
    }
}

/**
 * @brief Enable or disable the PI interrupt
 *
 * @param[in] active
 *            Flag to specify whether the PI interrupt should be active
 */
void set_PI_interrupt(int active)
{
    if ( active )
    {
        MI_regs->mask=MI_MASK_SET_PI;
    }
    else
    {
        MI_regs->mask=MI_MASK_CLR_PI;
    }
}

/**
 * @brief Enable or disable the DP interrupt
 *
 * @param[in] active
 *            Flag to specify whether the DP interrupt should be active
 */
void set_DP_interrupt(int active)
{
    if( active )
    {
        MI_regs->mask=MI_MASK_SET_DP;
    }
    else
    {
        MI_regs->mask=MI_MASK_CLR_DP;
    }
}

/**
 * @brief Enable or disable the SI interrupt
 *
 * @param[in] active
 *            Flag to specify whether the SI interrupt should be active
 */
void set_SI_interrupt(int active)
{
    if( active )
    {
        MI_regs->mask=MI_MASK_SET_SI;
    }
    else
    {
        MI_regs->mask=MI_MASK_CLR_SI;
    }
}

/**
 * @brief Enable or disable the SP interrupt
 *
 * @param[in] active
 *            Flag to specify whether the SP interrupt should be active
 */
void set_SP_interrupt(int active)
{
    if( active )
    {
        MI_regs->mask=MI_MASK_SET_SP;
    }
    else
    {
        MI_regs->mask=MI_MASK_CLR_SP;
    }
}

/**
 * @brief Enable the timer interrupt
 * 
 * @note If you use the timer library (#timer_init and #new_timer), you do not
 * need to call this function, as timer interrupt is already handled by the timer
 * library.
 *
 * @param[in] active
 *            Flag to specify whether the timer interrupt should be active
 *
 * @see #register_TI_handler
 */
void set_TI_interrupt(int active)
{
    if( active )
    {
        C0_WRITE_STATUS(C0_STATUS() | C0_INTERRUPT_TIMER);
    }
    else
    {
        C0_WRITE_STATUS(C0_STATUS() & ~C0_INTERRUPT_TIMER);
    }
}

/**
 * @brief Enable the CART interrupt
 * 
 * @param[in] active
 *            Flag to specify whether the CART interrupt should be active
 * 
 * @see #register_CART_handler
 */
void set_CART_interrupt(int active)
{
    if( active )
    {
        C0_WRITE_STATUS(C0_STATUS() | C0_INTERRUPT_CART);
    }
    else
    {
        C0_WRITE_STATUS(C0_STATUS() & ~C0_INTERRUPT_CART);
    }
}

/**
 * @brief Enable the RESET interrupt
 * 
 * @param[in] active
 *            Flag to specify whether the RESET interrupt should be active
 * 
 * @note RESET interrupt is active by default.
 * 
 * @see #register_CART_handler
 */
void set_RESET_interrupt(int active)
{
    if( active )
    {
        C0_WRITE_STATUS(C0_STATUS() | C0_INTERRUPT_PRENMI);
    }
    else
    {
        C0_WRITE_STATUS(C0_STATUS() & ~C0_INTERRUPT_PRENMI);
    }
}


/**
 * @brief Initialize the interrupt controller
 */
__attribute__((constructor)) void __init_interrupts()
{
    /* Make sure that we aren't initializing interrupts when they are already enabled */
    if( __interrupt_depth < 0 )
    {
        /* Clear and mask all interrupts on the system so we start with a clean slate */
        MI_regs->mask=MI_MASK_CLR_SP|MI_MASK_CLR_SI|MI_MASK_CLR_AI|MI_MASK_CLR_VI|MI_MASK_CLR_PI|MI_MASK_CLR_DP;

        /* Set that we are enabled */
        __interrupt_depth = 0;

        /* Enable interrupts systemwide. We set the global interrupt enable,
           and then specifically enable RCP interrupts. */
        uint32_t sr = C0_STATUS();
        C0_WRITE_STATUS(sr | C0_STATUS_IE | C0_INTERRUPT_RCP | C0_INTERRUPT_PRENMI);
    }
}

/**
 * @brief Disable interrupts systemwide
 *
 * @note If interrupts are already disabled on the system or interrupts have not
 *       been initialized, this function will not modify the system state.
 */
void disable_interrupts()
{
    /* Don't do anything if we haven't initialized */
    if( __interrupt_depth < 0 ) { return; }

    if( __interrupt_depth == 0 )
    {
        /* We must disable the interrupts now. */
        uint32_t sr = C0_STATUS();
        C0_WRITE_STATUS(sr & ~C0_STATUS_IE);

        /* Save the original SR value away, so that we now if
           interrupts were enabled and whether to restore them.
           NOTE: this memory write must happen now that interrupts
           are disabled, otherwise it could cause a race condition
           because an interrupt could trigger and overwrite it.
           So put an explicit barrier. */
        MEMORY_BARRIER();
        __interrupt_sr = sr;

        interrupt_disabled_tick = TICKS_READ();
    }

    /* Ensure that we remember nesting levels */
    __interrupt_depth++;
}

/**
 * @brief Enable interrupts systemwide
 *
 * @note If this is called inside a nested disable call, it will have no effect on the
 *       system.  Therefore it is safe to nest disable/enable calls.  After the last
 *       nested interrupt is enabled, systemwide interrupts will be reenabled.
 */
void enable_interrupts()
{
    /* Don't do anything if we've hosed up or aren't initialized */
    if( __interrupt_depth < 0 ) { return; }

    /* Check that we're not calling enable_interrupts() more than expected */
    assertf(__interrupt_depth > 0, "unbalanced enable_interrupts() call");

    /* Decrement the nesting level now that we are enabling interrupts */
    __interrupt_depth--;

    if( __interrupt_depth == 0 )
    {
        /* Restore the interrupt state that was active when interrupts got
           disabled. This is important because, within an interrupt handler,
           we don't want here to force-enable interrupts, or we would allow
           reentrant interrupts which are not supported. */
        C0_WRITE_STATUS(C0_STATUS() | (__interrupt_sr & C0_STATUS_IE));
    }
}

/**
 * @brief Return the current state of interrupts
 *
 * @retval INTERRUPTS_UNINITIALIZED if the interrupt system has not been initialized yet.
 * @retval INTERRUPTS_DISABLED if interrupts have been disabled for some reason.
 * @retval INTERRUPTS_ENABLED if interrupts are currently enabled.
 */
interrupt_state_t get_interrupts_state()
{
    if( __interrupt_depth < 0 )
    {
        return INTERRUPTS_UNINITIALIZED;
    }
    else if( __interrupt_depth == 0 )
    {
        return INTERRUPTS_ENABLED;
    }
    else
    {
        return INTERRUPTS_DISABLED;
    }
}


/** 
 * @brief Check whether the RESET button was pressed and how long we are into
 *        the reset process.
 * 
 * This function returns how many ticks have elapsed since the user has pressed
 * the RESET button, or 0 if the user has not pressed it.
 * 
 * It can be used by user code to perform actions during the RESET
 * process (see #register_RESET_handler). It is also possible to simply
 * poll this value to check at any time if the button has been pressed or not.
 * 
 * The reset process takes about 500ms between the user pressing the
 * RESET button and the CPU being actually reset, though on some consoles
 * it seems to be much less. See #RESET_TIME_LENGTH for more information.
 * For the broadest compatibility, please use #RESET_TIME_LENGTH to implement
 * the reset logic.
 * 
 * Notice also that the reset process is initiated when the user presses the
 * button, but the reset will not happen until the user releases the button.
 * So keeping the button pressed is a good way to check if the application
 * actually winds down correctly.
 * 
 * @return Ticks elapsed since RESET button was pressed, or 0 if the RESET button
 *         was not pressed.
 * 
 * @see register_RESET_handler
 * @see #RESET_TIME_LENGTH
 */
uint32_t exception_reset_time( void )
{
	if (!__prenmi_tick) return 0;
	return TICKS_SINCE(__prenmi_tick);
}



/** @} */
