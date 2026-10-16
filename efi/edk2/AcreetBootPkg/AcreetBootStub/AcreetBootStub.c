/* AcreetBootStub.c — payload stand-in for the 0.2 OVMF smoke test
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Natalie Cole-Clift Spiva / AcreetionOS.
 *
 * Purpose: prove the milestone-0.2 hand-off (LoadImage + StartImage) is REAL:
 * the main menu's hardcoded path \acreetboot\boot\vmtest.efi gets this file
 * staged as the payload inside the test ESP; if the marker string appears on
 * the serial log, the menu successfully loaded and STARTED this image.
 * Nothing more than a banner and a clean return.
 */

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>

EFI_STATUS
EFIAPI
AcreetBootStubEntry (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  Print (L"ACREET-PAYLOAD-OK AcreetBootStub started via LoadImage+StartImage\n");
  return EFI_SUCCESS;
}
