#ifndef __SC7A20_REG_H__
#define __SC7A20_REG_H__

/* SPI command bits */
#define SC7A20_SPI_READ             0x80U
#define SC7A20_SPI_AUTO_INCREMENT   0x40U

/* Registers used by this driver */
#define SC7A20_REG_WHO_AM_I         0x0FU
#define SC7A20_REG_CTRL1            0x20U
#define SC7A20_REG_CTRL2            0x21U
#define SC7A20_REG_CTRL3            0x22U
#define SC7A20_REG_CTRL4            0x23U
#define SC7A20_REG_CTRL5            0x24U
#define SC7A20_REG_CTRL6            0x25U
#define SC7A20_REG_REFERENCE        0x26U
#define SC7A20_REG_OUT_X_L          0x28U
#define SC7A20_REG_INT1_CFG         0x30U
#define SC7A20_REG_INT1_SOURCE      0x31U
#define SC7A20_REG_INT1_THS         0x32U
#define SC7A20_REG_INT1_DURATION    0x33U
#define SC7A20_REG_INT2_CFG         0x34U

/* 25Hz low-power, +/-2g, INT1 high-pass motion event on PA9 */
#define SC7A20_WHO_AM_I             0x11U
#define SC7A20_CTRL1_VALUE          0x3FU
#define SC7A20_CTRL2_VALUE          0x01U
#define SC7A20_CTRL3_VALUE          0x40U
#define SC7A20_INT1_CFG_VALUE       0x2AU

#endif
