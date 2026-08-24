// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"

#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct McdSizeInfo
{
	u16 SectorSize; // Size of each sector, in bytes.  (only 512 and 1024 are valid)
	u16 EraseBlockSizeInSectors; // Size of the erase block, in sectors (max is 16)
	u32 McdSizeInSectors; // Total size of the card, in sectors (no upper limit)
	u8 Xor; // Checksum of previous data
};

struct AvailableMcdInfo
{
	std::string name;
	std::string path;
	std::time_t modified_time;
	MemoryCardType type;
	MemoryCardFileType file_type;
	u32 size;
	bool formatted;
};

extern uint FileMcd_GetMtapPort(uint slot);
extern uint FileMcd_GetMtapSlot(uint slot);
extern bool FileMcd_IsMultitapSlot(uint slot);
extern std::string FileMcd_GetDefaultName(uint slot);

uint FileMcd_ConvertToSlot(uint port, uint slot);
void FileMcd_SetType();
void FileMcd_EmuOpen();
void FileMcd_EmuClose();
void FileMcd_CancelEject();
void FileMcd_Reopen(std::string new_serial);
void FileMcd_Swap();
s32 FileMcd_IsPresent(uint port, uint slot);
void FileMcd_GetSizeInfo(uint port, uint slot, McdSizeInfo* outways);
bool FileMcd_IsPSX(uint port, uint slot);
s32 FileMcd_Read(uint port, uint slot, u8* dest, u32 adr, int size);
s32 FileMcd_Save(uint port, uint slot, const u8* src, u32 adr, int size);
s32 FileMcd_EraseBlock(uint port, uint slot, u32 adr);
u64 FileMcd_GetCRC(uint port, uint slot);
void FileMcd_NextFrame(uint port, uint slot);
int FileMcd_ReIndex(uint port, uint slot, const std::string& filter);

std::vector<AvailableMcdInfo> FileMcd_GetAvailableCards(bool include_in_use_cards);
std::optional<AvailableMcdInfo> FileMcd_GetCardInfo(const std::string_view name);
bool FileMcd_IsMemoryCardFormatted(const std::string& path);
bool FileMcd_IsMemoryCardFormatted(std::FILE* fp);
bool FileMcd_CreateNewCard(const std::string_view name, MemoryCardType type, MemoryCardFileType file_type);
bool FileMcd_RenameCard(const std::string_view name, const std::string_view new_name);
bool FileMcd_DeleteCard(const std::string_view name);

/// Creates an empty (unformatted) memory card at the specified absolute path. When no_ecc is set,
/// the card is written in the raw layout used by .bin/.mc2 cards, otherwise PCSX2's native layout.
bool FileMcd_CreateBlankCard(const std::string& path, uint size_in_mb, bool no_ecc);

/// Returns every memory card in the memory cards directory belonging to the specified game serial,
/// best match first. Matching is done on the start of the file name, so third-party naming schemes
/// such as "SCES-50001 Tekken Tag (Europe).bin" work.
std::vector<std::string> FileMcd_FindCardsForSerial(const std::string_view serial);

/// Returns the memory card which should be used for the specified game serial, creating it when
/// none exists yet. The new card is named after the serial, the GameDB title (falling back to
/// fallback_title) and the region, with the specified extension. When template_card names an
/// existing card, it is copied instead of writing a blank card, which allows new cards to start out
/// already formatted. preferred_card, when it is one of the cards belonging to this game, wins over
/// the automatic pick - that is how the user's choice between several matching cards is honoured.
/// Note: this must not query the settings itself, it is called while the settings lock is held.
std::string FileMcd_GetCardForSerial(const std::string_view serial, const std::string_view fallback_title,
	const std::string_view extension, const std::string_view template_card,
	const std::string_view preferred_card);