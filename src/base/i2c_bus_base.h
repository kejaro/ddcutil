/** @file i2c_bus_base.h
 *
 */

// Copyright (C) 2014-2023 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef I2C_BUS_BASE_H_
#define I2C_BUS_BASE_H_

#include <stdbool.h>
#include <glib-2.0/glib.h>

#include "util/edid.h"

// shared with i2c_bus_selector.c
extern GPtrArray * i2c_buses;

// Retrieve and inspect bus information

// Keep interpret_i2c_bus_flags() in sync with these definitions
#define I2C_BUS_EXISTS               0x80
#define I2C_BUS_ACCESSIBLE           0x40
#define I2C_BUS_ADDR_0X50            0x20      ///< detected I2C bus address 0x50
#define I2C_BUS_ADDR_0X37            0x10      ///< detected I2C bus address 0x37
#define I2C_BUS_ADDR_0X30            0x08      ///< detected write-only addr to specify EDID block number
#define I2C_BUS_EDP                  0x04      ///< bus associated with eDP display
#define I2C_BUS_LVDS                 0x02      ///< bus associated with LVDS display
#define I2C_BUS_PROBED               0x01      ///< has bus been checked?
#define I2C_BUS_VALID_NAME_CHECKED 0x0800
#define I2C_BUS_HAS_VALID_NAME     0x0400
#define I2C_BUS_BUSY               0x0200      ///< for possible future use
#define I2C_BUS_SYSFS_EDID         0x0100
#define I2C_BUS_DRM_CONNECTOR_CHECKED 0x8000

#ifdef FOR_FUTURE_SIMPLIFICATION
Value_Name_Table i2c_bus_flags_table = {
      VN(I2C_BUS_EXISTS),
      VN(I2C_BUS_ACCESSIBLE),
      VN(I2C_BUS_ADDR_0X50),
      VN(I2C_BUS_ADDR_0X37),
      VN(I2C_BUS_ADDR_0X30),
      VN(I2C_BUS_EDP),
      VN(I2C_BUS_LVDS),
      VN(I2C_BUS_PROBED),
      VN(I2C_BUS_VALID_NAME_CHECKED),
      VN(I2C_BUS_HAS_VALID_NAME),
      VN(I2C_BUS_BUSY),
      VN(I2C_BUS_SYSFS_EDID),
      VN(I2C_BUS_DRM_CONNECTOR_CHECKED),
      VN_END
};
#endif


typedef enum {
   DRM_CONNECTOR_NOT_FOUND      = 0,
   DRM_CONNECTOR_FOUND_BY_BUSNO = 1,
   DRM_CONNECTOR_FOUND_BY_EDID  = 2
} Drm_Connector_Found_By;

const char * drm_connector_found_by_name(Drm_Connector_Found_By found_by);

#define I2C_BUS_INFO_MARKER "BINF"
/** Information about one I2C bus */
typedef
struct {
   char             marker[4];          ///< always "BINF"
   int              busno;              ///< I2C device number, i.e. N for /dev/i2c-N
   unsigned long    functionality;      ///< i2c bus functionality flags
   Parsed_Edid *    edid;               ///< parsed EDID, if slave address x50 active
   uint16_t         flags;              ///< I2C_BUS_* flags
   char *           driver;             ///< driver name
   int              open_errno;         ///< errno if open fails (!I2C_BUS_ACCESSIBLE)
   char *           drm_connector_name; ///< from /sys
   Drm_Connector_Found_By
                    drm_connector_found_by;
} I2C_Bus_Info;

char *           interpret_i2c_bus_flags(uint16_t flags);
I2C_Bus_Info *   i2c_new_bus_info(int busno);
void             i2c_free_bus_info(I2C_Bus_Info * bus_info);
void             i2c_gdestroy_bus_info(void * data);
GPtrArray *      i2c_get_all_buses();

// Simple Bus_Info retrieval
I2C_Bus_Info *   i2c_get_bus_info_by_index(guint busndx);
I2C_Bus_Info *   i2c_find_bus_info_by_busno(int busno);

void             i2c_dbgrpt_bus_info(I2C_Bus_Info * bus_info, int depth);
// Reports all detected i2c buses:
int              i2c_dbgrpt_buses(bool report_all, int depth);

void init_i2c_bus_base();

#endif /* I2C_BUS_BASE_H_ */
