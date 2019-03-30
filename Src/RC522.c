#include "stm32f4xx_hal.h"
#include "RC522.h"


uint8_t Sectorkey[PICC_SECTOR_KEY_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
Block_t	block;

uint8_t SpiSendByte(uint8_t txData) { //0b10001000 = 0x88
	uint8_t rxData = 0;
	HAL_SPI_TransmitReceive(&hspi1, &txData, &rxData, 1, 10);
	return rxData;
}

void MFRC522_WriteRegister(uint8_t addr, uint8_t val)
{
	MFRC522_NSS_RESET();

	SpiSendByte((addr << 1) & 0x7E);
	SpiSendByte(val);

	MFRC522_NSS_SET();

}

uint8_t MFRC522_ReadRegister(uint8_t addr)
{
	MFRC522_NSS_RESET();

	SpiSendByte(((addr << 1) & 0x7E) | 0x80);
	uint8_t val = SpiSendByte(0x00);

	MFRC522_NSS_SET();
	return val;
}

uint8_t MFRC522_GetUID(uint8_t* uid)
{
	// Find cards, return card type and Enter card to READY mode
	uint8_t status = MFRC522_RequestCardType(PICC_REQIDL, uid);

	// Card detected. Anti-collision, return card serial number 4 bytes
	if (MI_OK == status) status = MFRC522_AnticollGetUID(uid);

	return status;
}

uint8_t MFRC522_Compare(uint8_t* CardID, uint8_t* CompareID) {
	uint8_t i;
	for (i = 0; i < 5; i++) {
		if (CardID[i] != CompareID[i]) return MI_ERR;
	}
	return MI_OK;
}

void MFRC522_SetBitMask(uint8_t reg, uint8_t mask) {
	MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) | mask);
}

void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask){
	MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) & (~mask));
}

uint8_t MFRC522_RequestCardType(uint8_t reqMode, uint8_t* TagType)
{
	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);		// TxLastBits = BitFramingReg[2..0]
	TagType[0] = reqMode;

	uint16_t backBits; // The received data bits
	uint8_t status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

	//printf("\rMFRC522_Request::status before = %d\n\n", status);
	if ((status != MI_OK) || (backBits != 0x10)) status = MI_ERR; //MFRC522_MAX_LEN = 0x10

	return status;
}

uint8_t MFRC522_ToCard(	uint8_t 	command,
						uint8_t*	sendData,
						uint8_t 	sendLen,
						uint8_t*	backData,
						uint16_t*	backLen)
{
//	static int enterFunc = 0;
	//static int weGotRX = 0;
	uint8_t status = MI_ERR;
	uint8_t irqEn = 0x0;
	uint8_t waitIRq = 0x0;

//	printf("\rMFRC522_ToCard::Enter MFRC522_ToCard time = %d\n", ++enterFunc);
//	printf("\rMFRC522_ToCard::-------------------------------sendData[0] = 0x%02x\n", sendData[0]);
//	printf("\rMFRC522_ToCard::-------------------------------sendData[1] = 0x%02x\n", sendData[1]);

	switch (command) {
		case PCD_AUTHENT:
			irqEn = 0x12;
			waitIRq = 0x10;
			break;

		case PCD_TRANSCEIVE:
			irqEn 	= 	TxIRq 		|
						RxIRq 		|
						IdleIRq  	|
						/*HiAlertIRq 	|*/
						LoAlertIRq 	|
						ErrIRq 		|
						TimerIRq;

			waitIRq = 	RxIRq 	|
						IdleIRq |
						TimerIRq;
			break;

		default:
			break;
	}

	MFRC522_WriteRegister(MFRC522_REG_COMM_IE_N, irqEn | 0x80); // Enabled IRQ bits  0xF7 = 0b11110111
	MFRC522_ClearBitMask(MFRC522_REG_COMM_IRQ, 0x80); // Clear IRQ bits
	MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80); // Clear FIFO
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_IDLE); // Enter IDLE mode

	// Writing data to the FIFO
	for (uint16_t i = 0; i < sendLen; i++) {
		MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, sendData[i]);
	}

	// Execute the command
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);
	if (command == PCD_TRANSCEIVE) {
		MFRC522_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80); // START Sending to MIFARE_CARD
	}

	uint16_t i = 2000;
	uint8_t ComIrqRegValue;
	do {
		ComIrqRegValue = MFRC522_ReadRegister(MFRC522_REG_COMM_IRQ); // Check IRQ reg

		//if(ComIrqRegValue & RxIRq) printf("\rMFRC522_ToCard::WE GOT RX!!!! time = %d\n", ++weGotRX);

		i--;
	} while ((i != 0) && !(ComIrqRegValue & waitIRq)); //RxIRq | IdleIRq | TimerIRq;

	MFRC522_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);// 0111 1111 bit 7=0 -> StartSend=0 stop send

//	printf("\rMFRC522_ToCard::ComIrqRegValue \t= 0x%08x\n",ComIrqRegValue);
//	printf("\rMFRC522_ToCard::i \t= %d\n",i);

	if (i != 0)  {
		uint8_t ErrorRegValue = MFRC522_ReadRegister(MFRC522_REG_ERROR);

		if (!( ErrorRegValue & 0x1B)) {
			status = MI_OK;
			if (ComIrqRegValue & irqEn & 0x01) status = MI_NO_TAG_ERR; // if TIME_OUT == > no tag!!

//			switch(status){
//				case MI_OK:
//					printf("\rMFRC522_ToCard::(i != 0) status = MI_OK\n");
//					break;
//
//				case MI_NO_TAG_ERR:
//					printf("\rMFRC522_ToCard::(i != 0) status = MI_NO_TAG_ERR\n");
//					break;
//
//				case MI_ERR:
//					printf("\rMFRC522_ToCard::(i != 0) status = MI_ERR\n");
//					break;
//			}

			if (command == PCD_TRANSCEIVE) {
				uint8_t FIFOLevelRegValue = MFRC522_ReadRegister(MFRC522_REG_FIFO_LEVEL);

//				printf("\rMFRC522_ToCard::FIFOLevelRegValue = 0x%08x\n", (unsigned)FIFOLevelRegValue);

				uint8_t lastValidBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;

//				printf("\rMFRC522_ToCard::lastValidBits = 0x%08x\n", (unsigned)lastValidBits);

				if (lastValidBits)
					*backLen = (FIFOLevelRegValue-1) * NUMBER_OF_VALID_BITS + lastValidBits;
				else
					*backLen = FIFOLevelRegValue * NUMBER_OF_VALID_BITS;

				if (FIFOLevelRegValue == 0) FIFOLevelRegValue = 1;
				if (FIFOLevelRegValue > MFRC522_MAX_LEN) FIFOLevelRegValue = MFRC522_MAX_LEN;

				// Reading the received data in FIFO
				for (i = 0; i < FIFOLevelRegValue; i++) {
					backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFO_DATA);
//					printf("\rMFRC522_ToCard::backData[%d] = 0x%02x\n", i, (unsigned)backData[i] & 0xFF);
				}

			}

		} else {
			status = MI_ERR;
		}
	}

//	printf("\rMFRC522_ToCard::status On EXIT \n");
//	switch(status){
//		case MI_OK:
//			printf("\rMFRC522_ToCard::status = MI_OK\n");
//			break;
//
//		case MI_NO_TAG_ERR:
//			printf("\rMFRC522_ToCard::status = MI_NO_TAG_ERR\n");
//			break;
//
//		case MI_ERR:
//			printf("\rMFRC522_ToCard::status = MI_ERR\n");
//			break;
//
//	}

	return status;
}

uint8_t MFRC522_AnticollGetUID(uint8_t* serNum)
{
	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);				// TxLastBists = BitFramingReg[2..0]
	serNum[0] = PICC_ANTICOLL;
	serNum[1] = 0x20;
	//printf("\rMFRC522_Anticoll::serNum[0] (PICC_ANTICOLL) \t= 0x%02x\n", (unsigned)serNum[0] & 0xFF);
	//printf("\rMFRC522_Anticoll::serNum[1] (blockAddr) \t= 0x%02x\n", (unsigned)serNum[1] & 0xFF);

	uint16_t unLen;
	uint8_t status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);
	//printf("\rMFRC522_Anticoll::status \t= %d\n",(unsigned)status & 0xFF);

	printf("\rSerialNumber=%x%x\r\n", *(serNum+4) & 0xFF, (int)(*(int*)serNum));

	if (status == MI_OK) {
		// Check card serial number
		uint8_t i;
		uint8_t serNumCheck = 0;
		for (i = 0; i < 4; i++) {
			serNumCheck ^= serNum[i]; // 0b0000 ^ 0b0111 =0b0111
		}
		if (serNumCheck != serNum[i]) status = MI_ERR;
	}

	return status;
}

void MFRC522_CalculateCRC(uint8_t*  pIndata, uint8_t len, uint8_t* pOutData) {
	uint8_t i, n;

	MFRC522_ClearBitMask(MFRC522_REG_DIV_IRQ, 0x04);	// CRCIrq = 0
	MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);	// Clear the FIFO pointer
	//	MFRC522_Write(CommandReg, PCD_IDLE);

	// Writing data to the FIFO
	for (i = 0; i < len; i++) MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, *(pIndata+i));
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_CALCCRC);

	// Wait CRC calculation is complete
	i = 0xFF;
	do {
		n = MFRC522_ReadRegister(MFRC522_REG_DIV_IRQ);
		i--;
	} while ((i!=0) && !(n&0x04));																						// CRCIrq = 1

	// Read CRC calculation result
	pOutData[0] = MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_L);
	pOutData[1] = MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_M);
}

uint8_t MFRC522_SelectTag(uint8_t* serNum) {
	uint8_t i;
	uint8_t status;
	uint8_t size;
	uint16_t recvBits;
	uint8_t buffer[9];

	buffer[0] = PICC_SELECT_TAG;
	buffer[1] = 0x70;
	for (i = 0; i < 5; i++) buffer[i+2] = *(serNum+i);
	MFRC522_CalculateCRC(buffer, 7, &buffer[7]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);

//	for (i = 0; i < 9; i++)
//		printf("\rMFRC522_SelectTag::buffer[%d] = 0x%02x\n", i, buffer[i]);

	if ((status == MI_OK) && (recvBits == 0x18))
		size = buffer[0];
	else
		size = 0;
//	printf("\rMFRC522_SelectTag::UID size = %d, recvBits = 0x%02x\n", (unsigned)size, recvBits);
	return size;
}

uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t* Sectorkey, uint8_t* serNum) {
	uint8_t status;
	uint16_t recvBits;
	uint8_t i;
	uint8_t buff[12];

	// Verify the command block address + sector + password + card serial number
	buff[0] = authMode; //PICC_AUTHENT1A or PICC_AUTHENT1B
	buff[1] = BlockAddr; // 0x00 - 0x3F = 0 - 39
	for (i = 0; i < 6; i++) buff[i+2] = *(Sectorkey+i);
	for (i=0; i<4; i++) buff[i+8] = *(serNum+i);
	status = MFRC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);
	if ((status != MI_OK) || (!(MFRC522_ReadRegister(MFRC522_REG_STATUS2) & 0x08))) status = MI_ERR;
	return status;
}

uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t* recvData) {
	uint8_t status;
	uint16_t unLen;

	recvData[0] = PICC_READ;
	recvData[1] = blockAddr;
	MFRC522_CalculateCRC(recvData,2, &recvData[2]);

//	printf("\rBlock[0] (PICC_READ) \t= 0x%02x\n", (unsigned)recvData[0] & 0xFF);
//	printf("\rBlock[1] (blockAddr) \t= 0x%02x\n", (unsigned)recvData[1] & 0xFF);
//	printf("\rBlock[2] (CRC_HI)\t\t= 0x%02x\n", (unsigned)recvData[2] & 0xFF);
//	printf("\rBlock[3] (CRC_LO)\t\t= 0x%02x\n", (unsigned)recvData[3] & 0xFF);

	status = MFRC522_ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen);
	if ((status != MI_OK) || (unLen != 0x90)) status = MI_ERR;

//	printf("\rstatus \t= 0x%02x\n",status);
	return status;
}

uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t* writeData) {
	uint8_t status;
	uint16_t recvBits;
	uint8_t i;
	uint8_t buff[18];

	buff[0] = PICC_WRITE;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;

	if (status == MI_OK) {
		// Data to the FIFO write 16Byte
		for (i = 0; i < 16; i++) buff[i] = *(writeData+i);
		MFRC522_CalculateCRC(buff, 16, &buff[16]);
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);
		if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;
	}
	return status;
}

uint8_t MFRC522_Decrement(uint8_t blockAddr, uint8_t* writeData)
{
	uint8_t buff[6];
	buff[0] = PICC_DECREMENT;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);

	uint16_t recvBits;
	uint8_t status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	printf("\rPICC_DECREMENT\n");

	if 	((status != MI_OK)  ||
				(recvBits != 4) 	||
				((buff[0] & 0x0F) != 0x0A))
	{
		status = MI_ERR;
	}

	switch(status)
	{
		case MI_OK:
			printf("\rMFRC522_Decrement::status = MI_OK , buff[0] = 0x%02x\n",(unsigned)buff[0] & 0xFF);
			break;

		case MI_NO_TAG_ERR:
			printf("\rMFRC522_Decrement::status = MI_NO_TAG_ERR , buff[0] = 0x%02x\n",(unsigned)buff[0] & 0xFF);
			break;

		case MI_ERR:
			printf("\rMFRC522_Decrement::status = MI_ERR , buff[0] = 0x%02x\n",(unsigned)buff[0] & 0xFF);
			break;

	}
	if (status == MI_OK)
	{
		// Data to the FIFO write 4Byte
		*((unsigned*)buff) = *((unsigned*)writeData);
		MFRC522_CalculateCRC(buff, PICC_OPERAND_SIZE, &buff[PICC_OPERAND_SIZE]);
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, PICC_OPERAND_SIZE + PICC_CRC_SIZE, buff, &recvBits);

		if 	((status != MI_OK)  ||
			(recvBits != 4) 	||
			((buff[0] & 0x0F) != 0x0A))
		{
			status = MI_ERR;
		}

	}
	return status;
}

uint8_t MFRC522_Increment(uint8_t blockAddr, uint8_t* writeData)
{
	uint8_t buff[6];
	buff[0] = PICC_INCREMENT;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);

	uint16_t recvBits;
	uint8_t status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	printf("\rPICC_INCREMENT\n");
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;

	switch(status){
		case MI_OK:
			printf("\rMFRC522_Increment::status = MI_OK , buff[0] = 0x%02x\n",(unsigned)buff[0] & 0xFF);
			break;

		case MI_NO_TAG_ERR:
			printf("\rMFRC522_Increment::status = MI_NO_TAG_ERR , buff[0] = 0x%02x\n",(unsigned)buff[0] & 0xFF);
			break;

		case MI_ERR:
			printf("\rMFRC522_Increment::status = MI_ERR , buff[0] = 0x%02x\n",(unsigned)buff[0] & 0xFF);
			break;

	}
	if (status == MI_OK) {
		// Data to the FIFO write 4Byte
		*((unsigned*)buff) = *((unsigned*)writeData);
		MFRC522_CalculateCRC(buff, PICC_OPERAND_SIZE, &buff[PICC_OPERAND_SIZE]);
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, PICC_OPERAND_SIZE + PICC_CRC_SIZE, buff, &recvBits);

		if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;
	}
	return status;
}

uint8_t MFRC522_Transfer(uint8_t blockAddr) {
	uint8_t status;
	uint16_t recvBits;
	uint8_t buff[4];

	buff[0] = PICC_TRANSFER;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;

	return status;
}
uint8_t MFRC522_Restore(uint8_t blockAddr) {
	uint8_t status;
	uint16_t recvBits;
	uint8_t buff[4];

	buff[0] = PICC_RESTORE;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;

	return status;
}

void MFRC522_Init(void) {
	MFRC522_Reset();
	MFRC522_WriteRegister(MFRC522_REG_T_MODE		, 0x0D);
	MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER	, 0x3E);
	MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_L	, 30);
	MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_H	, 5);
	MFRC522_WriteRegister(MFRC522_REG_RF_CFG		, 0x70);	// 48dB gain
	MFRC522_WriteRegister(MFRC522_REG_TX_AUTO		, 0x40);
	MFRC522_WriteRegister(MFRC522_REG_MODE			, 0x3D);
	MFRC522_AntennaOn();							// Open the antenna
}

void MFRC522_Reset(void) {
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_RESETPHASE);
}

void MFRC522_AntennaOn(void) {
	uint8_t temp;
	temp = MFRC522_ReadRegister(MFRC522_REG_TX_CONTROL);
	if (!(temp & 0x03)) MFRC522_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

void MFRC522_AntennaOff(void) {
	MFRC522_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

void MFRC522_Halt(void) {
	uint16_t unLen;
	uint8_t buff[4];

	buff[0] = PICC_HALT;
	buff[1] = 0;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &unLen);
}




/************ Private Utility functions *********************************/

void printDataBlock (void)
{
	for (int i = 0 ; i < MFRC522_MAX_LEN ; i++)
	{
		printf("\rblock.data[%d] \t= 0x%02x\n", i, (unsigned)block.data[i] & 0xFF);
	}
}
void printDataBlockNoNewLine (int startBlock)
{
	for (int i = startBlock ; i < MFRC522_MAX_LEN ; i++)
	{
		printf("%02x",(unsigned)block.data[i] & 0xFF);
	}
	printf("\r\n");
}

BlockAccessConditions_t getAccessBits (void)
{
	BlockAccessConditions_t blockAccessCond;

	blockAccessCond.block0.bit.c1 =	!block.sectorTrailer.accessBytes[0].Byte6.c1b0;
	blockAccessCond.block0.bit.c2 =  block.sectorTrailer.accessBytes[2].Byte8.c2b0;
	blockAccessCond.block0.bit.c3 = !block.sectorTrailer.accessBytes[1].Byte7.c3b0;

	blockAccessCond.block1.bit.c1 =	!block.sectorTrailer.accessBytes[0].Byte6.c1b1;
	blockAccessCond.block1.bit.c2 =	 block.sectorTrailer.accessBytes[2].Byte8.c2b1;
	blockAccessCond.block1.bit.c3 =	!block.sectorTrailer.accessBytes[1].Byte7.c3b1;

	blockAccessCond.block2.bit.c1 =	!block.sectorTrailer.accessBytes[0].Byte6.c1b2;
	blockAccessCond.block2.bit.c2 =	 block.sectorTrailer.accessBytes[2].Byte8.c2b2;
	blockAccessCond.block2.bit.c3 =	!block.sectorTrailer.accessBytes[1].Byte7.c3b2;

	blockAccessCond.block3.bit.c1 =	!block.sectorTrailer.accessBytes[0].Byte6.c1b3;
	blockAccessCond.block3.bit.c2 =	 block.sectorTrailer.accessBytes[2].Byte8.c2b3;
	blockAccessCond.block3.bit.c3 =	!block.sectorTrailer.accessBytes[1].Byte7.c3b3;

	return blockAccessCond;
}

void printSectorTrailerBlock(void)
{
	unsigned i;
	printf("\rKey_A \t\t= 0x ");
	fflush(stdout);
	for (i = 0 ; i < PICC_SECTOR_KEY_SIZE ; i++) {
		printf("%02x ", (unsigned)block.sectorTrailer.keyA[i] & 0xFF);
	}
	printf("\n");

	printf("\rAccess_Bits \t= 0x ");
	for (i = 0 ; i <  PICC_ACCESS_BITS_LEN ; i++) {
		printf("%02x ", (unsigned)(block.sectorTrailer.accessBytes[PICC_ACCESS_BITS_LEN - i - 1].data) & 0xFF);
	}
	printf("\n");

	printf("\rKey_B \t\t= 0x ");
	fflush(stdout);
	for (i = 0 ; i < PICC_SECTOR_KEY_SIZE ; i++) {
		printf("%02x ", (unsigned)block.sectorTrailer.keyB[i] & 0xFF);
	}
	printf("\n");

	BlockAccessConditions_t accessBits = getAccessBits();
	printf("\rAccessBitsBlock0 = 0x%01x \n", (unsigned)accessBits.block0.data & 0xF);
	printf("\rAccessBitsBlock1 = 0x%01x \n", (unsigned)accessBits.block1.data & 0xF);
	printf("\rAccessBitsBlock2 = 0x%01x \n", (unsigned)accessBits.block2.data & 0xF);
	printf("\rAccessBitsBlock3 = 0x%01x \n", (unsigned)accessBits.block3.data & 0xF);
}



void MFRC522_ReadVersionRFID ()
{
	// Expected 0x91 or 0x92
	int32_t version = MFRC522_ReadRegister(MFRC522_REG_VERSION);
	printf("\r0x%02x\r\n",(unsigned)version);
}

void MFRC522_ReadCardType ()
{
	while (true)
	{
		MFRC522_Init();
		if (!MFRC522_RequestCardType(PICC_REQIDL, block.data)) {
			printf("\r0x%02x\r\n",(unsigned)block.data[0] & 0xFF);
			//printDataBlock();
			break;
		}
	}
}


void MFRC522_ReadUIDCard ()
{
	while (true)
	{
		MFRC522_Init();
		if(!MFRC522_GetUID(block.data)) {
			//printDataBlock();
			break;
		}
	}
}
void MFRC522_ReadCardManufData ()
{
	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into block
		if(!MFRC522_GetUID(block.data)) {
			if ( PICC_SAK == MFRC522_SelectTag(block.data)){
				uint8_t BlockAddr = 0x0;
				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data)){
					MFRC522_Read(BlockAddr, block.data);
					printf("\rManufactureId=");
					printDataBlockNoNewLine(5);//5 start of manufacture block
					//printDataBlock();
				}
			}
			break;
		}
	}
}


void MFRC522_ReadCardDataBlock ()
{
	uint8_t BlockAddr = 0x0;
	printf("\r Please enter block of choice to read (0 - 63): ");
	fflush(stdout);
	scanf("%d",(int*)(&BlockAddr));

	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					MFRC522_Read(BlockAddr, block.data);
					//printDataBlock();
					printf("\rDataBlock=%s\r\n",block.data);
				}
			}

			break;
		}
	}
}

char* MFRC522_GetDataBlock (uint8_t BlockAddr)
{
	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					MFRC522_Read(BlockAddr, block.data);
				}
			}

			break;
		}
	}
	return (char*)(block.data);
}

int32_t MFRC522_GetValueBlock (uint8_t BlockAddr)
{
	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					MFRC522_Read(BlockAddr, block.data);
					if( 	/*(~block.value.operand == block.value.notOperand) &&*/
							(block.value.operand  == block.value.operandCopy) &&
							/*(~block.value.address == block.value.notAddress) &&*/
							(block.value.address  == block.value.addressCopy) /* &&
							 (~block.value.address == block.value.notAddressCopy)*/)
					{
						//printDataBlock();
					} else {
						printf("\rThis is NOT a value block!! :)\n");
					}

				}
			}

			break;
		}
	}
	return block.value.operand;
}
void MFRC522_ReadCardValueBlock ()
{
	uint8_t BlockAddr = 0x0;
	printf("\r Please enter block of choice to read (0 - 63): ");
	fflush(stdout);
	scanf("%d",(int*)(&BlockAddr));

	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					MFRC522_Read(BlockAddr, block.data);
					if( 	/*(~block.value.operand == block.value.notOperand) &&*/
							(block.value.operand  == block.value.operandCopy) &&
							/*(~block.value.address == block.value.notAddress) &&*/
							(block.value.address  == block.value.addressCopy) /* &&
							 (~block.value.address == block.value.notAddressCopy)*/)
					{
						printDataBlock();
						printf("\rValueBlock: %d\n",(int)block.value.operand);
					} else {
						printf("\rThis is NOT a value block!! :)\n");
					}

				}
			}

			break;
		}
	}
}

void readSectorTrailer(uint8_t userBlockAddr)
{

	// BlockAddr += 4 ---- 3,7,11,15
	uint8_t sectorTraile = 3 + (userBlockAddr / 4) * 4;
	printf("\rblockAddrSectorTraile = %d \n",(unsigned)sectorTraile );

	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into Block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{
				if(!MFRC522_Auth(PICC_AUTHENT1A, sectorTraile, Sectorkey, block.data))
				{
					if (MI_OK == MFRC522_Read(sectorTraile, block.data)) break;
				}
			}
		}

	}
}
void MFRC522_ReadSectorTrailerCardBlock ()
{
	uint8_t userBlockAddr = 0;

	do{
		printf("\r Please enter block of choice to get SectorTrailer (0 - 63): ");
		fflush(stdout);
		scanf("%d",(int*)(&userBlockAddr));
	}while(userBlockAddr < 0 || userBlockAddr > 63);

	readSectorTrailer(userBlockAddr);
	printf("\n");
	printf("\rSector Trailer of sector: %d\n", (unsigned)userBlockAddr / 4);
	printSectorTrailerBlock();
}


void MFRC522_WriteValueBlock ()
{
	uint8_t BlockAddr = 0x0;

	do{
		printf("\r Please enter block number (0 - 63): ");
		fflush(stdout);
		scanf("%d",(int*)(&BlockAddr));
	}while(BlockAddr < 0 || BlockAddr > 63 || BlockAddr == 0 || BlockAddr%4 == 3);

	printf("\r Please enter block value (operand): ");
	fflush(stdout);
	Block_t userBlock = {0};
	scanf("%d",(int*)(&userBlock.value.operand));
	userBlock.value.notOperand = ~userBlock.value.operand;
	userBlock.value.operandCopy = userBlock.value.operand;
	userBlock.value.address = BlockAddr;
	userBlock.value.notAddress = ~userBlock.value.address;
	userBlock.value.addressCopy = userBlock.value.address;
	userBlock.value.notAddressCopy = ~userBlock.value.address;

	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into Block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{
				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					if (MI_OK == MFRC522_Write(BlockAddr, userBlock.data)) break;
				}
			}

		}
	}
}
void MFRC522_WriteDataBlock ()
{
	uint8_t BlockAddr = 0x0;
	uint8_t	userData[MFRC522_MAX_LEN];
	do{
		printf("\r Please enter block number (0 - 63): ");
		fflush(stdout);
		scanf("%d",(int*)(&BlockAddr));
	}while(BlockAddr < 0 || BlockAddr > 63 || BlockAddr == 0 || BlockAddr%4 == 3);

	do{
		printf("\r Please enter block data (16 bytes ONLY!): ");
		fflush(stdout);
		scanf("%s",userData);
	}while(strlen((char*)(userData)) >= MFRC522_MAX_LEN - 1);

	userData[MFRC522_MAX_LEN-1] ='\0';

	while (true)
	{
		MFRC522_Init();

		// Get SerialNum into Block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{
				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					if (MI_OK == MFRC522_Write(BlockAddr, userData)) break;
				}
			}

		}
	}
}
void MFRC522_DecrementCardBlock ()
{
	uint8_t BlockAddr = 0x0;
	printf("\rPlease enter block of choice to increment (0x00 - 0xFF): ");
	fflush(stdout);
	scanf("%d",(int*)(&BlockAddr));

	readSectorTrailer(BlockAddr);

	while (true) {
		MFRC522_Init();

		// Get SerialNum into Block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					block.value.operand = 0x2;
					MFRC522_Increment(BlockAddr, block.data);
					MFRC522_Transfer(BlockAddr);
				}
			}
			break;
		}
	}
}
void MFRC522_IncrementCardBlock ()
{
	uint8_t BlockAddr = 0x0;
	printf("\rPlease enter block of choice to increment (0 - 63): ");
	fflush(stdout);
	scanf("%d",(int*)(&BlockAddr));

	readSectorTrailer(BlockAddr);

	while (true) {
		MFRC522_Init();

		// Get SerialNum into Block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					block.value.operand = 0x2;
					MFRC522_Increment(BlockAddr, block.data);
					MFRC522_Transfer(BlockAddr);
				}
			}
			break;
		}
	}
}

void MFRC522_TransferCardBlock ()
{
	uint8_t BlockAddr = 0x0;
	printf("\rPlease enter block of choice to increment (0x00 - 0xFF): ");
	fflush(stdout);
	scanf("%d",(int*)(&BlockAddr));

	readSectorTrailer(BlockAddr);

	while (true) {
		MFRC522_Init();

		// Get SerialNum into Block
		if(!MFRC522_GetUID(block.data))
		{
			if ( PICC_SAK == MFRC522_SelectTag(block.data))
			{

				if(!MFRC522_Auth(PICC_AUTHENT1A, BlockAddr, Sectorkey, block.data))
				{
					MFRC522_Transfer(BlockAddr);
				}
			}
			break;
		}
	}
}


