#include "stdafx.h"
#include "WriteNAND.h"
#include "Flash.h"
#include "Logger.h"
#include "Display.h"
#include "Touch.h"
#include "Checksum.h"

#ifndef BACKUP_HASH_ALGO
#define BACKUP_HASH_ALGO HASH_SHA512
#endif

const char* flashLabels[] = {"ALL+EEPROM", "ALL OS", "START", "CANCEL"};

bool flashBlocks[4] = {false, false, false, false};

static DWORD bufferSize = 0x20000;


void RenderFlashMenuOptions() {
    int y = 15;
    for (int i = 0; i < 4; i++) {
        if (RenderButtonWithState(780, y, 150, 36, flashLabels[i], flashBlocks[i]) != 0) {
            break;
        }
        y += 36 + 10; // Move down for next button
    }
}

int GetPressedFlashButton(int x, int y, int x_right, int y_top, int w, int h, int gap, int num_buttons) {
    int button_x = x_right - w; // Top-left x (e.g., 800 - 150 = 650)
    int button_y = y_top;

    for (int i = 0; i < num_buttons; i++) {
        // Check if (x, y) is within button bounds
        if (x >= button_x && x < button_x + w &&
            y >= button_y && y < button_y + h) {
            return i; // Return button index (0 to 4)
        }
        button_y += h + gap; // Move to next button
    }
    return -1; // No button pressed
}

DWORD DataSum(const BYTE *data, UINT nLen) 
{
   DWORD sum = 0;
   for ( UINT i=0; i<nLen ;i++ ) {
      sum += data[i];
   }
   return sum;
}

void RunWriteNAND() {
	ResetTextRenderer();
	DrawBackground(0x0010);
	AdjustBoundaries(620);
	WaitForScreenUntouch();
	Sleep(50);
	WaitForScreenUntouch();
	Sleep(50);
	PrintToScreen(2, "W r i t e   N A N D");
	PrintToScreen(1, "\n\nWrite backup file to NAND.\n");
	PrintToScreen(1, "!NOTE! Be careful, bad flash may render the device inoperable\n\n"); 
	memset(flashBlocks, 0, sizeof(flashBlocks));
	RenderFlashMenuOptions();
	Sleep(50);
	BYTE serial[4] = {0};
	BYTE productId[4] = {0};
	LogError(L"GetProdSection!", 0);
	int prodResult = GetProdSection(NULL, (BYTE*)productId, NULL, (BYTE*)serial, NULL);
	if (prodResult != 0) {

		LogError(L"GetProdSection fail!", prodResult);
		PrintToScreen(1, "Could not read device section: %u \n", prodResult);
		} else {
			char fileName[260] = {0};
			sprintf(fileName, "\\SystemSD\\nand_%02X%02X%02X%02X_%02X%02X%02X%02X.bin", 
				((BYTE*)productId)[0], ((BYTE*)productId)[1], ((BYTE*)productId)[2], ((BYTE*)productId)[3],
				((BYTE*)serial)[0], ((BYTE*)serial)[1], ((BYTE*)serial)[2], ((BYTE*)serial)[3]);
			
			FILE* backupFile = fopen(fileName, "rb");
			bool isPartial = false;
			bool backupFileValid = false;
			if (backupFile == NULL) {
				// Try partial/partition files
				backupFile = fopen("\\SystemSD\\nk.bin", "rb");
				if (backupFile) {
					isPartial = true;
					PrintToScreen(1, "Using partial flash: nk.bin\n");
				}
			}

			long fileSize = 0;
			if (backupFile != NULL) {
				fseek(backupFile, 0, SEEK_END);
				fileSize = ftell(backupFile);
				fseek(backupFile, 0, SEEK_SET); // Reset to start
				backupFileValid = (fileSize > 8192);
			}

			if (!backupFileValid) {
				PrintToScreen(1, "No NAND Backup or nk.bin found! Cannot continue.\n");
			for (int i = 0; i < 5; i++) {
				PrintToScreen(1, "\rExiting in.. %u", 5-i);
				Sleep(1000);
			}
		} else {
			PrintToScreen(1, "Valid NAND backup found! Size: %u bytes\n", fileSize);

			// Verify the image before flashing so we never write a corrupt
			// payload onto the device.
			if (isPartial) {
				// Partial flash uses an NK.bin (B000FF) kernel image: validate
				// its internal structure and per-record checksums.
				char nkErr[64] = {0};
				PrintToScreen(1, "Validating NK.bin structure...\n");
				if (ValidateNkBin("\\SystemSD\\nk.bin", nkErr, sizeof(nkErr))) {
					PrintToScreen(1, "NK.bin valid (B000FF). Records verified.\n");
				} else {
					PrintToScreen(1, "!INVALID NK.bin! (%s) Aborting.\n", nkErr);
					fclose(backupFile);
					for (int i = 0; i < 5; i++) {
						PrintToScreen(1, "\rExiting in.. %u", 5-i);
						Sleep(1000);
					}
					return;
				}
			} else {
				// Full backup: verify against its checksum sidecar (if present).
				int hashAlgo = BACKUP_HASH_ALGO;
				const char* algoName = (hashAlgo == HASH_SHA512) ? "SHA512" : "MD5";
				bool missing = false;
				PrintToScreen(1, "Verifying %s checksum...\n", algoName);
				if (VerifyChecksumSidecar(hashAlgo, fileName, &missing)) {
					PrintToScreen(1, "Checksum OK. Backup is intact.\n");
				} else if (missing) {
					PrintToScreen(1, "!WARNING! No checksum file found, integrity unverified.\n");
				} else {
					PrintToScreen(1, "!CHECKSUM MISMATCH! Backup is CORRUPT. Aborting.\n");
					fclose(backupFile);
					for (int i = 0; i < 5; i++) {
						PrintToScreen(1, "\rExiting in.. %u", 5-i);
						Sleep(1000);
					}
					return;
				}
			}

			flashBlocks[2] = true;

			bool startFlash = false;
			bool quitAction = false;
			while (!startFlash && !quitAction) {
				RenderFlashMenuOptions();
				WaitForScreenUntouch();
				int btn = -1;
				while (TRUE) {
					LCDTouchEvent* touchEvt = WaitForTouch(INFINITE);
					if (touchEvt != NULL) {
						if (btn == -1) {
							btn = GetPressedFlashButton(touchEvt->xCoord, touchEvt->yCoord, 780, 15, 150, 36, 10, 10);
							if (btn != -1 && NEW_NAV)
								break;
						}
					} else if (btn != -1 && !NEW_NAV) {
						// Once user has lifted their finger off / touch events have stopped, continue.
						break;
					}
				}

				switch (btn) {
					case 2:
						if (flashBlocks[btn]) {
							quitAction = false;
							startFlash = true;
						}
						break;
					case 3:
						startFlash = false;
						quitAction = true;
						break;
					default:
						if (btn == 0 || btn == 1) {
							if (!flashBlocks[btn]) {
								memset(flashBlocks, 0, sizeof(flashBlocks)-2);
								flashBlocks[btn] = true;
							} else {
								flashBlocks[btn] = false;
							}
							flashBlocks[btn] = true;
						} else {
							if (flashBlocks[0]) {
								flashBlocks[0] = false;
							}
							if (flashBlocks[1]) {
								flashBlocks[1] = false;
							}
							flashBlocks[btn] = !flashBlocks[btn];
						}
						bool flashEnabled = false;
						for (int i3 = 0; i3 < (sizeof(flashBlocks)-2); i3++) {
							if (flashBlocks[i3]) {
								flashEnabled = true;
								break;
							}
						}
						flashBlocks[2] = flashEnabled;
						break;
				}
			}

			if (startFlash) {

				flashBlocks[2] = false;
				flashBlocks[3] = false;

				RenderFlashMenuOptions();
				Sleep(400);

				if (!flashBlocks[0] && !flashBlocks[1]) {
					// TODO: implement specific blocks 
				} else if (flashBlocks[1]) {
					// Only flash starting from block 3
					PrintToScreen(1, "\nFlashing all OS data\n");

					FILE *backupFile = fopen(fileName, "rb");
					if (backupFile == NULL) {
						PrintToScreen(1, "Could not open backup file!");
						Sleep(5000);
						return;
					}

					// Seek to offset 0x20000*3 = 0x60000 (block 3)
					fseek(backupFile, bufferSize*3, SEEK_SET);

					HANDLE hDevice = CreateFileW(L"FMD1:",
                                 GENERIC_READ | GENERIC_WRITE,
                                 0,
                                 NULL,
                                 OPEN_EXISTING,
                                 (NEW_NAV ? 0x80 : 0),
                                 NULL);

					if (hDevice == INVALID_HANDLE_VALUE) {
						fclose(backupFile);
						LogError(L"Backup handle invalid!", 1);
						PrintToScreen(1, "Could not open flash device!");
						Sleep(5000);
						return;
					}

					BYTE* buffer = NULL;

					// Allocate buffers dynamically
					buffer = (BYTE*)malloc(bufferSize);
					if (buffer == NULL)
					{
						fclose(backupFile);
						PrintToScreen(1, "Failed to allocate buffer!");
						Sleep(5000);
						return;
					}


					ZeroMemory(buffer, 0x20000);

					DWORD* ioControlInput = (DWORD*)malloc(FLASH_CONTROL_IO_SIZE);
					if (ioControlInput == NULL)
					{
						PrintToScreen(1, "Failed to allocate ioControlInput!");
						free(buffer);
						fclose(backupFile);
						Sleep(5000);
						return;
					}
					ZeroMemory(ioControlInput, FLASH_CONTROL_IO_SIZE);

					int blockIndex = 3;
					DWORD byteCounter = 0;
					BOOL success = TRUE;

					DWORD maxSize = (0x1fd-3)*bufferSize;

					PrintToScreen(1, ">> 0000000 / %u", maxSize);

					while (blockIndex < 0x1fd)  // Loop from page 0x3 to 0x1ff
					{
						
						if (!WriteSingleBlockFromFile(hDevice, backupFile, buffer, ioControlInput, blockIndex, bufferSize)) {
							PrintToScreen(1, "\nFailed to write block %u! skip..", blockIndex);
							success = FALSE;
							blockIndex++;
							break;
						}

						byteCounter += bufferSize;

						PrintToScreen(1, "\r >> %u / %u", byteCounter, maxSize);
						DrawProgressBar(50, 400, 500, 30, byteCounter, maxSize, 0xF800); // Red for writing

						blockIndex++;
					}

					CloseHandle(hDevice);
					fclose(backupFile);
					free(buffer);

					if (!success) {
						PrintToScreen(1, "\nFailed to flash all OS blocks!\n");
					} else {
						PrintToScreen(1, "\nFlash complete!\n");
					}
				} else {
					PrintToScreen(1, "\nFlashing all NAND blocks + EEPROM\n");

					FILE *backupFile = fopen(fileName, "rb");
					if (backupFile == NULL) {
						PrintToScreen(1, "Could not open backup file!");
						Sleep(5000);
						return;
					}

					// Seek to the beginning
					fseek(backupFile, 0, SEEK_SET);

					HANDLE hDevice = CreateFileW(L"FMD1:",
                                 GENERIC_READ | GENERIC_WRITE,
                                 0,
                                 NULL,
                                 OPEN_EXISTING,
                                 (NEW_NAV ? 0x80 : 0),
                                 NULL);

					if (hDevice == INVALID_HANDLE_VALUE) {
						fclose(backupFile);
						LogError(L"Backup handle invalid!", 1);
						PrintToScreen(1, "Could not open flash device!");
						Sleep(5000);
						return;
					}

					BYTE* buffer = (BYTE*)malloc(bufferSize);

					if (buffer == NULL)
					{
						fclose(backupFile);
						PrintToScreen(1, "Failed to allocate buffer!");
						Sleep(5000);
						return;
					}


					ZeroMemory(buffer, bufferSize);

					DWORD* ioControlInput = (DWORD*)malloc(FLASH_CONTROL_IO_SIZE);
					if (ioControlInput == NULL)
					{
						PrintToScreen(1, "Failed to allocate ioControlInput!");
						free(buffer);
						fclose(backupFile);
						Sleep(5000);
						return;
					}
					ZeroMemory(ioControlInput, FLASH_CONTROL_IO_SIZE);

					int blockIndex = 0;
					DWORD byteCounter = 0;
					BOOL success = TRUE;

					DWORD maxSize = (0x1fd)*bufferSize;

					PrintToScreen(1, ">> 0000000 / %u", maxSize);


					while (blockIndex < 0x1fd)  // Loop from page 0 to 0x1ff
					{
						
						if (!WriteSingleBlockFromFile(hDevice, backupFile, buffer, ioControlInput, blockIndex, bufferSize)) {
							PrintToScreen(1, "\nFailed to write block %u! skip..", blockIndex);
							success = FALSE;
							blockIndex++;
							break;
						}

						byteCounter += bufferSize;

						PrintToScreen(1, "\r >> %u / %u", byteCounter, maxSize);
						DrawProgressBar(50, 400, 500, 30, byteCounter, maxSize, 0xF800); // Red for writing

						blockIndex++;
					}

					CloseHandle(hDevice);
					fclose(backupFile);
					free(buffer);

					if (!success) {
						PrintToScreen(1, "\nFailed to flash all NAND+EEPROM blocks!\n");
					} else {
						PrintToScreen(1, "\nFlash complete!\n");
					}
				}

				for (int i2 = 0; i2 < 5; i2++) {
					PrintToScreen(1, "\rExiting in.. %u", 5-i2);
					Sleep(1000);
				}
				WaitForScreenUntouch();
				return;
			}
			WaitForScreenUntouch();
			return;

		}
	}
	Sleep(4000);
	WaitForScreenUntouch();
}