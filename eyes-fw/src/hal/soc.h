/*
 * ESP32-S3 registers, only the ones this firmware touches.
 *
 * Addresses and bit positions are copied from ESP-IDF v5.3.2
 * (the *_reg.h files under components/soc/esp32s3/include/soc), not typed from memory.
 */
#ifndef SOC_H
#define SOC_H

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))
#define BIT(n) (1u << (n))

/* ---- SYSTEM (clocks, peripheral gates) -------------------------------- */
#define SYSTEM_CPU_PER_CONF_REG   0x600C0010u
#define   SYSTEM_CPUPERIOD_SEL_M    (0x3u << 0) /* 0: 80 MHz, 1: 160, 2: 240 (from PLL) */
#define   SYSTEM_PLL_FREQ_SEL       BIT(2)      /* 1: PLL at 480 MHz */
#define SYSTEM_PERIP_CLK_EN0_REG  0x600C0018u
#define SYSTEM_PERIP_RST_EN0_REG  0x600C0020u
#define   SYSTEM_SPI2_BIT           BIT(6)
#define SYSTEM_SYSCLK_CONF_REG    0x600C0060u
#define   SYSTEM_PRE_DIV_CNT_M      (0x3FFu << 0)
#define   SYSTEM_SOC_CLK_SEL_S      10          /* 0: XTAL, 1: PLL, 2: RC_FAST */
#define   SYSTEM_SOC_CLK_SEL_M      (0x3u << 10)

/* ---- RTC_CNTL (watchdogs, PLL power) ---------------------------------- */
#define RTC_CNTL_OPTIONS0_REG     0x60008000u
#define   RTC_CNTL_BB_I2C_FORCE_PD     BIT(6)
#define   RTC_CNTL_BBPLL_I2C_FORCE_PD  BIT(8)
#define   RTC_CNTL_BBPLL_FORCE_PD      BIT(10)
#define RTC_CNTL_WDTCONFIG0_REG   0x60008098u
#define   RTC_CNTL_WDT_FLASHBOOT_MOD_EN BIT(12)
#define   RTC_CNTL_WDT_EN               BIT(31)
#define RTC_CNTL_WDTWPROTECT_REG  0x600080B0u
#define   RTC_CNTL_WDT_WKEY_VALUE   0x50D83AA1u
#define RTC_CNTL_SWD_CONF_REG     0x600080B4u
#define   RTC_CNTL_SWD_AUTO_FEED_EN BIT(31)
#define RTC_CNTL_SWD_WPROTECT_REG 0x600080B8u
#define   RTC_CNTL_SWD_WKEY_VALUE   0x8F1D312Au

/* ---- timer groups (their watchdogs only) ------------------------------ */
#define TIMG0_BASE                0x6001F000u
#define TIMG1_BASE                0x60020000u
#define TIMG_WDTCONFIG0_REG(b)    ((b) + 0x48u)
#define TIMG_WDTWPROTECT_REG(b)   ((b) + 0x64u)
#define   TIMG_WDT_WKEY_VALUE       0x50D83AA1u

/* ---- analog master: PLL calibration ----------------------------------- */
#define I2C_MST_ANA_CONF0_REG     0x6000E040u
#define   I2C_MST_BBPLL_STOP_FORCE_HIGH BIT(2)
#define   I2C_MST_BBPLL_STOP_FORCE_LOW  BIT(3)
#define   I2C_MST_BBPLL_CAL_DONE        BIT(24)
#define ANA_CONFIG_REG            0x6000E044u
#define   ANA_CONFIG_ALL_OFF        (0x3FFu << 8) /* every analog block cut off the internal bus */
#define   ANA_CONFIG_I2C_BBPLL      BIT(17)       /* 0: the PLL block is reachable */
#define I2C_BBPLL                 0x66
#define I2C_BBPLL_HOSTID          1

/* ---- GPIO --------------------------------------------------------------- */
#define GPIO_OUT_W1TS_REG         0x60004008u /* pins 0..31 */
#define GPIO_OUT_W1TC_REG         0x6000400Cu
#define GPIO_OUT1_W1TS_REG        0x60004014u /* pins 32..48 */
#define GPIO_OUT1_W1TC_REG        0x60004018u
#define GPIO_ENABLE_W1TS_REG      0x60004024u
#define GPIO_ENABLE_W1TC_REG      0x60004028u
#define GPIO_ENABLE1_W1TS_REG     0x60004030u
#define GPIO_ENABLE1_W1TC_REG     0x60004034u
#define GPIO_IN_REG               0x6000403Cu
#define GPIO_IN1_REG              0x60004040u
#define GPIO_PIN_REG(n)           (0x60004074u + 4u * (n))
#define   GPIO_PIN_PAD_DRIVER       BIT(2)     /* 1: open drain */
#define GPIO_FUNC_OUT_SEL_CFG_REG(n) (0x60004554u + 4u * (n))
#define   GPIO_FUNC_OEN_SEL         BIT(10)    /* 1: output enable comes from GPIO_ENABLE */
#define SIG_GPIO_OUT_IDX          256          /* plain GPIO output */
#define FSPICLK_OUT_IDX           101
#define FSPID_OUT_IDX             103

#define IO_MUX_REG(n)             (0x60009004u + 4u * (n))
#define   IO_MUX_FUN_PD             BIT(7)
#define   IO_MUX_FUN_PU             BIT(8)
#define   IO_MUX_FUN_IE             BIT(9)
#define   IO_MUX_FUN_DRV_S          10
#define   IO_MUX_MCU_SEL_S          12
#define   IO_MUX_FUNC_GPIO          1

/* ---- SPI2 (general purpose SPI, a.k.a. FSPI) -------------------------- */
#define SPI2_BASE                 0x60024000u
#define SPI_CMD_REG               (SPI2_BASE + 0x00u)
#define   SPI_UPDATE                BIT(23)
#define   SPI_USR                   BIT(24)
#define SPI_CTRL_REG              (SPI2_BASE + 0x08u)
#define SPI_CLOCK_REG             (SPI2_BASE + 0x0Cu)
#define   SPI_CLKCNT_L_S            0
#define   SPI_CLKCNT_H_S            6
#define   SPI_CLKCNT_N_S            12
#define   SPI_CLK_EQU_SYSCLK        BIT(31)
#define SPI_USER_REG              (SPI2_BASE + 0x10u)
#define   SPI_USR_MOSI              BIT(27)
#define SPI_USER1_REG             (SPI2_BASE + 0x14u)
#define SPI_USER2_REG             (SPI2_BASE + 0x18u)
#define SPI_MS_DLEN_REG           (SPI2_BASE + 0x1Cu)
#define SPI_MISC_REG              (SPI2_BASE + 0x20u)
#define   SPI_CS_ALL_DIS            0x3Fu      /* CS0..CS5 not driven by the peripheral */
#define SPI_DIN_MODE_REG          (SPI2_BASE + 0x24u)
#define SPI_DIN_NUM_REG           (SPI2_BASE + 0x28u)
#define SPI_DOUT_MODE_REG         (SPI2_BASE + 0x2Cu)
#define SPI_DMA_CONF_REG          (SPI2_BASE + 0x30u)
#define   SPI_RX_AFIFO_RST          BIT(29)
#define   SPI_BUF_AFIFO_RST         BIT(30)
#define SPI_W_REG(i)              (SPI2_BASE + 0x98u + 4u * (i)) /* 16 words = 64 bytes */
#define SPI_SLAVE_REG             (SPI2_BASE + 0xE0u)
#define SPI_CLK_GATE_REG          (SPI2_BASE + 0xE8u)
#define   SPI_CLK_EN                BIT(0)
#define   SPI_MST_CLK_ACTIVE        BIT(1)
#define   SPI_MST_CLK_SEL           BIT(2)     /* 1: 80 MHz from the PLL, 0: XTAL */

/* ---- USB Serial/JTAG (the console) ------------------------------------ */
#define USB_SERIAL_JTAG_EP1_REG       0x60038000u
#define USB_SERIAL_JTAG_EP1_CONF_REG  0x60038004u
#define   USB_SERIAL_JTAG_WR_DONE                  BIT(0)
#define   USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE   BIT(1)
#define   USB_SERIAL_JTAG_SERIAL_OUT_EP_DATA_AVAIL BIT(2)

/* ---- hardware random number ------------------------------------------- */
#define WDEV_RND_REG              0x6003507Cu

/* ---- ROM routines (addresses provided by the linker script) ------------ */
void ets_update_cpu_frequency(uint32_t ticks_per_us);
void rom_i2c_writeReg(uint8_t block, uint8_t host_id, uint8_t reg_add, uint8_t data);
void rom_i2c_writeReg_Mask(uint8_t block, uint8_t host_id, uint8_t reg_add, uint8_t msb, uint8_t lsb, uint8_t data);

#endif
