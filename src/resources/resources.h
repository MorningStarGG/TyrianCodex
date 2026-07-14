#pragma once
// Resource IDs for assets embedded directly in the DLL (see src/resources/TyrianCodex.rc). The Nexus
// tray / QuickAccess icons live here so they are present the INSTANT the addon loads -- before the data/
// self-download bootstrap fetches the rest of the data folder on a fresh install (Nexus ships only the DLL).
#define RESID_TRAY_ICON        101
#define RESID_TRAY_ICON_HOVER  102
