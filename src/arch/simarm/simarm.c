/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:     src/arch/simarm/simarm.c                                   *
 * Created:       2004-11-04 by Hampa Hug <hampa@hampa.ch>                   *
 * Last modified: 2004-12-13 by Hampa Hug <hampa@hampa.ch>                   *
 * Copyright:     (C) 2004 Hampa Hug <hampa@hampa.ch>                        *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/

/* $Id$ */


#include "main.h"


void sarm_break (simarm_t *sim, unsigned char val);


static
void sarm_setup_ram (simarm_t *sim, ini_sct_t *ini)
{
  ini_sct_t     *sct;
  mem_blk_t     *ram;
  const char    *fname;
  unsigned long base, size;

  sct = ini_sct_find_sct (ini, "ram");

  while (sct != NULL) {
    fname = ini_get_str (sct, "file");
    base = ini_get_lng_def (sct, "base", 0);
    size = ini_get_lng_def (sct, "size", 16L * 1024L * 1024L);

    pce_log (MSG_INF, "RAM:\tbase=0x%08lx size=%lu file=%s\n",
      base, size, (fname == NULL) ? "<none>" : fname
    );

    ram = mem_blk_new (base, size, 1);
    mem_blk_clear (ram, 0x00);
    mem_blk_set_readonly (ram, 0);
    mem_add_blk (sim->mem, ram, 1);

    if (base == 0) {
      sim->ram = ram;
    }

    if (fname != NULL) {
      if (pce_load_blk_bin (ram, fname)) {
        pce_log (MSG_ERR, "*** loading ram failed (%s)\n", fname);
      }
    }

    sct = ini_sct_find_next (sct, "ram");
  }
}

static
void sarm_setup_rom (simarm_t *sim, ini_sct_t *ini)
{
  ini_sct_t     *sct;
  mem_blk_t     *rom;
  const char    *fname;
  unsigned long base, size;

  sct = ini_sct_find_sct (ini, "rom");

  while (sct != NULL) {
    fname = ini_get_str (sct, "file");
    base = ini_get_lng_def (sct, "base", 0);
    size = ini_get_lng_def (sct, "size", 65536L);

    pce_log (MSG_INF, "ROM:\tbase=0x%08lx size=%lu file=%s\n",
      base, size, (fname != NULL) ? fname : "<none>"
    );

    rom = mem_blk_new (base, size, 1);
    mem_blk_clear (rom, 0x00);
    mem_blk_set_readonly (rom, 1);
    mem_add_blk (sim->mem, rom, 1);

    if (fname != NULL) {
      if (pce_load_blk_bin (rom, fname)) {
        pce_log (MSG_ERR, "*** loading rom failed (%s)\n", fname);
      }
    }

    sct = ini_sct_find_next (sct, "rom");
  }
}

static
void sarm_setup_cpu (simarm_t *sim, ini_sct_t *ini)
{
  ini_sct_t  *sct;
  const char *model;

  sct = ini_sct_find_sct (ini, "cpu");

  model = ini_get_str_def (sct, "model", "armv5");

  pce_log (MSG_INF, "CPU:\tmodel=%s\n", model);

  sim->cpu = arm_new ();
  if (sim->cpu == NULL) {
    return;
  }

  arm_set_mem_fct (sim->cpu, sim->mem,
    &mem_get_uint8,
    &mem_get_uint16_le,
    &mem_get_uint32_le,
    &mem_set_uint8,
    &mem_set_uint16_le,
    &mem_set_uint32_le
  );

  if (sim->ram != NULL) {
    arm_set_ram (sim->cpu, mem_blk_get_data (sim->ram), mem_blk_get_size (sim->ram));
  }
}

static
void sarm_setup_intc (simarm_t *sim, ini_sct_t *ini)
{
  unsigned long base;
  ini_sct_t     *sct;

  sct = ini_sct_find_sct (ini, "intc");
  if (sct != NULL) {
    base = ini_get_lng_def (sct, "base", 0xd6000000UL);
  }
  else {
    base = 0xd6000000UL;
  }

  sim->intc = ict_new (base);
  if (sim->intc == NULL) {
    return;
  }

  ict_set_fiq_f (sim->intc, arm_set_fiq, sim->cpu);
  ict_set_irq_f (sim->intc, arm_set_irq, sim->cpu);

  mem_add_blk (sim->mem, ict_get_io (sim->intc, 0), 0);

  pce_log (MSG_INF, "INTC:\tbase=0x%08lx\n", base);
}

static
void sarm_setup_timer (simarm_t *sim, ini_sct_t *ini)
{
  unsigned long base;
  ini_sct_t     *sct;

  sct = ini_sct_find_sct (ini, "timer");
  if (sct != NULL) {
    base = ini_get_lng_def (sct, "base", 0xc0020000UL);
  }
  else {
    base = 0xc0020000UL;
  }

  sim->timer = tmr_new (base);
  if (sim->timer == NULL) {
    return;
  }

  tmr_set_irq_f (sim->timer, 0, ict_set_irq4, sim->intc);
  tmr_set_irq_f (sim->timer, 1, ict_set_irq5, sim->intc);
  tmr_set_irq_f (sim->timer, 2, ict_set_irq6, sim->intc);
  tmr_set_irq_f (sim->timer, 3, ict_set_irq7, sim->intc);

  mem_add_blk (sim->mem, tmr_get_io (sim->timer, 0), 0);

  pce_log (MSG_INF, "TIMER:\tbase=0x%08lx\n", base);
}

static
void sarm_setup_serport (simarm_t *sim, ini_sct_t *ini)
{
  unsigned      i;
  unsigned long base;
  unsigned      irq;
  const char    *fname;
  const char    *chip;
  ini_sct_t     *sct;

  sim->serport[0] = NULL;
  sim->serport[1] = NULL;

  sct = ini_sct_find_sct (ini, "serial");
  if (sct == NULL) {
    return;
  }

  i = 0;
  while ((i < 2) && (sct != NULL)) {
    base = ini_get_lng_def (sct, "io", 0xc0030000UL);
    irq = ini_get_lng_def (sct, "irq", 2);
    chip = ini_get_str_def (sct, "uart", "8250");
    fname = ini_get_str (sct, "file");

    pce_log (MSG_INF, "UART%u:\tio=0x%08lx irq=%u uart=%s file=%s\n",
      i, base, irq, chip, (fname == NULL) ? "<none>" : fname
    );

    sim->serport[i] = ser_new (base, 2);
    if (sim->serport[i] == NULL) {
      pce_log (MSG_ERR, "*** serial port setup failed [%08lX/%u -> %s]\n",
        base, irq, (fname == NULL) ? "<none>" : fname
      );
    }
    else {
      if (fname != NULL) {
        if (ser_set_fname (sim->serport[i], fname)) {
          pce_log (MSG_ERR, "*** can't open file (%s)\n", fname);
        }
      }
      else {
        ser_set_fp (sim->serport[i], stdout, 0);
      }

      if (e8250_set_chip_str (&sim->serport[i]->uart, chip)) {
        pce_log (MSG_ERR, "*** unknown UART chip (%s)\n", chip);
      }

      e8250_set_irq_f (&sim->serport[i]->uart,
        ict_get_irq_f (sim->intc, irq), sim->intc
      );

      mem_add_blk (sim->mem, ser_get_reg (sim->serport[i]), 0);

      i += 1;
    }

    sct = ini_sct_find_next (sct, "serial");
  }
}

static
void sarm_setup_pci (simarm_t *sim, ini_sct_t *ini)
{
  sim->pci = pci_ixp_new();

  mem_add_blk (sim->mem, pci_ixp_get_mem_io (sim->pci), 0);
  mem_add_blk (sim->mem, pci_ixp_get_mem_cfg (sim->pci), 0);
  mem_add_blk (sim->mem, pci_ixp_get_mem_special (sim->pci), 0);
  mem_add_blk (sim->mem, pci_ixp_get_mem_pcicfg (sim->pci), 0);
  mem_add_blk (sim->mem, pci_ixp_get_mem_csr (sim->pci), 0);
  mem_add_blk (sim->mem, pci_ixp_get_mem_mem (sim->pci), 0);

  pci_set_irq_f (&sim->pci->bus, NULL, NULL);

  pce_log (MSG_INF, "PCI:\tinitialized\n");
}

static
void sarm_setup_ata (simarm_t *sim, ini_sct_t *ini)
{
  pci_ata_init (&sim->pciata);

  pci_dev_set_irq_f (&sim->pciata.pci, 0, pci_set_intr_a, &sim->pci->bus);

  pci_set_device (&sim->pci->bus, &sim->pciata.pci, 4);

  pce_log (MSG_INF, "PCI0:\tATA\n");
}

static
void sarm_load_mem (simarm_t *sim, ini_sct_t *ini)
{
  ini_sct_t  *sct;
  const char *fmt;
  const char *fname;

  sct = ini_sct_find_sct (ini, "load");

  while (sct != NULL) {
    fmt = ini_get_str_def (sct, "format", "binary");
    fname = ini_get_str (sct, "file");

    pce_log (MSG_INF, "Load:\tformat=%s file=%s\n",
      fmt, (fname != NULL) ? fname : "<none>"
    );

    if (strcmp (fmt, "ihex") == 0) {
      if (fname != NULL) {
        if (pce_load_mem_hex (sim->mem, fname)) {
          pce_log (MSG_ERR, "*** loading ihex failed (%s)\n", fname);
        }
      }
    }
    else if (strcmp (fmt, "binary") == 0) {
      unsigned long base;

      base = ini_get_lng_def (sct, "base", 0);

      if (fname != NULL) {
        if (pce_load_mem_bin (sim->mem, fname, base)) {
          pce_log (MSG_ERR, "*** loading binary failed (%s)\n", fname);
        }
      }
    }
    else {
      pce_log (MSG_ERR, "*** unknown format (%s)\n", fmt);
    }

    sct = ini_sct_find_next (sct, "load");
  }
}

simarm_t *sarm_new (ini_sct_t *ini)
{
  unsigned i;
  simarm_t *sim;

  sim = (simarm_t *) malloc (sizeof (simarm_t));
  if (sim == NULL) {
    return (NULL);
  }

  sim->ram = NULL;
  sim->brk = 0;
  sim->clk_cnt = 0;

  for (i = 0; i < 4; i++) {
    sim->clk_div[i] = 0;
  }

  sim->brkpt = NULL;

  sim->mem = mem_new();

  sarm_setup_ram (sim, ini);
  sarm_setup_rom (sim, ini);
  sarm_setup_cpu (sim, ini);
  sarm_setup_intc (sim, ini);
  sarm_setup_timer (sim, ini);
  sarm_setup_serport (sim, ini);
  sarm_setup_pci (sim, ini);
  sarm_setup_ata (sim, ini);

  sarm_load_mem (sim, ini);

  return (sim);
}

void sarm_del (simarm_t *sim)
{
  if (sim == NULL) {
    return;
  }

  pci_ata_free (&sim->pciata);
  pci_ixp_del (sim->pci);

  ser_del (sim->serport[1]);
  ser_del (sim->serport[0]);

  tmr_del (sim->timer);
  ict_del (sim->intc);

  arm_del (sim->cpu);

  mem_del (sim->mem);

  free (sim);
}

unsigned long long sarm_get_clkcnt (simarm_t *sim)
{
  return (sim->clk_cnt);
}

void sarm_break (simarm_t *sim, unsigned char val)
{
  if ((val == PCE_BRK_STOP) || (val == PCE_BRK_ABORT)) {
    sim->brk = val;
  }
}

void sarm_set_keycode (simarm_t *sim, unsigned char val)
{
  if (sim->serport[0] != NULL) {
    ser_receive (sim->serport[0], val);
  }
}

void sarm_reset (simarm_t *sim)
{
  arm_reset (sim->cpu);
}

void sarm_clock (simarm_t *sim, unsigned n)
{
  if (sim->clk_div[0] >= 1024) {
    scon_check (sim);

    sim->clk_div[0] &= 1023;
  }

  tmr_clock (sim->timer);
  arm_clock (sim->cpu, n);

  sim->clk_cnt += n;
  sim->clk_div[0] += n;
/*  sim->clk_div[1] += n; */
/*  sim->clk_div[2] += n; */
/*  sim->clk_div[3] += n; */
}

void sarm_set_msg (simarm_t *sim, const char *msg, const char *val)
{
  if (sim == NULL) {
    sim = par_sim;
  }

  if (strcmp (msg, "break") == 0) {
    if (strcmp (val, "stop") == 0) {
      sim->brk = PCE_BRK_STOP;
      return;
    }
    else if (strcmp (val, "abort") == 0) {
      sim->brk = PCE_BRK_ABORT;
      return;
    }
  }

  pce_log (MSG_DEB, "msg (\"%s\", \"%s\")\n", msg, val);

  pce_log (MSG_INF, "unhandled message (\"%s\", \"%s\")\n", msg, val);
}
