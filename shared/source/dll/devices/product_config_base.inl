/*
 * Copyright (C) 2021-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#if SUPPORT_XE2_HPG_CORE
#ifdef SUPPORT_BMG
DEVICE_CONFIG(BMG_G21_A0, BmgHwConfig, bmgG21DeviceIds, XE2_FAMILY, XE2_HPG_RELEASE)
#endif
#ifdef SUPPORT_LNL
DEVICE_CONFIG(LNL_A0, LnlHwConfig, lnlDeviceIds, XE2_FAMILY, XE2_LPG_RELEASE)
DEVICE_CONFIG(LNL_A1, LnlHwConfig, lnlDeviceIds, XE2_FAMILY, XE2_LPG_RELEASE)
DEVICE_CONFIG(LNL_B0, LnlHwConfig, lnlDeviceIds, XE2_FAMILY, XE2_LPG_RELEASE)
#endif
#endif

#if SUPPORT_XE_HPC_CORE
#ifdef SUPPORT_PVC
DEVICE_CONFIG(PVC_XL_A0, PvcHwConfig, pvcXlDeviceIds, XE_FAMILY, XE_HPC_RELEASE)
DEVICE_CONFIG(PVC_XL_A0P, PvcHwConfig, pvcXlDeviceIds, XE_FAMILY, XE_HPC_RELEASE)
DEVICE_CONFIG(PVC_XT_A0, PvcHwConfig, pvcXtDeviceIds, XE_FAMILY, XE_HPC_RELEASE)
DEVICE_CONFIG(PVC_XT_B0, PvcHwConfig, pvcXtDeviceIds, XE_FAMILY, XE_HPC_RELEASE)
DEVICE_CONFIG(PVC_XT_B1, PvcHwConfig, pvcXtDeviceIds, XE_FAMILY, XE_HPC_RELEASE)
DEVICE_CONFIG(PVC_XT_C0, PvcHwConfig, pvcXtDeviceIds, XE_FAMILY, XE_HPC_RELEASE)
DEVICE_CONFIG(PVC_XT_C0_VG, PvcHwConfig, pvcXtVgDeviceIds, XE_FAMILY, XE_HPC_VG_RELEASE)

#endif
#endif

#ifdef SUPPORT_XE_HPG_CORE
#ifdef SUPPORT_MTL
DEVICE_CONFIG(MTL_U_A0, MtlHwConfig, mtlmDeviceIds, XE_FAMILY, XE_LPG_RELEASE)
DEVICE_CONFIG(MTL_U_B0, MtlHwConfig, mtlmDeviceIds, XE_FAMILY, XE_LPG_RELEASE)
DEVICE_CONFIG(MTL_H_A0, MtlHwConfig, mtlpDeviceIds, XE_FAMILY, XE_LPG_RELEASE)
DEVICE_CONFIG(MTL_H_B0, MtlHwConfig, mtlpDeviceIds, XE_FAMILY, XE_LPG_RELEASE)
#endif
#ifdef SUPPORT_DG2
DEVICE_CONFIG(DG2_G10_A0, Dg2HwConfig, dg2G10DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G10_A1, Dg2HwConfig, dg2G10DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G10_B0, Dg2HwConfig, dg2G10DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G10_C0, Dg2HwConfig, dg2G10DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G11_A0, Dg2HwConfig, dg2G11DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G11_B0, Dg2HwConfig, dg2G11DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G11_B1, Dg2HwConfig, dg2G11DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
DEVICE_CONFIG(DG2_G12_A0, Dg2HwConfig, dg2G12DeviceIds, XE_FAMILY, XE_HPG_RELEASE)
#endif
#ifdef SUPPORT_ARL
DEVICE_CONFIG(ARL_H_A0, ArlHwConfig, arlDeviceIds, XE_FAMILY, XE_LPGPLUS_RELEASE)
DEVICE_CONFIG(ARL_H_B0, ArlHwConfig, arlDeviceIds, XE_FAMILY, XE_LPGPLUS_RELEASE)
#endif
#endif

#ifdef SUPPORT_GEN12LP
#ifdef SUPPORT_TGLLP
DEVICE_CONFIG(TGL, TgllpHw1x6x16, tgllpDeviceIds, XE_FAMILY, XE_LP_RELEASE)
#endif
#ifdef SUPPORT_DG1
DEVICE_CONFIG(DG1, Dg1HwConfig, dg1DeviceIds, XE_FAMILY, XE_LP_RELEASE)
#endif
#ifdef SUPPORT_RKL
DEVICE_CONFIG(RKL, RklHwConfig, rklDeviceIds, XE_FAMILY, XE_LP_RELEASE)
#endif
#ifdef SUPPORT_ADLS
DEVICE_CONFIG(ADL_S, AdlsHwConfig, adlsDeviceIds, XE_FAMILY, XE_LP_RELEASE)
#endif
#ifdef SUPPORT_ADLP
DEVICE_CONFIG(ADL_P, AdlpHwConfig, adlpDeviceIds, XE_FAMILY, XE_LP_RELEASE)
#endif
#ifdef SUPPORT_ADLN
DEVICE_CONFIG(ADL_N, AdlnHwConfig, adlnDeviceIds, XE_FAMILY, XE_LP_RELEASE)
#endif
#endif
