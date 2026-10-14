## AcreetBootPkg — acreetboot EDK2 package (milestone 0.2)
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
#
# Builds AcreetMainMenu.efi — our BSD-3 boot-manager application.
# MdePkg / MdeModulePkg are the *references*, never vendored; the EDK2 tree
# itself is fetched + checksum-pinned by tools/edk2-bootstrap.sh.
[Defines]
  PLATFORM_NAME                  = AcreetBootPkg
  PLATFORM_GUID                  = d45789fc-26df-494f-96d7-a1110f1a4ce3
  PLATFORM_VERSION               = 0.2.0
  DSC_SPECIFICATION              = 0x00010006
  OUTPUT_DIRECTORY               = $(WORKSPACE)/Build/AcreetBootPkg
  SUPPORTED_ARCHITECTURES        = X64
  BUILD_TARGETS                  = RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf

[LibraryClasses.common.UEFI_APPLICATION]
  DevicePathLib|MdePkg/Library/UefiDevicePathLibDevicePathProtocol/UefiDevicePathLibDevicePathProtocol.inf

[Components]
  AcreetBootPkg/AcreetMainMenu/AcreetMainMenu.inf
  AcreetBootPkg/AcreetBootStub/AcreetBootStub.inf
