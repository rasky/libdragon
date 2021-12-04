/**
 * @file interruptinternal.h
 * @brief Interrupt Controller
 * @ingroup interrupt
 */
#ifndef __LIBDRAGON_INTERRUPTINTERNAL_H
#define __LIBDRAGON_INTERRUPTINTERNAL_H

void disable_interrupts_when(volatile uint32_t* reg, uint32_t mask);

#endif
