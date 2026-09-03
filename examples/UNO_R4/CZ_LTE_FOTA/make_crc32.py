# -*- coding: utf-8 -*-
import os
import zlib


def generate_fota_metadata(bin_filename="fw.bin"):
    if not os.path.exists(bin_filename):
        print("[Error] " + bin_filename + " file not found.")
        return

    # 1. 파일 크기(Bytes) 구하기
    file_size = os.path.getsize(bin_filename)

    # 2. CRC32 계산 (Polynomial 0xEDB88320)
    with open(bin_filename, "rb") as f:
        data = f.read()
        crc32_val = zlib.crc32(data) & 0xFFFFFFFF

    # 3. fw.bin.crc32 파일 작성
    crc_filename = bin_filename + ".crc32"
    with open(crc_filename, "w") as f:
        f.write("0x%08X\n" % crc32_val)  # Line 1: 16진수 CRC32
        f.write("%d\n" % file_size)  # Line 2: 10진수 바이너리 크기

    print("=== " + crc_filename + " Created Successfully ===")
    print(" - Target File  : " + bin_filename)
    print(" - Firmware CRC : 0x%08X" % crc32_val)
    print(" - Firmware Size: %d Bytes" % file_size)


if __name__ == "__main__":
    generate_fota_metadata("fw.bin")