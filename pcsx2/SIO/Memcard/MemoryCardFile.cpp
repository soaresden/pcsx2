// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "SIO/Memcard/MemoryCardFile.h"

#include "SIO/Memcard/MemoryCardFolder.h"
#include "SIO/Sio.h"
#include <SIO/SioTypes.h>

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>

#include "Config.h"
#include "GameDatabase.h"
#include "Host.h"
#include "VMManager.h"
#include "IconsPromptFont.h"

#include "fmt/format.h"

#include <map>

static constexpr int MCD_SIZE = 1024 * 8 * 16; // Legacy PSX card default size

static constexpr int MC2_MBSIZE = 1024 * 528 * 2; // Size of a single megabyte of card data

static constexpr int MC2_ERASE_SIZE = 528 * 16;

static const char* s_folder_mem_card_id_file = "_pcsx2_superblock";

bool FileMcd_Open = false;

// ECC code ported from mymc
// https://sourceforge.net/p/mymc-opl/code/ci/master/tree/ps2mc_ecc.py
// Public domain license

static u32 CalculateECC(u8* buf)
{
	const u8 parity_table[256] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1,
		0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
		1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1,
		0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0,
		1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1,
		0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0,
		1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0,
		1, 0, 1, 1, 0};

	const u8 column_parity_mask[256] = {0, 7, 22, 17, 37, 34, 51, 52, 52, 51, 34, 37, 17, 22,
		7, 0, 67, 68, 85, 82, 102, 97, 112, 119, 119, 112, 97, 102, 82, 85, 68, 67, 82, 85, 68, 67, 119, 112,
		97, 102, 102, 97, 112, 119, 67, 68, 85, 82, 17, 22, 7, 0, 52, 51, 34, 37, 37, 34, 51, 52, 0, 7, 22, 17,
		97, 102, 119, 112, 68, 67, 82, 85, 85, 82, 67, 68, 112, 119, 102, 97, 34, 37, 52, 51, 7, 0, 17, 22,
		22, 17, 0, 7, 51, 52, 37, 34, 51, 52, 37, 34, 22, 17, 0, 7, 7, 0, 17, 22, 34, 37, 52, 51, 112, 119, 102,
		97, 85, 82, 67, 68, 68, 67, 82, 85, 97, 102, 119, 112, 112, 119, 102, 97, 85, 82, 67, 68, 68, 67, 82,
		85, 97, 102, 119, 112, 51, 52, 37, 34, 22, 17, 0, 7, 7, 0, 17, 22, 34, 37, 52, 51, 34, 37, 52, 51, 7, 0,
		17, 22, 22, 17, 0, 7, 51, 52, 37, 34, 97, 102, 119, 112, 68, 67, 82, 85, 85, 82, 67, 68, 112, 119, 102,
		97, 17, 22, 7, 0, 52, 51, 34, 37, 37, 34, 51, 52, 0, 7, 22, 17, 82, 85, 68, 67, 119, 112, 97, 102, 102,
		97, 112, 119, 67, 68, 85, 82, 67, 68, 85, 82, 102, 97, 112, 119, 119, 112, 97, 102, 82, 85, 68, 67,
		0, 7, 22, 17, 37, 34, 51, 52, 52, 51, 34, 37, 17, 22, 7, 0};

	u8 column_parity = 0x77;
	u8 line_parity_0 = 0x7F;
	u8 line_parity_1 = 0x7F;

	for (int i = 0; i < 128; i++)
	{
		u8 b = buf[i];
		column_parity ^= column_parity_mask[b];
		if (parity_table[b])
		{
			line_parity_0 ^= ~i;
			line_parity_1 ^= i;
		}
	}

	return column_parity | (line_parity_0 << 8) | (line_parity_1 << 16);
}

static bool ConvertNoECCtoRAW(const char* file_in, const char* file_out)
{
	auto fin = FileSystem::OpenManagedCFile(file_in, "rb");
	if (!fin)
		return false;

	auto fout = FileSystem::OpenManagedCFile(file_out, "wb");
	if (!fout)
		return false;

	const s64 size = FileSystem::FSize64(fin.get());
	u8 buffer[512];

	for (s64 i = 0; i < (size / 512); i++)
	{
		if (std::fread(buffer, sizeof(buffer), 1, fin.get()) != 1 ||
			std::fwrite(buffer, sizeof(buffer), 1, fout.get()) != 1)
		{
			return false;
		}

		for (int j = 0; j < 4; j++)
		{
			u32 checksum = CalculateECC(&buffer[j * 128]);
			if (std::fwrite(&checksum, 3, 1, fout.get()) != 1)
				return false;
		}

		u32 nullbytes = 0;
		if (std::fwrite(&nullbytes, sizeof(nullbytes), 1, fout.get()) != 1)
			return false;
	}

	if (std::fflush(fout.get()) != 0)
		return false;

	return true;
}

static bool ConvertRAWtoNoECC(const char* file_in, const char* file_out)
{
	auto fin = FileSystem::OpenManagedCFile(file_in, "rb");
	if (!fin)
		return false;

	auto fout = FileSystem::OpenManagedCFile(file_out, "wb");
	if (!fout)
		return false;

	const s64 size = FileSystem::FSize64(fin.get());
	u8 buffer[512];
	u8 checksum[16];

	for (s64 i = 0; i < (size / 528); i++)
	{
		if (std::fread(buffer, sizeof(buffer), 1, fin.get()) != 1 ||
			std::fwrite(buffer, sizeof(buffer), 1, fout.get()) != 1 ||
			std::fread(checksum, sizeof(checksum), 1, fin.get()) != 1)
		{
			return false;
		}
	}

	if (std::fflush(fout.get()) != 0)
		return false;

	return true;
}

// --------------------------------------------------------------------------------------
//  FileMemoryCard
// --------------------------------------------------------------------------------------
// Provides thread-safe direct file IO mapping.
//
class FileMemoryCard
{
protected:
	std::FILE* m_file[8] = {};
	s64 m_fileSize[8] = {};
	std::string m_filenames[8] = {};
	std::vector<u8> m_currentdata;
	u64 m_chksum[8] = {};
	bool m_ispsx[8] = {};
	u32 m_chkaddr = 0;

public:
	FileMemoryCard();
	~FileMemoryCard();

	void Lock();
	void Unlock();

	void Open();
	void Close();

	s32 IsPresent(uint slot);
	void GetSizeInfo(uint slot, McdSizeInfo& outways);
	bool IsPSX(uint slot);
	s32 Read(uint slot, u8* dest, u32 adr, int size);
	s32 Save(uint slot, const u8* src, u32 adr, int size);
	s32 EraseBlock(uint slot, u32 adr);
	u64 GetCRC(uint slot);

protected:
	bool Seek(std::FILE* f, u32 adr);
	bool Create(const char* mcdFile, uint sizeInMB);
};

uint FileMcd_GetMtapPort(uint slot)
{
	switch (slot)
	{
		case 0:
		case 2:
		case 3:
		case 4:
			return 0;
		case 1:
		case 5:
		case 6:
		case 7:
			return 1;

			jNO_DEFAULT
	}

	return 0; // technically unreachable.
}

// Returns the multitap slot number, range 1 to 3 (slot 0 refers to the standard
// 1st and 2nd player slots).
uint FileMcd_GetMtapSlot(uint slot)
{
	switch (slot)
	{
		case 0:
		case 1:
			pxFail("Invalid parameter in call to GetMtapSlot -- specified slot is one of the base slots, not a Multitap slot.");
			break;

		case 2:
		case 3:
		case 4:
			return slot - 1;
		case 5:
		case 6:
		case 7:
			return slot - 4;

			jNO_DEFAULT
	}

	return 0; // technically unreachable.
}

bool FileMcd_IsMultitapSlot(uint slot)
{
	return (slot > 1);
}

std::string FileMcd_GetDefaultName(uint slot)
{
	if (FileMcd_IsMultitapSlot(slot))
		return StringUtil::StdStringFromFormat("Mcd-Multitap%u-Slot%02u.ps2", FileMcd_GetMtapPort(slot) + 1, FileMcd_GetMtapSlot(slot) + 1);
	else
		return StringUtil::StdStringFromFormat("Mcd%03u.ps2", slot + 1);
}

FileMemoryCard::FileMemoryCard()
{
	for (u8 slot = 0; slot < 8; slot++)
	{
		m_fileSize[slot] = -1;
	}
}

FileMemoryCard::~FileMemoryCard() = default;

void FileMemoryCard::Open()
{
	for (int slot = 0; slot < 8; ++slot)
	{
		m_filenames[slot] = {};

		if (EmuConfig.Mcd[slot].Type != MemoryCardType::File)
			continue;

		if (FileMcd_IsMultitapSlot(slot))
		{
			if (!EmuConfig.Pad.MultitapPort0_Enabled && (FileMcd_GetMtapPort(slot) == 0))
				continue;
			if (!EmuConfig.Pad.MultitapPort1_Enabled && (FileMcd_GetMtapPort(slot) == 1))
				continue;
		}

		const std::string fname = EmuConfig.FullpathToMcd(slot);

		if (!EmuConfig.Mcd[slot].Enabled || fname.empty())
		{
			Console.WriteLnFmt("McdSlot {} [File]: [disabled/empty filename]", slot);
			continue;
		}

		if (FileSystem::GetPathFileSize(fname.c_str()) <= 0)
		{
			if (!Create(fname.c_str(), 8))
			{
				Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Creation Failed"),
					fmt::format(TRANSLATE_FS("MemoryCard", "Could not create the memory card:\n{}"),
						fname));
			}
		}

		if (fname.ends_with(".bin") || fname.ends_with(".mc2"))
		{
			std::string newname(fname + "x");
			if (!ConvertNoECCtoRAW(fname.c_str(), newname.c_str()))
			{
				Console.Error("Could convert memory card: %s", fname.c_str());
				FileSystem::DeleteFilePath(newname.c_str());
				continue;
			}

			// store the original filename
			m_file[slot] = FileSystem::OpenSharedCFile(newname.c_str(), "r+b", FileSystem::FileShareMode::DenyWrite);
		}
		else
		{
			m_file[slot] = FileSystem::OpenSharedCFile(fname.c_str(), "r+b", FileSystem::FileShareMode::DenyWrite);
		}

		if (!m_file[slot])
		{
			Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Read Failed"),
				fmt::format(TRANSLATE_FS("MemoryCard", "Unable to access memory card:\n\n{}\n\n"
													   "Another instance of PCSX2 may be using this memory card "
													   "or the memory card is stored in a write-protected folder.\n"
													   "Close any other instances of PCSX2, or restart your computer.\n"),
					fname));
		}
		else // Load checksum
		{
			m_fileSize[slot] = FileSystem::FSize64(m_file[slot]);

			Console.WriteLnFmt(Color_Green, "McdSlot {} [File]: {} [{} MB, {}]", slot, Path::GetFileName(fname),
				(m_fileSize[slot] + (MCD_SIZE + 1)) / MC2_MBSIZE,
				FileMcd_IsMemoryCardFormatted(m_file[slot]) ? "Formatted" : "UNFORMATTED");

			m_filenames[slot] = std::move(fname);
			m_ispsx[slot] = m_fileSize[slot] == 0x20000;
			m_chkaddr = 0x210;

			if (!m_ispsx[slot] && FileSystem::FSeek64(m_file[slot], m_chkaddr, SEEK_SET) == 0)
			{
				const size_t read_result = std::fread(&m_chksum[slot], sizeof(m_chksum[slot]), 1, m_file[slot]);
				if (read_result == 0)
					Host::ReportErrorAsync("Memory Card Read Failed", "Error reading memory card.");
			}
		}
	}
}

void FileMemoryCard::Close()
{
	for (int slot = 0; slot < 8; ++slot)
	{
		if (!m_file[slot])
			continue;

		// Store checksum
		if (!m_ispsx[slot] && FileSystem::FSeek64(m_file[slot], m_chkaddr, SEEK_SET) == 0)
			std::fwrite(&m_chksum[slot], sizeof(m_chksum[slot]), 1, m_file[slot]);

		std::fclose(m_file[slot]);
		m_file[slot] = nullptr;

		if (m_filenames[slot].ends_with(".bin") || m_filenames[slot].ends_with(".mc2"))
		{
			const std::string name_in(m_filenames[slot] + 'x');
			if (ConvertRAWtoNoECC(name_in.c_str(), m_filenames[slot].c_str()))
				FileSystem::DeleteFilePath(name_in.c_str());
		}

		m_filenames[slot] = {};
		m_fileSize[slot] = -1;
	}
}

// Returns FALSE if the seek failed (is outside the bounds of the file).
bool FileMemoryCard::Seek(std::FILE* f, u32 adr)
{
	return (FileSystem::FSeek64(f, adr, SEEK_SET) == 0);
}

// returns FALSE if an error occurred (either permission denied or disk full)
bool FileMcd_CreateBlankCard(const std::string& path, uint size_in_mb, bool no_ecc)
{
	//int enc[16] = {0x77,0x7f,0x7f,0x77,0x7f,0x7f,0x77,0x7f,0x7f,0x77,0x7f,0x7f,0,0,0,0};

	Console.WriteLn("(FileMcd) Creating new %uMB %s memory card: %s", size_in_mb, no_ecc ? "raw" : "ECC", path.c_str());

	auto fp = FileSystem::OpenManagedCFile(path.c_str(), "wb");
	if (!fp)
		return false;

	// A megabyte of card data is 2048 sectors; with ECC each sector is 528 bytes instead of 512.
	const u64 total_size = static_cast<u64>(size_in_mb) * (no_ecc ? (1024 * 512 * 2) : MC2_MBSIZE);

	u8 buf[MC2_ERASE_SIZE];
	std::memset(buf, 0xff, sizeof(buf));

	// The raw size is not a multiple of the erase block size, so write whatever is left over after
	// the last full block as well.
	for (u64 written = 0; written < total_size;)
	{
		const size_t chunk = static_cast<size_t>(std::min<u64>(sizeof(buf), total_size - written));
		if (std::fwrite(buf, chunk, 1, fp.get()) != 1)
			return false;

		written += chunk;
	}
	return true;
}

bool FileMemoryCard::Create(const char* mcdFile, uint sizeInMB)
{
	// .bin/.mc2 cards are stored without ECC data, and get converted to the raw layout on open.
	const std::string_view name(mcdFile);
	const bool no_ecc = name.ends_with(".bin") || name.ends_with(".mc2");
	return FileMcd_CreateBlankCard(std::string(name), sizeInMB, no_ecc);
}

s32 FileMemoryCard::IsPresent(uint slot)
{
	return m_file[slot] != nullptr;
}

void FileMemoryCard::GetSizeInfo(uint slot, McdSizeInfo& outways)
{
	outways.SectorSize = 512; // 0x0200
	outways.EraseBlockSizeInSectors = 16; // 0x0010
	outways.Xor = 18; // 0x12, XOR 02 00 00 10

	pxAssert(m_file[slot]);
	if (m_file[slot])
		outways.McdSizeInSectors = static_cast<u32>(m_fileSize[slot]) / (outways.SectorSize + outways.EraseBlockSizeInSectors);
	else
		outways.McdSizeInSectors = 0x4000;

	u8* pdata = (u8*)&outways.McdSizeInSectors;
	outways.Xor ^= pdata[0] ^ pdata[1] ^ pdata[2] ^ pdata[3];
}

bool FileMemoryCard::IsPSX(uint slot)
{
	return m_ispsx[slot];
}

s32 FileMemoryCard::Read(uint slot, u8* dest, u32 adr, int size)
{
	std::FILE* mcfp = m_file[slot];
	if (!mcfp)
	{
		DevCon.Error("(FileMcd) Ignoring attempted read from disabled slot.");
		memset(dest, 0, size);
		return 1;
	}
	if (!Seek(mcfp, adr))
		return 0;
	return std::fread(dest, size, 1, mcfp) == 1;
}

s32 FileMemoryCard::Save(uint slot, const u8* src, u32 adr, int size)
{
	std::FILE* mcfp = m_file[slot];

	if (!mcfp)
	{
		DevCon.Error("(FileMcd) Ignoring attempted save/write to disabled slot.");
		return 1;
	}

	if (m_ispsx[slot])
	{
		if (static_cast<int>(m_currentdata.size()) < size)
			m_currentdata.resize(size);
		for (int i = 0; i < size; i++)
			m_currentdata[i] = src[i];
	}
	else
	{
		if (!Seek(mcfp, adr))
			return 0;
		if (static_cast<int>(m_currentdata.size()) < size)
			m_currentdata.resize(size);

		const size_t read_result = std::fread(m_currentdata.data(), size, 1, mcfp);
		if (read_result == 0)
			Host::ReportErrorAsync("Memory Card Read Failed", "Error reading memory card.");

		for (int i = 0; i < size; i++)
		{
			if ((m_currentdata[i] & src[i]) != src[i])
				Console.Warning("(FileMcd) Warning: writing to uncleared data. (%d) [%08X]", slot, adr);
			m_currentdata[i] &= src[i];
		}

		// Checksumness
		{
			if (adr == m_chkaddr)
				Console.Warning("(FileMcd) Warning: checksum sector overwritten. (%d)", slot);

			u64* pdata = (u64*)&m_currentdata[0];
			u32 loops = size / 8;

			for (u32 i = 0; i < loops; i++)
				m_chksum[slot] ^= pdata[i];
		}
	}

	if (!Seek(mcfp, adr))
		return 0;

	if (std::fwrite(m_currentdata.data(), size, 1, mcfp) == 1)
	{
		static auto last = std::chrono::time_point<std::chrono::system_clock>();

		std::chrono::duration<float> elapsed = std::chrono::system_clock::now() - last;
		if (elapsed > std::chrono::seconds(5))
		{
			Host::AddIconOSDMessage(fmt::format("MemoryCardSave{}", slot), ICON_PF_MEMORY_CARD,
				fmt::format(TRANSLATE_FS("MemoryCard", "Memory Card '{}' was saved to storage."),
					Path::GetFileName(m_filenames[slot])),
				Host::OSD_INFO_DURATION);
			last = std::chrono::system_clock::now();
		}
		return 1;
	}

	return 0;
}

s32 FileMemoryCard::EraseBlock(uint slot, u32 adr)
{
	std::FILE* mcfp = m_file[slot];
	if (!mcfp)
	{
		DevCon.Error("MemoryCard: Ignoring erase for disabled slot.");
		return 1;
	}

	if (!Seek(mcfp, adr))
		return 0;

	u8 buf[MC2_ERASE_SIZE];
	std::memset(buf, 0xff, sizeof(buf));
	return std::fwrite(buf, sizeof(buf), 1, mcfp) == 1;
}

u64 FileMemoryCard::GetCRC(uint slot)
{
	std::FILE* mcfp = m_file[slot];
	if (!mcfp)
		return 0;

	u64 retval = 0;

	if (m_ispsx[slot])
	{
		if (!Seek(mcfp, 0))
			return 0;

		const s64 mcfpsize = m_fileSize[slot];
		if (mcfpsize < 0)
			return 0;

		// Process the file in 4k chunks.  Speeds things up significantly.

		u64 buffer[528 * 8]; // use 528 (sector size), ensures even divisibility

		const uint filesize = static_cast<uint>(mcfpsize) / sizeof(buffer);
		for (uint i = filesize; i; --i)
		{
			if (std::fread(buffer, sizeof(buffer), 1, mcfp) != 1)
				return 0;

			for (uint t = 0; t < std::size(buffer); ++t)
				retval ^= buffer[t];
		}
	}
	else
	{
		retval = m_chksum[slot];
	}

	return retval;
}

// --------------------------------------------------------------------------------------
//  MemoryCard Component API Bindings
// --------------------------------------------------------------------------------------
namespace Mcd
{
	FileMemoryCard impl; // class-based implementations we refer to when API is invoked
	FolderMemoryCardAggregator implFolder;
}; // namespace Mcd

uint FileMcd_ConvertToSlot(uint port, uint slot)
{
	if (slot == 0)
		return port;
	if (port == 0)
		return slot + 1; // multitap 1
	return slot + 4; // multitap 2
}

void FileMcd_SetType()
{
	// detect inserted memory card types
	for (uint slot = 0; slot < 8; ++slot)
	{
		if (EmuConfig.Mcd[slot].Filename.empty())
		{
			EmuConfig.Mcd[slot].Type = MemoryCardType::Empty;
		}
		else if (EmuConfig.Mcd[slot].Enabled)
		{
			MemoryCardType type = MemoryCardType::File; // default to file if we can't find anything at the path so it gets auto-generated

			const std::string path(EmuConfig.FullpathToMcd(slot));
			if (FileSystem::DirectoryExists(path.c_str()))
				type = MemoryCardType::Folder;

			EmuConfig.Mcd[slot].Type = type;
		}
	}
}

void FileMcd_EmuOpen()
{
	if (FileMcd_Open)
		return;
	FileMcd_Open = true;


	Mcd::impl.Open();
	Mcd::implFolder.SetFiltering(true);
	Mcd::implFolder.Open();
}

void FileMcd_EmuClose()
{
	if (!FileMcd_Open)
		return;
	FileMcd_Open = false;
	Mcd::implFolder.Close();
	Mcd::impl.Close();
}

void FileMcd_CancelEject()
{
	AutoEject::ClearAll();
}

void FileMcd_Reopen(std::string new_serial)
{
	Console.WriteLn("Reopening memory cards...");
	FileMcd_EmuClose();
	FileMcd_SetType();
	sioSetGameSerial(new_serial);
	FileMcd_EmuOpen();
}

static bool FileMcd_IsAutoEjecting()
{
	for (size_t port = 0; port < SIO::PORTS; ++port)
	{
		for (size_t slot = 0; slot < SIO::SLOTS; ++slot)
		{
			if (mcds[port][slot].autoEjectTicks > 0)
			{
				return true; // Auto-eject is active
			}
		}
	}
	return false; // No auto-eject active
}

void FileMcd_Swap()
{
	if (MemcardBusy::IsBusy())
	{
		Host::AddIconOSDMessage("MemoryCardSwap_Busy", ICON_PF_MEMORY_CARD, TRANSLATE_SV("MemoryCardSwap_Busy", "Memory cards are busy. Can't swap right now."));
		return;
	}

	// Slot 1 is owned by the per-game override, and would be put straight back on the next
	// settings reload - leaving both slots pointing at the same file.
	if (Host::GetBoolSettingValue("MemoryCards", "PerGameCards", false))
	{
		Host::AddIconOSDMessage("MemoryCardSwap_PerGame", ICON_PF_MEMORY_CARD,
			TRANSLATE_SV("MemoryCardSwap_PerGame",
				"Can't swap memory cards while per-game memory cards are enabled."));
		return;
	}

	// Check if auto-eject is active
	if (FileMcd_IsAutoEjecting())
	{
		Host::AddIconOSDMessage("MemoryCardSwap_AutoEject", ICON_PF_MEMORY_CARD, TRANSLATE_SV("MemoryCardSwap_AutoEject", "Memory cards are being auto-ejected. Can't swap right now."));
		return;
	}

	const std::string card1Filename = Host::GetStringSettingValue("MemoryCards", "Slot1_Filename");
	const std::string card2Filename = Host::GetStringSettingValue("MemoryCards", "Slot2_Filename");

	// Copy each McdOptions to local memory
	Pcsx2Config::McdOptions firstSlot = EmuConfig.Mcd[0];
	Pcsx2Config::McdOptions secondSlot = EmuConfig.Mcd[1];

	if (!firstSlot.Enabled || !secondSlot.Enabled || card1Filename.empty() || card2Filename.empty())
	{
		Host::AddIconOSDMessage("MemoryCardSwap_EmptySlot", ICON_PF_MEMORY_CARD, TRANSLATE_SV("MemoryCard_EmptySlot", "Both slots must have a card selected to swap."));
		return;
	}

	// Swap them
	Host::SetBaseStringSettingValue("MemoryCards", "Slot1_Filename", card2Filename.c_str());
	Host::SetBaseStringSettingValue("MemoryCards", "Slot2_Filename", card1Filename.c_str());
	Host::CommitBaseSettingChanges();
	VMManager::ApplySettings();
	EmuConfig.Mcd[0] = secondSlot;
	EmuConfig.Mcd[1] = firstSlot;

	// Reopen them
	FileMcd_EmuClose();
	FileMcd_SetType();
	FileMcd_EmuOpen();
	AutoEject::SetAll();
	Host::AddIconOSDMessage("MemoryCardSwap", ICON_PF_MEMORY_CARD, fmt::format(TRANSLATE_FS("MemoryCardSwap", "Memory Cards have been swapped.\nSlot 1: {}\nSlot 2: {}"), EmuConfig.Mcd[0].Filename, EmuConfig.Mcd[1].Filename), Host::OSD_INFO_DURATION);
}

s32 FileMcd_IsPresent(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.IsPresent(combinedSlot);
		case MemoryCardType::Folder:
			return Mcd::implFolder.IsPresent(combinedSlot);
		default:
			return false;
	}
}

void FileMcd_GetSizeInfo(uint port, uint slot, McdSizeInfo* outways)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			Mcd::impl.GetSizeInfo(combinedSlot, *outways);
			break;
		case MemoryCardType::Folder:
			Mcd::implFolder.GetSizeInfo(combinedSlot, *outways);
			break;
		default:
			return;
	}
}

bool FileMcd_IsPSX(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.IsPSX(combinedSlot);
		case MemoryCardType::Folder:
			return Mcd::implFolder.IsPSX(combinedSlot);
		default:
			return false;
	}
}

s32 FileMcd_Read(uint port, uint slot, u8* dest, u32 adr, int size)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.Read(combinedSlot, dest, adr, size);
		case MemoryCardType::Folder:
			return Mcd::implFolder.Read(combinedSlot, dest, adr, size);
		default:
			return 0;
	}
}

s32 FileMcd_Save(uint port, uint slot, const u8* src, u32 adr, int size)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.Save(combinedSlot, src, adr, size);
		case MemoryCardType::Folder:
			return Mcd::implFolder.Save(combinedSlot, src, adr, size);
		default:
			return 0;
	}
}

s32 FileMcd_EraseBlock(uint port, uint slot, u32 adr)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.EraseBlock(combinedSlot, adr);
		case MemoryCardType::Folder:
			return Mcd::implFolder.EraseBlock(combinedSlot, adr);
		default:
			return 0;
	}
}

u64 FileMcd_GetCRC(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		case MemoryCardType::File:
			return Mcd::impl.GetCRC(combinedSlot);
		case MemoryCardType::Folder:
			return Mcd::implFolder.GetCRC(combinedSlot);
		default:
			return 0;
	}
}

void FileMcd_NextFrame(uint port, uint slot)
{
	const uint combinedSlot = FileMcd_ConvertToSlot(port, slot);
	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		//case MemoryCardType::MemoryCard_File:
		//	Mcd::impl.NextFrame( combinedSlot );
		//	break;
		case MemoryCardType::Folder:
			Mcd::implFolder.NextFrame(combinedSlot);
			break;
		default:
			return;
	}
}

int FileMcd_ReIndex(uint port, uint slot, const std::string& filter)
{
	const int combinedSlot = FileMcd_ConvertToSlot(port, slot);

	switch (EmuConfig.Mcd[combinedSlot].Type)
	{
		//case MemoryCardType::File:
		//	return Mcd::impl.ReIndex( combinedSlot, filter );
		//	break;
		case MemoryCardType::Folder:
			if (!Mcd::implFolder.ReIndex(combinedSlot, true, filter))
				return -1;
			break;
		default:
			return -1;
			break;
	}

	return combinedSlot;
}

// --------------------------------------------------------------------------------------
//  Library API Implementations
// --------------------------------------------------------------------------------------

static MemoryCardFileType GetMemoryCardFileTypeFromSize(s64 size)
{
	// Handle both ecc and non ecc versions
	if (size == (8 * MC2_MBSIZE) || size == _8mb)
		return MemoryCardFileType::PS2_8MB;
	else if (size == (16 * MC2_MBSIZE) || size == _16mb)
		return MemoryCardFileType::PS2_16MB;
	else if (size == (32 * MC2_MBSIZE) || size == _32mb)
		return MemoryCardFileType::PS2_32MB;
	else if (size == (64 * MC2_MBSIZE) || size == _64mb)
		return MemoryCardFileType::PS2_64MB;
	else if (size == MCD_SIZE)
		return MemoryCardFileType::PS1;
	else
		return MemoryCardFileType::Unknown;
}

static bool FileMcd_IsFolder(const std::string& path)
{
	const std::string superblock_path(Path::Combine(path, s_folder_mem_card_id_file));
	return FileSystem::FileExists(superblock_path.c_str());
}

bool FileMcd_IsMemoryCardFormatted(const std::string& path)
{
	auto fp = FileSystem::OpenManagedSharedCFile(path.c_str(), "rb", FileSystem::FileShareMode::DenyNone);
	if (!fp)
		return false;

	return FileMcd_IsMemoryCardFormatted(fp.get());
}

bool FileMcd_IsMemoryCardFormatted(std::FILE* fp)
{
	static const char formatted_psx[] = "MC";
	static const char formatted_string[] = "Sony PS2 Memory Card Format";
	static constexpr size_t read_length = sizeof(formatted_string) - 1;

	const s64 pos = FileSystem::FTell64(fp);

	u8 data[read_length];
	const bool okay = (FileSystem::FSeek64(fp, 0, SEEK_SET) == 0 && std::fread(data, read_length, 1, fp) == 1);
	FileSystem::FSeek64(fp, pos, SEEK_SET);
	if (!okay)
		return false;

	return (std::memcmp(data, formatted_string, sizeof(formatted_string) - 1) == 0 ||
			std::memcmp(data, formatted_psx, sizeof(formatted_psx) - 1) == 0);
}

std::vector<AvailableMcdInfo> FileMcd_GetAvailableCards(bool include_in_use_cards)
{
	std::vector<FILESYSTEM_FIND_DATA> files;
	FileSystem::FindFiles(EmuFolders::MemoryCards.c_str(), "*",
		FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_FOLDERS | FILESYSTEM_FIND_HIDDEN_FILES, &files);

	std::vector<AvailableMcdInfo> mcds;
	mcds.reserve(files.size());

	for (FILESYSTEM_FIND_DATA& fd : files)
	{
		std::string basename(Path::GetFileName(fd.FileName));
		if (!include_in_use_cards)
		{
			bool in_use = false;
			for (size_t i = 0; i < std::size(EmuConfig.Mcd); i++)
			{
				if (EmuConfig.Mcd[i].Filename == basename)
				{
					in_use = true;
					break;
				}
			}
			if (in_use)
				continue;
		}

		// We only want relevant file types.
		if (!(fd.FileName.ends_with(".ps2") || fd.FileName.ends_with(".mcr") ||
				fd.FileName.ends_with(".mcd") || fd.FileName.ends_with(".bin") ||
				fd.FileName.ends_with(".mc2")))
			continue;

		if (fd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!FileMcd_IsFolder(fd.FileName))
				continue;

			FolderMemoryCard sourceFolderMemoryCard;
			Pcsx2Config::McdOptions config;
			config.Enabled = true;
			config.Type = MemoryCardType::Folder;
			sourceFolderMemoryCard.Open(fd.FileName, config, (8 * 1024 * 1024) / FolderMemoryCard::ClusterSize, true, "");

			mcds.push_back({std::move(basename), std::move(fd.FileName), fd.ModificationTime,
				MemoryCardType::Folder, MemoryCardFileType::Unknown, 0u, sourceFolderMemoryCard.IsFormatted()});
			sourceFolderMemoryCard.Close(false);
		}
		else
		{
			if (fd.Size < MCD_SIZE)
				continue;

			const bool formatted = FileMcd_IsMemoryCardFormatted(fd.FileName);
			mcds.push_back({std::move(basename), std::move(fd.FileName), fd.ModificationTime,
				MemoryCardType::File, GetMemoryCardFileTypeFromSize(fd.Size),
				static_cast<u32>(fd.Size), formatted});
		}
	}

	std::sort(mcds.begin(), mcds.end(), [](auto& a, auto& b) { return a.name < b.name; });
	return mcds;
}

std::optional<AvailableMcdInfo> FileMcd_GetCardInfo(const std::string_view name)
{
	std::optional<AvailableMcdInfo> ret;

	std::string basename(name);
	std::string path(Path::Combine(EmuFolders::MemoryCards, basename));

	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(path.c_str(), &sd))
		return ret;

	if (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
	{
		if (FileMcd_IsFolder(path))
		{
			ret = {std::move(basename), std::move(path), sd.ModificationTime,
				MemoryCardType::Folder, MemoryCardFileType::Unknown, 0u, true};
		}
	}
	else
	{
		if (sd.Size >= MCD_SIZE)
		{
			const bool formatted = FileMcd_IsMemoryCardFormatted(path);
			ret = {std::move(basename), std::move(path), sd.ModificationTime,
				MemoryCardType::File, GetMemoryCardFileTypeFromSize(sd.Size),
				static_cast<u32>(sd.Size), formatted};
		}
	}

	return ret;
}

bool FileMcd_CreateNewCard(const std::string_view name, MemoryCardType type, MemoryCardFileType file_type)
{
	const std::string full_path(Path::Combine(EmuFolders::MemoryCards, name));

	if (type == MemoryCardType::Folder)
	{
		Console.WriteLn("(FileMcd) Creating new PS2 folder memory card: '%.*s'", static_cast<int>(name.size()), name.data());

		Error error;
		if (!FileSystem::CreateDirectoryPath(full_path.c_str(), false, &error))
		{
			Host::ReportErrorAsync("Memory Card Creation Failed",
				fmt::format("Failed to create directory. The error was:\n{}", error.GetDescription()));
			return false;
		}

		// write the superblock
		auto fp = FileSystem::OpenManagedCFile(Path::Combine(full_path, s_folder_mem_card_id_file).c_str(), "wb", &error);
		if (!fp)
		{
			Host::ReportErrorAsync("Memory Card Creation Failed", fmt::format("Failed to create superblock. The error was:\n{}", error.GetDescription()));
			return false;
		}

		return true;
	}

	if (type == MemoryCardType::File)
	{
		if (file_type <= MemoryCardFileType::Unknown || file_type >= MemoryCardFileType::MaxCount)
			return false;

		static constexpr std::array<u32, static_cast<size_t>(MemoryCardFileType::MaxCount)> sizes = {{0, 8 * MC2_MBSIZE, 16 * MC2_MBSIZE, 32 * MC2_MBSIZE, 64 * MC2_MBSIZE, MCD_SIZE}};

		const bool isPSX = (type == MemoryCardType::File && file_type == MemoryCardFileType::PS1);
		const u32 size = sizes[static_cast<u32>(file_type)];
		if (!isPSX && size == 0)
			return false;

		Error error;
		auto fp = FileSystem::OpenManagedCFile(full_path.c_str(), "wb", &error);
		if (!fp)
		{
			Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Creation Failed"),
				fmt::format(TRANSLATE_FS("MemoryCard", "Failed to create memory card. The error was:\n{}"), error.GetDescription()));
			return false;
		}

		if (!isPSX)
		{
			Console.WriteLn("(FileMcd) Creating new PS2 %uMB memory card: '%s'", size / MC2_MBSIZE, full_path.c_str());

			// PS2 Memory Card
			u8 buf[MC2_ERASE_SIZE];
			std::memset(buf, 0xff, sizeof(buf));

			const u32 count = size / sizeof(buf);
			for (uint i = 0; i < count; i++)
			{
				if (std::fwrite(buf, sizeof(buf), 1, fp.get()) != 1)
				{
					Host::ReportErrorAsync("Memory Card Creation Failed",
						fmt::format("Failed to write memory card file:\n{}", full_path));
					return false;
				}
			}

			return true;
		}
		else
		{
			Console.WriteLn("(FileMcd) Creating new PSX 128 KiB memory card: '%s'", full_path.c_str());

			// PSX Memory Card; 8192 is the size in bytes of a single block of a PSX memory card (8 KiB).
			u8 buf[8192];
			std::memset(buf, 0xff, sizeof(buf));

			// PSX cards consist of 16 blocks, each 8 KiB in size.
			for (uint i = 0; i < 16; i++)
			{
				if (std::fwrite(buf, sizeof(buf), 1, fp.get()) != 1)
				{
					Host::ReportErrorAsync("Memory Card Creation Failed",
						fmt::format("Failed to write memory card file:\n{}", full_path));
					return false;
				}
			}

			return true;
		}
	}

	return false;
}

bool FileMcd_RenameCard(const std::string_view name, const std::string_view new_name)
{
	const std::string name_path(Path::Combine(EmuFolders::MemoryCards, name));
	const std::string new_name_path(Path::Combine(EmuFolders::MemoryCards, new_name));

	FILESYSTEM_STAT_DATA sd, new_sd;
	if (!FileSystem::StatFile(name_path.c_str(), &sd) || FileSystem::StatFile(new_name_path.c_str(), &new_sd))
	{
		Console.Error("(FileMcd) New name already exists, or old name does not");
		return false;
	}

	Console.WriteLn("(FileMcd) Renaming memory card '%.*s' to '%.*s'",
		static_cast<int>(name.size()), name.data(),
		static_cast<int>(new_name.size()), new_name.data());

	if (!FileSystem::RenamePath(name_path.c_str(), new_name_path.c_str()))
	{
		Console.Error("(FileMcd) Failed to rename '%s' to '%s'", name_path.c_str(), new_name_path.c_str());
		return false;
	}

	return true;
}

bool FileMcd_DeleteCard(const std::string_view name)
{
	const std::string name_path(Path::Combine(EmuFolders::MemoryCards, name));

	FILESYSTEM_STAT_DATA sd;
	if (!FileSystem::StatFile(name_path.c_str(), &sd))
	{
		Console.Error("(FileMcd) Can't stat '%s' for deletion", name_path.c_str());
		return false;
	}

	Console.WriteLn("(FileMcd) Deleting memory card '%.*s'", static_cast<int>(name.size()), name.data());

	if (sd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY)
	{
		// must be a folder memcard, so do a recursive delete (scary)
		if (!FileSystem::RecursiveDeleteDirectory(name_path.c_str()))
		{
			Console.Error("(FileMcd) Failed to recursively delete '%s'", name_path.c_str());
			return false;
		}
	}
	else
	{
		if (!FileSystem::DeleteFilePath(name_path.c_str()))
		{
			Console.Error("(FileMcd) Failed to delete file '%s'", name_path.c_str());
			return false;
		}
	}

	return true;
}

// --------------------------------------------------------------------------------------
//  Per-Game Memory Cards
// --------------------------------------------------------------------------------------
// When enabled, slot 1 is automatically pointed at a memory card dedicated to the running
// game. An existing card is detected by matching the game's serial against the start of the
// file name, which makes cards produced by other tools (e.g. OPL VMCs named
// "SCES-50001 TEKKEN TAG TOURNAMENT (Europe).bin") usable as-is. If no card matches, one is
// created using the serial, the GameDB title and the region.

namespace
{
	// Strips everything but alphanumerics and uppercases, so that "SCES-50001", "SCES_500.01"
	// and "sces50001" all compare equal.
	std::string PerGameMcd_NormalizeSerial(const std::string_view str)
	{
		std::string ret;
		ret.reserve(str.size());
		for (const char ch : str)
		{
			if (std::isalnum(static_cast<unsigned char>(ch)))
				ret.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
		}
		return ret;
	}

	bool PerGameMcd_IsCardFileName(const std::string_view name)
	{
		// Keep in sync with FileMcd_GetAvailableCards().
		return name.ends_with(".ps2") || name.ends_with(".bin") || name.ends_with(".mc2") ||
			   name.ends_with(".mcd") || name.ends_with(".mcr");
	}

	// Region label used when creating a new card. Derived from the serial prefix, which is more
	// reliable (and matches third-party naming conventions) than the GameDB region string.
	std::string_view PerGameMcd_GetRegionName(const std::string_view normalized_serial)
	{
		if (normalized_serial.size() < 4)
			return {};

		const std::string_view prefix = normalized_serial.substr(0, 4);
		if (prefix.ends_with("ES") || prefix.ends_with("ED"))
			return "Europe";
		if (prefix.ends_with("US") || prefix.ends_with("UD"))
			return "USA";
		if (prefix.ends_with("PS") || prefix.ends_with("PM"))
			return "Japan";
		if (prefix.ends_with("KA"))
			return "Korea";
		if (prefix.ends_with("AJ"))
			return "Asia";
		return {};
	}

	// Returns true if file_name belongs to the game identified by normalized_serial, i.e. it starts
	// with the serial and the serial is not merely a prefix of a longer one.
	bool PerGameMcd_NameMatchesSerial(const std::string_view file_name, const std::string_view normalized_serial)
	{
		const std::string_view title(Path::GetFileTitle(file_name));

		// Consume the serial, ignoring whatever separators the name happens to use.
		size_t pos = 0;
		size_t matched = 0;
		for (; pos < title.size() && matched < normalized_serial.size(); pos++)
		{
			const unsigned char ch = static_cast<unsigned char>(title[pos]);
			if (!std::isalnum(ch))
				continue;
			if (static_cast<char>(std::toupper(ch)) != normalized_serial[matched])
				return false;

			matched++;
		}

		if (matched != normalized_serial.size())
			return false;

		// The boundary check runs on the original name, not the normalized one, otherwise a title
		// starting with a digit ("SLES-53667 24 The Game") would look like a longer serial. What we
		// actually want to reject is "SLES-5000" claiming "SLES-50001 Foo.bin".
		return pos >= title.size() || !std::isalnum(static_cast<unsigned char>(title[pos]));
	}

	// .bin/.mc2 cards are stored without ECC data (the layout used by OPL and by real hardware
	// dumps); everything else uses PCSX2's native layout, which includes ECC.
	bool PerGameMcd_UsesRawLayout(const std::string_view name)
	{
		return name.ends_with(".bin") || name.ends_with(".mc2");
	}

	// Seeds a freshly created card from the template card configured in
	// [MemoryCards]/PerGameCardsTemplate, so that new cards can start out already formatted.
	bool PerGameMcd_CreateFromTemplate(const std::string& full_path, const std::string_view template_name)
	{
		if (template_name.empty())
			return false;

		const std::string template_path(Path::Combine(EmuFolders::MemoryCards, template_name));
		if (FileSystem::GetPathFileSize(template_path.c_str()) <= 0)
		{
			Console.Warning("(FileMcd) Per-game card template '%s' does not exist, ignoring.", template_path.c_str());
			return false;
		}

		// The template and the new card do not necessarily use the same on-disk layout, and copying
		// one into the other verbatim would produce garbage once the ECC conversion runs on open.
		const bool src_raw = PerGameMcd_UsesRawLayout(template_path);
		const bool dst_raw = PerGameMcd_UsesRawLayout(full_path);

		bool result;
		if (src_raw == dst_raw)
			result = FileSystem::CopyFilePath(template_path.c_str(), full_path.c_str(), false);
		else if (src_raw)
			result = ConvertNoECCtoRAW(template_path.c_str(), full_path.c_str());
		else
			result = ConvertRAWtoNoECC(template_path.c_str(), full_path.c_str());

		if (!result)
		{
			Console.Warning("(FileMcd) Failed to create '%s' from per-game card template '%s'.",
				full_path.c_str(), template_path.c_str());
			FileSystem::DeleteFilePath(full_path.c_str());
			return false;
		}

		Console.WriteLnFmt("(FileMcd) Created per-game memory card '{}' from template '{}'.",
			Path::GetFileName(full_path), template_name);
		return true;
	}
} // namespace

std::string FileMcd_FindCardForSerial(const std::string_view serial)
{
	const std::string normalized_serial(PerGameMcd_NormalizeSerial(serial));
	if (normalized_serial.empty())
		return {};

	FileSystem::FindResultsArray results;
	FileSystem::FindFiles(EmuFolders::MemoryCards.c_str(), "*",
		FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_FOLDERS | FILESYSTEM_FIND_RELATIVE_PATHS | FILESYSTEM_FIND_SORT_BY_NAME,
		&results);

	std::string best;
	for (FILESYSTEM_FIND_DATA& fd : results)
	{
		const bool is_directory = (fd.Attributes & FILESYSTEM_FILE_ATTRIBUTE_DIRECTORY) != 0;
		if (is_directory)
		{
			if (!FileMcd_IsFolder(Path::Combine(EmuFolders::MemoryCards, fd.FileName)))
				continue;
		}
		else if (!PerGameMcd_IsCardFileName(fd.FileName))
		{
			continue;
		}

		if (!PerGameMcd_NameMatchesSerial(fd.FileName, normalized_serial))
			continue;

		// An exact "SCES-50001.bin" wins over "SCES-50001 Some Title (Europe).bin".
		if (PerGameMcd_NormalizeSerial(Path::GetFileTitle(fd.FileName)).size() == normalized_serial.size())
			return std::move(fd.FileName);

		if (best.empty())
			best = std::move(fd.FileName);
	}

	return best;
}

std::string FileMcd_GetCardForSerial(const std::string_view serial, const std::string_view fallback_title,
	const std::string_view extension_setting, const std::string_view template_card)
{
	const std::string normalized_serial(PerGameMcd_NormalizeSerial(serial));
	if (normalized_serial.empty())
		return {};

	std::string existing(FileMcd_FindCardForSerial(serial));
	if (!existing.empty())
		return existing;

	// Nothing matched, so build a name out of the serial, the title and the region.
	std::string title(fallback_title);
	if (const GameDatabaseSchema::GameEntry* game = GameDatabase::findGame(serial))
		title = game->name;

	const std::string_view region(PerGameMcd_GetRegionName(normalized_serial));
	std::string extension(extension_setting.empty() ? std::string_view(".bin") : extension_setting);
	if (extension.front() != '.')
		extension.insert(extension.begin(), '.');

	std::string name(serial);
	if (!title.empty())
		name += fmt::format(" {}", title);
	if (!region.empty())
		name += fmt::format(" ({})", region);
	name = Path::SanitizeFileName(name);

	// Leave room for the extension; some filesystems get unhappy past 255 bytes.
	static constexpr size_t MAX_NAME_LENGTH = 200;
	if (extension.size() < MAX_NAME_LENGTH && name.size() > (MAX_NAME_LENGTH - extension.size()))
	{
		name.erase(MAX_NAME_LENGTH - extension.size());

		// GameDB titles are not ASCII, so don't leave half a UTF-8 sequence (or a trailing space) behind.
		while (!name.empty() && (static_cast<u8>(name.back()) & 0xC0) == 0x80)
			name.pop_back();
		if (!name.empty() && (static_cast<u8>(name.back()) & 0x80) != 0)
			name.pop_back();
		while (!name.empty() && name.back() == ' ')
			name.pop_back();
	}
	name += extension;

	const std::string full_path(Path::Combine(EmuFolders::MemoryCards, name));
	if (FileSystem::GetPathFileSize(full_path.c_str()) <= 0 && !PerGameMcd_CreateFromTemplate(full_path, template_card))
	{
		if (!FileMcd_CreateBlankCard(full_path, 8, PerGameMcd_UsesRawLayout(name)))
		{
			Host::ReportErrorAsync(TRANSLATE_SV("MemoryCard", "Memory Card Creation Failed"),
				fmt::format(TRANSLATE_FS("MemoryCard", "Could not create the memory card:\n{}"), full_path));
			return {};
		}

		Host::AddIconOSDMessage("PerGameMemoryCard", ICON_PF_MEMORY_CARD,
			fmt::format(TRANSLATE_FS("MemoryCard", "Created memory card for this game:\n{}"), name),
			Host::OSD_INFO_DURATION);
	}

	return name;
}
