# SPDX-License-Identifier: MIT

if(CONFIG_CODEKEEB_BATTERY_NRF_VDDH_CURVE)
  zephyr_library()
  zephyr_library_sources(src/battery_nrf_vddh_curve.c)
endif()
