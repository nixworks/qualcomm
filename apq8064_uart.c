/*-
 * Copyright (c) 2014 Ganbold Tsagaankhuu <ganbold@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kdb.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include <arm/qualcomm/apq8064_uart.h>

#include "uart_if.h"

#define	DEF_CLK		7372800

#define GETREG(bas, reg)                                                \
                bus_space_read_4((bas)->bst, (bas)->bsh, (reg))
#define SETREG(bas, reg, value)                                         \
                bus_space_write_4((bas)->bst, (bas)->bsh, (reg), (value))

static int apq8064_uart_param(struct uart_bas *, int, int, int, int);

/*
 * Low-level UART interface.
 */
static int apq8064_probe(struct uart_bas *bas);
static void apq8064_init(struct uart_bas *bas, int, int, int, int);
static void apq8064_term(struct uart_bas *bas);
static void apq8064_putc(struct uart_bas *bas, int);
static int apq8064_rxready(struct uart_bas *bas);
static int apq8064_getc(struct uart_bas *bas, struct mtx *mtx);

extern SLIST_HEAD(uart_devinfo_list, uart_devinfo) uart_sysdevs;

static int
apq8064_uart_param(struct uart_bas *bas, int baudrate, int databits,
    int stopbits, int parity)
{
	int ulcon;

	ulcon = 0;

	switch (databits) {
	case 5:
		ulcon |= (UART_DM_5_BPS << 4);
		break;
	case 6:
		ulcon |= (UART_DM_6_BPS << 4);
		break;
	case 7:
		ulcon |= (UART_DM_7_BPS << 4);
		break;
	case 8:
		ulcon |= (UART_DM_8_BPS << 4);
		break;
	default:
		return (EINVAL);
	}

	switch (parity) {
	case UART_PARITY_NONE:
		ulcon |= UART_DM_NO_PARITY;
		break;
	case UART_PARITY_ODD:
		ulcon |= UART_DM_ODD_PARITY;
		break;
	case UART_PARITY_EVEN:
		ulcon |= UART_DM_EVEN_PARITY;
		break;
	case UART_PARITY_SPACE:
		ulcon |= UART_DM_SPACE_PARITY;
		break;
	case UART_PARITY_MARK:
	default:
		return (EINVAL);
	}

	switch (stopbits) {
	case 1:
		ulcon |= (UART_DM_SBL_1 << 2);
		break;
	case 2:
		ulcon |= (UART_DM_SBL_2 << 2);
		break;
	default:
		return (EINVAL);
	}
#if 0
	uart_setreg(bas, UART_DM_MR2, ulcon);
#endif
	/* For now set 8-N-1 configuration: 8 data bits - No parity - 1 stop bit */
	uart_setreg(bas, UART_DM_MR2, UART_DM_8_N_1_MODE);

	/* Set 115200 for both TX and RX. */;
	uart_setreg(bas, UART_DM_CSR, 0xff);
	uart_barrier(bas);

	return (0);
}

struct uart_ops uart_apq8064_ops = {
	.probe = apq8064_probe,
	.init = apq8064_init,
	.term = apq8064_term,
	.putc = apq8064_putc,
	.rxready = apq8064_rxready,
	.getc = apq8064_getc,
};

static int
apq8064_probe(struct uart_bas *bas)
{

	return (0);
}

static void
apq8064_init(struct uart_bas *bas, int baudrate, int databits, int stopbits,
    int parity)
{

	if (bas->rclk == 0)
		bas->rclk = DEF_CLK;

	KASSERT(bas->rclk != 0, ("apq8064_init: Invalid rclk"));

	/* Set default parameters */
	apq8064_uart_param(bas, baudrate, databits, stopbits, parity);

	/* Configure UART mode registers MR1 and MR2 */
	/* Hardware flow control isn't supported */
	uart_setreg(bas, UART_DM_MR1, 0x0);

	/* Reset interrupt mask register. */
	uart_setreg(bas, UART_DM_IMR, 0);

	/* Configure Tx and Rx watermarks configuration registers */
	/*
	 * TX watermark value is set to 0 - interrupt is generated when
	 * FIFO level is less than or equal to 0 
	 */
	uart_setreg(bas, UART_DM_TFWR, UART_DM_TFW_VALUE);

	/* Set RX watermark value */
	uart_setreg(bas, UART_DM_RFWR, UART_DM_RFW_VALUE);

	/* Configure Interrupt Programming Register */
	/* Set initial Stale timeout value */
	uart_setreg(bas, UART_DM_IPR, UART_DM_STALE_TIMEOUT_LSB);

	/* Disable IRDA mode */
	uart_setreg(bas, UART_DM_IRDA, 0x0);

	/* Configure and enable sim interface if required */
	/* Configure hunt character value in HCR register */
	/* Keep it in reset state */
	uart_setreg(bas, UART_DM_HCR, 0x0);

	/* Issue soft reset command */
	SETREG(bas, UART_DM_CR, RESET_TX);
	SETREG(bas, UART_DM_CR, RESET_RX);
	SETREG(bas, UART_DM_CR, RESET_ERROR_STATUS);
	SETREG(bas, UART_DM_CR, RESET_BREAK_INT);
	SETREG(bas, UART_DM_CR, RESET_STALE_INT);

	/* Enable/Disable Rx/Tx DM interfaces */
	/* Disable Data Mover for now. */
	uart_setreg(bas, UART_DM_DMEN, 0x0);

	/* Enable transmitter and receiver */
	uart_setreg(bas, UART_DM_CR, UART_DM_CR_RX_ENABLE);
	uart_setreg(bas, UART_DM_CR, UART_DM_CR_TX_ENABLE);

	uart_barrier(bas);
}

static void
apq8064_term(struct uart_bas *bas)
{
	/* XXX */
}

static void
apq8064_putc(struct uart_bas *bas, int c)
{
	int limit;

	/* 
	 * Write to NO_CHARS_FOR_TX register the number of characters
	 * to be transmitted. However, before writing TX_FIFO must
	 * be empty as indicated by TX_READY interrupt in IMR register
	 */

        /* 
	 * Check if transmit FIFO is empty.
         * If not wait for TX_READY interrupt.
	 */
	limit = 1000;
        if (!(uart_getreg(bas, UART_DM_SR) & UART_DM_SR_TXEMT)) {
                while ((uart_getreg(bas, UART_DM_ISR) & UART_DM_TX_READY) == 0 && --limit)
                        DELAY(4);
        }
        /* FIFO is ready, write number of characters to be written */
        uart_setreg(bas, UART_DM_NO_CHARS_FOR_TX, 1);

        /* Wait till TX FIFO has space */
        while ((uart_getreg(bas, UART_DM_SR) & UART_DM_SR_TXRDY) == 0)
                DELAY(4);

        /* TX FIFO has space. Write char */
        SETREG(bas, UART_DM_TF(0), (c & 0xff));
}

static int
apq8064_rxready(struct uart_bas *bas)
{

	/* Wait for a character to come ready */
	return ((uart_getreg(bas, UART_DM_SR) & UART_DM_SR_RXRDY) ==
	    UART_DM_SR_RXRDY);
}

static int
apq8064_getc(struct uart_bas *bas, struct mtx *mtx)
{
	int c;

	/* Wait for a character to come ready */
	while ((uart_getreg(bas, UART_DM_SR) & UART_DM_SR_RXRDY) != UART_DM_SR_RXRDY)
		DELAY(4);

        /* Check for Overrun error. If so reset Error Status */
        if (uart_getreg(bas, UART_DM_SR) & UART_DM_SR_UART_OVERRUN)
                uart_setreg(bas, UART_DM_CR, UART_DM_CMD_RESET_ERR_STAT);

	/* Read char */
	c = uart_getreg(bas, UART_DM_RF(0));

	return (c);
}

/*
 * High-level UART interface.
 */
struct apq8064_uart_softc {
        struct uart_softc base;
	uint32_t ier;
};

static int apq8064_bus_probe(struct uart_softc *sc);
static int apq8064_bus_attach(struct uart_softc *sc);
static int apq8064_bus_flush(struct uart_softc *, int);
static int apq8064_bus_getsig(struct uart_softc *);
static int apq8064_bus_ioctl(struct uart_softc *, int, intptr_t);
static int apq8064_bus_ipend(struct uart_softc *);
static int apq8064_bus_param(struct uart_softc *, int, int, int, int);
static int apq8064_bus_receive(struct uart_softc *);
static int apq8064_bus_setsig(struct uart_softc *, int);
static int apq8064_bus_transmit(struct uart_softc *);

static kobj_method_t apq8064_methods[] = {
	KOBJMETHOD(uart_probe,		apq8064_bus_probe),
	KOBJMETHOD(uart_attach, 	apq8064_bus_attach),
	KOBJMETHOD(uart_flush,		apq8064_bus_flush),
	KOBJMETHOD(uart_getsig,		apq8064_bus_getsig),
	KOBJMETHOD(uart_ioctl,		apq8064_bus_ioctl),
	KOBJMETHOD(uart_ipend,		apq8064_bus_ipend),
	KOBJMETHOD(uart_param,		apq8064_bus_param),
	KOBJMETHOD(uart_receive,	apq8064_bus_receive),
	KOBJMETHOD(uart_setsig,		apq8064_bus_setsig),
	KOBJMETHOD(uart_transmit,	apq8064_bus_transmit),
	{0, 0 }
};

int
apq8064_bus_probe(struct uart_softc *sc)
{

	sc->sc_txfifosz = 64;
	sc->sc_rxfifosz = 64;

        device_set_desc(sc->sc_dev, "Qualcomm HSUART");

	return (0);
}

static int
apq8064_bus_attach(struct uart_softc *sc)
{
	struct apq8064_uart_softc *u = (struct apq8064_uart_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;

	sc->sc_hwiflow = 0;
	sc->sc_hwoflow = 0;

	/* Set TX_READY, TXLEV, RXLEV, RXSTALE */
	u->ier = UART_DM_IMR_ENABLED;

	/* Configure Interrupt Mask register IMR */
	uart_setreg(bas, UART_DM_IMR, u->ier);

	return (0);
}

/* 
 * Write the current transmit buffer to the TX FIFO. 
 */
static int
apq8064_bus_transmit(struct uart_softc *sc)
{
	struct apq8064_uart_softc *u = (struct apq8064_uart_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;
	int i;

	uart_lock(sc->sc_hwmtx);

	/* Write some data */
	for (i = 0; i < sc->sc_txdatasz; i++) {
		/* Write TX data */
		apq8064_putc(bas, sc->sc_txbuf[i]);
		uart_barrier(bas);
	}

	/* TX FIFO is empty now, enable TX_READY interrupt */
	u->ier |= UART_DM_TX_READY;
	SETREG(bas, UART_DM_IMR, u->ier);
	uart_barrier(bas);

	/* 
	 * Inform upper layer that it us transmitting data to hardware,
	 * this will be cleared when TXIDLE interrupt occurs.
	 */
	sc->sc_txbusy = 1;
	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
apq8064_bus_setsig(struct uart_softc *sc, int sig)
{

	return (0);
}

static int
apq8064_bus_receive(struct uart_softc *sc)
{
	struct apq8064_uart_softc *u = (struct apq8064_uart_softc *)sc;
	struct uart_bas *bas;
	int c;

	bas = &sc->sc_bas;
	uart_lock(sc->sc_hwmtx);

	/* Initialize Receive Path and interrupt */
	SETREG(bas, UART_DM_CR, RESET_STALE_INT);
	SETREG(bas, UART_DM_DMRX, 512);
	SETREG(bas, UART_DM_CR, STALE_EVENT_ENABLE);
	u->ier |= UART_DM_RXLEV;
	SETREG(bas, UART_DM_IMR, u->ier);

	/* Loop over until we are full, or no data is available */
	while (uart_getreg(bas, UART_DM_SR) & UART_DM_SR_RXRDY) {	
               	if (uart_rx_full(sc)) {
	                /* No space left in input buffer */
                        sc->sc_rxbuf[sc->sc_rxput] = UART_STAT_OVERRUN;
                        break;
                }

		/* Read RX FIFO */
		c = uart_getreg(bas, UART_DM_RF(0));
		uart_barrier(bas);

                uart_rx_put(sc, c);
	}

	uart_unlock(sc->sc_hwmtx);

	return (0);
}

static int
apq8064_bus_param(struct uart_softc *sc, int baudrate, int databits,
    int stopbits, int parity)
{
	int error;

	if (sc->sc_bas.rclk == 0)
		sc->sc_bas.rclk = DEF_CLK;

	KASSERT(sc->sc_bas.rclk != 0, ("apq8064_init: Invalid rclk"));

	uart_lock(sc->sc_hwmtx);
	error = apq8064_uart_param(&sc->sc_bas, baudrate, databits, stopbits,
	    parity);
	uart_unlock(sc->sc_hwmtx);

	return (error);
}

static int
apq8064_bus_ipend(struct uart_softc *sc)
{
	struct apq8064_uart_softc *u = (struct apq8064_uart_softc *)sc;
	struct uart_bas *bas = &sc->sc_bas;
	uint32_t isr;
	int ipend;

	uart_lock(sc->sc_hwmtx);

	/* Get ISR status */
	isr = GETREG(bas, UART_DM_MISR);

	ipend = 0;

	/* Uart RX starting, notify upper layer */
	if (isr & UART_DM_RXLEV) {
		u->ier &= ~UART_DM_RXLEV;
		SETREG(bas, UART_DM_IMR, u->ier);
		uart_barrier(bas);
		ipend |= SER_INT_RXREADY;
	}

	/* Stale RX interrupt */
	if (isr & UART_DM_RXSTALE) {
		/* Disable and reset it */
		SETREG(bas, UART_DM_CR, STALE_EVENT_DISABLE);
		SETREG(bas, UART_DM_CR, RESET_STALE_INT);
		uart_barrier(bas);
		ipend |= SER_INT_RXREADY;
	}

	/* TX READY interrupt */
	if (isr & UART_DM_TX_READY) {
		/* Clear  TX Ready */
		SETREG(bas, UART_DM_CR, CLEAR_TX_READY);

		/* Disable TX_READY */
		u->ier &= ~UART_DM_TX_READY;
		SETREG(bas, UART_DM_IMR, u->ier);
		uart_barrier(bas);

		if (sc->sc_txbusy != 0)
			ipend |= SER_INT_TXIDLE;
	}

	if (isr & UART_DM_TXLEV) {
		/* TX FIFO is empty */
		u->ier &= ~UART_DM_TXLEV;
		SETREG(bas, UART_DM_IMR, u->ier);
		uart_barrier(bas);

		if (sc->sc_txbusy != 0)
			ipend |= SER_INT_TXIDLE;
	}

	uart_unlock(sc->sc_hwmtx);
	return (ipend);
}

static int
apq8064_bus_flush(struct uart_softc *sc, int what)
{

	return (0);
}

static int
apq8064_bus_getsig(struct uart_softc *sc)
{

	return (0);
}

static int
apq8064_bus_ioctl(struct uart_softc *sc, int request, intptr_t data)
{

	return (EINVAL);
}

struct uart_class uart_apq8064_class = {
	"apq8064",
	apq8064_methods,
	sizeof(struct apq8064_uart_softc),
	.uc_ops = &uart_apq8064_ops,
	.uc_range = 8,
	.uc_rclk = DEF_CLK,
};
