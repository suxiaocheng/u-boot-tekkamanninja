/*
 * (C) Copyright 2003
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * (C) Copyright 2004
 * Mark Jonas, Freescale Semiconductor, mark.jonas@motorola.com.
 *
 * (C) Copyright 2010
 * Sergei Poselenov, Emcraft Systems, sposelenov@emcraft.com.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <mpc5xxx.h>
#include <pci.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <libfdt.h>
#include <netdev.h>

#include "mt46v32m16.h"

#ifndef CONFIG_SYS_RAMBOOT
static void sdram_start (int hi_addr)
{
	long hi_addr_bit = hi_addr ? 0x01000000 : 0;
	long control = SDRAM_CONTROL | hi_addr_bit;

	/* unlock mode register */
	out_be32((void *)MPC5XXX_SDRAM_CTRL, control | 0x80000000);
	__asm__ volatile ("sync");

	/* precharge all banks */
	out_be32((void *)MPC5XXX_SDRAM_CTRL, control | 0x80000002);
	__asm__ volatile ("sync");

#if SDRAM_DDR
	/* set mode register: extended mode */
	out_be32((void *)MPC5XXX_SDRAM_MODE, SDRAM_EMODE);
	__asm__ volatile ("sync");

	/* set mode register: reset DLL */
	out_be32((void *)MPC5XXX_SDRAM_MODE, SDRAM_MODE | 0x04000000);
	__asm__ volatile ("sync");
#endif

	/* precharge all banks */
	out_be32((void *)MPC5XXX_SDRAM_CTRL, control | 0x80000002);
	__asm__ volatile ("sync");

	/* auto refresh */
	out_be32((void *)MPC5XXX_SDRAM_CTRL, control | 0x80000004);
	__asm__ volatile ("sync");

	/* set mode register */
	out_be32((void *)MPC5XXX_SDRAM_MODE, SDRAM_MODE);
	__asm__ volatile ("sync");

	/* normal operation */
	out_be32((void *)MPC5XXX_SDRAM_CTRL, control);
	__asm__ volatile ("sync");
}
#endif

/*
 * ATTENTION: Although partially referenced initdram does NOT make real use
 *            use of CONFIG_SYS_SDRAM_BASE. The code does not work if CONFIG_SYS_SDRAM_BASE
 *            is something else than 0x00000000.
 */

phys_size_t initdram (int board_type)
{
	ulong dramsize = 0;
	uint svr, pvr;

#ifndef CONFIG_SYS_RAMBOOT
	ulong test1, test2;

	/* setup SDRAM chip selects */
	out_be32((void *)MPC5XXX_SDRAM_CS0CFG, 0x0000001e); /* 2GB at 0x0 */
	out_be32((void *)MPC5XXX_SDRAM_CS1CFG, 0x80000000); /* disabled */
	__asm__ volatile ("sync");

	/* setup config registers */
	out_be32((void *)MPC5XXX_SDRAM_CONFIG1, SDRAM_CONFIG1);
	out_be32((void *)MPC5XXX_SDRAM_CONFIG2, SDRAM_CONFIG2);
	__asm__ volatile ("sync");

#if SDRAM_DDR
	/* set tap delay */
	out_be32((void *)MPC5XXX_CDM_PORCFG, SDRAM_TAPDELAY);
	__asm__ volatile ("sync");
#endif

	/* find RAM size using SDRAM CS0 only */
	sdram_start(0);
	test1 = get_ram_size((long *)CONFIG_SYS_SDRAM_BASE, 0x80000000);
	sdram_start(1);
	test2 = get_ram_size((long *)CONFIG_SYS_SDRAM_BASE, 0x80000000);
	if (test1 > test2) {
		sdram_start(0);
		dramsize = test1;
	} else {
		dramsize = test2;
	}

	/* memory smaller than 1MB is impossible */
	if (dramsize < (1 << 20)) {
		dramsize = 0;
	}

	/* set SDRAM CS0 size according to the amount of RAM found */
	if (dramsize > 0) {
		out_be32((void *)MPC5XXX_SDRAM_CS0CFG,
				 0x13 + __builtin_ffs(dramsize >> 20) - 1);
	} else {
		out_be32((void *)MPC5XXX_SDRAM_CS0CFG, 0); /* disabled */
	}

#else /* CONFIG_SYS_RAMBOOT */

	/* retrieve size of memory connected to SDRAM CS0 */
	dramsize = in_be32((void *)MPC5XXX_SDRAM_CS0CFG) & 0xFF;
	if (dramsize >= 0x13) {
		dramsize = (1 << (dramsize - 0x13)) << 20;
	} else {
		dramsize = 0;
	}

#endif /* CONFIG_SYS_RAMBOOT */

	/*
	 * On MPC5200B we need to set the special configuration delay in the
	 * DDR controller. Please refer to Freescale's AN3221 "MPC5200B SDRAM
	 * Initialization and Configuration", 3.3.1 SDelay--MBAR + 0x0190:
	 *
	 * "The SDelay should be written to a value of 0x00000004. It is
	 * required to account for changes caused by normal wafer processing
	 * parameters."
	 */
	svr = get_svr();
	pvr = get_pvr();
	if ((SVR_MJREV(svr) >= 2) &&
	    (PVR_MAJ(pvr) == 1) && (PVR_MIN(pvr) == 4)) {

		out_be32((void *)MPC5XXX_SDRAM_SDELAY, 0x04);
		__asm__ volatile ("sync");
	}

	return dramsize;
}

int checkboard (void)
{
	puts ("Board: A4M072\n");
	return 0;
}

#ifdef	CONFIG_PCI
static struct pci_controller hose;

extern void pci_mpc5xxx_init(struct pci_controller *);

void pci_init_board(void)
{
	pci_mpc5xxx_init(&hose);
}
#endif

#if defined(CONFIG_OF_LIBFDT) && defined(CONFIG_OF_BOARD_SETUP)
void
ft_board_setup(void *blob, bd_t *bd)
{
	ft_cpu_setup(blob, bd);
}
#endif

int board_eth_init(bd_t *bis)
{
	int rv, num_if = 0;

	/* Initialize TSECs first */
	if ((rv = cpu_eth_init(bis)) >= 0)
		num_if += rv;
	else
		printf("ERROR: failed to initialize FEC.\n");

	if ((rv = pci_eth_init(bis)) >= 0)
		num_if += rv;
	else
		printf("ERROR: failed to initialize PCI Ethernet.\n");

	return num_if;
}
/*
 * Miscellaneous late-boot configurations
 *
 * Initialize EEPROM write-protect GPIO pin.
 */
int misc_init_r(void)
{
#if defined(CONFIG_SYS_EEPROM_WREN)
	/* Enable GPIO pin */
	setbits_be32((void *)MPC5XXX_WU_GPIO_ENABLE, CONFIG_SYS_EEPROM_WP);
	/* Set direction, output */
	setbits_be32((void *)MPC5XXX_WU_GPIO_DIR, CONFIG_SYS_EEPROM_WP);
	/* De-assert write enable */
	setbits_be32((void *)MPC5XXX_WU_GPIO_DATA_O, CONFIG_SYS_EEPROM_WP);
#endif
	return 0;
}
#if defined(CONFIG_SYS_EEPROM_WREN)
/* Input: <dev_addr>  I2C address of EEPROM device to enable.
 *         <state>     -1: deliver current state
 *	               0: disable write
 *		       1: enable write
 *  Returns:           -1: wrong device address
 *                      0: dis-/en- able done
 *		     0/1: current state if <state> was -1.
 */
int eeprom_write_enable (unsigned dev_addr, int state)
{
	if (CONFIG_SYS_I2C_EEPROM_ADDR != dev_addr) {
		return -1;
	} else {
		switch (state) {
		case 1:
			/* Enable write access */
			clrbits_be32((void *)MPC5XXX_WU_GPIO_DATA_O, CONFIG_SYS_EEPROM_WP);
			state = 0;
			break;
		case 0:
			/* Disable write access */
			setbits_be32((void *)MPC5XXX_WU_GPIO_DATA_O, CONFIG_SYS_EEPROM_WP);
			state = 0;
			break;
		default:
			/* Read current status back. */
			state = (0 == (in_be32((void *)MPC5XXX_WU_GPIO_DATA_O) &
						   CONFIG_SYS_EEPROM_WP));
			break;
		}
	}
	return state;
}
#endif