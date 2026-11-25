/*
 *  1541fs.h - 1541 emulation in host file system (FatFs RP2350)
 *
 *  Frodo Copyright (C) Christian Bauer
 */

#ifndef C1541FS_H
#define C1541FS_H

#include "IEC.h"
#include <string>

#ifdef FRODO_RP2350
#include "fatfs/ff.h"
#include <vector>
#else
#include <filesystem>
#endif

class FSDrive : public Drive {
public:
	FSDrive(IEC *iec, const std::string & path);
	virtual ~FSDrive();

	uint8_t Open(int channel, const uint8_t *name, int name_len) override;
	uint8_t Close(int channel) override;
	uint8_t Read(int channel, uint8_t &byte) override;
	uint8_t Write(int channel, uint8_t byte, bool eoi) override;
	void Reset() override;

private:
	bool change_dir(const std::string & path);

	uint8_t open_file(int channel, const uint8_t *name, int name_len);
	uint8_t open_directory(int channel, const uint8_t *pattern, int pattern_len);
	void close_all_channels();

	void initialize_cmd() override;
	void validate_cmd() override;

	std::string dir_path;
	uint8_t dir_title[16];

#ifdef FRODO_RP2350
	FIL     fil[16];
	bool    fil_open[16];
	bool    fil_writing[16];
	bool    has_read_char[16];
	uint8_t read_char[16];
	std::vector<uint8_t> dir_buf[16];
	size_t               dir_pos[16];
#else
	FILE *file[16];
	uint8_t read_char[16];
#endif
};

#endif // ndef C1541FS_H
