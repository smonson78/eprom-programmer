int EPROM_CE = 22;
int EPROM_OE = 2;
int EPROM_A0 = 23;
int EPROM_VPP = 42;
int EPROM_VCC = 44;

// current data bus direction
typedef enum { DATA_IN, DATA_OUT } data_bus_direction_t;
data_bus_direction_t data_bus_dir;

/* Current command */
#define CMDLEN 16
char cmdbuf[CMDLEN + 1];

#define DATALEN 512
uint16_t databuf[DATALEN];
uint16_t dataused;

/* Pointer to next operand */
uint8_t cmdptr;


void setup() {
  pinMode(EPROM_OE, OUTPUT);
  pinMode(EPROM_CE, OUTPUT);
  pinMode(EPROM_VCC, OUTPUT);
  pinMode(EPROM_VPP, OUTPUT);

  // Address
  for (int pin = EPROM_A0; pin <= EPROM_A0 + 17; pin++) {
    pinMode(pin, OUTPUT);
  }

  data_bus_direction(DATA_IN);

  digitalWrite(EPROM_OE, HIGH);
  digitalWrite(EPROM_CE, HIGH);
  digitalWrite(EPROM_VCC, LOW);
  digitalWrite(EPROM_VPP, LOW);

  // Allow time for voltages to settle
  delay(100);
  Serial.begin(19200);

  dataused = 0;
  
}

void data_bus_direction(data_bus_direction_t dir) {
  // Already set the right way - do nothing
  if (data_bus_dir == dir) {
    return;
  }
  
  int pinmode;
  if (dir == DATA_IN) {
    pinmode = INPUT;
  } else {
    pinmode = OUTPUT;
  }
  data_bus_dir = dir;
  
  for (int pin = 3; pin <= 13; pin++) {
    pinMode(pin, pinmode);
  }
  for (int pin = 49; pin <= 53; pin++) {
    pinMode(pin, pinmode);
  }
}

void set_address(int addr) {
  for (int bit = 0; bit <= 17; bit++) {
    digitalWrite(EPROM_A0 + bit, (addr >> bit) & 1);
  }
}

unsigned int data_bus_value() {
  int acc = 0;

  acc |= (PINE >> 5) & 1; // bit 0
  acc |= (PING >> 4) & (1 << 1); // bit 1
  acc |= (PINE >> 1) & (1 << 2); // bit 2
  acc |= PINH & ((1 << 3) | (1 << 4) | (1 << 5) | (1 << 6)); // bit 3, 4, 5, 6
  acc |= (PINB & ((1 << 4) | (1 << 5) | (1 << 6) | (1 << 7))) << 3; // bit 7, 8, 9, 10
  acc |= (PINL & 1) << 11; // bit 11
  acc |= (PINB & (1 << 3)) << 9; // bit 12
  acc |= (PINB & (1 << 2)) << 11; // bit 13
  acc |= (PINB & (1 << 1)) << 13; // bit 14
  acc |= (PINB & 1) << 15; // bit 15
  
  return acc;
}

void data_bus_write(unsigned int val) {
  PORTE &= ~(_BV(5) | _BV(3)); // 0, 2
  PORTE |= ((val & 1) << 5) | ((val & (1 << 2)) << 1); // bit 0 = PE5; bit 2 = PE3

  PORTG &= ~_BV(5); // 1
  PORTG |= (val & (1 << 1)) << 4; // bit 1 = PG5

  PORTH &= ~(_BV(3) | _BV(4) | _BV(5) | _BV(6)); // 3, 4, 5, 6
  PORTH |= val & (_BV(3) | _BV(4) | _BV(5) | _BV(6)); // PH3-6

  PORTB &= 0xff; // all bits in port B are used in d15, 14, 13, 12, 7, 8, 9, 10
  PORTB |= ((val & (1 << 7)) >> 3) | ((val & (1 << 8)) >> 3) | ((val & (1 << 9)) >> 3) | ((val & (1 << 10)) >> 3)
    | ((val & (1 << 12)) >> 9) | ((val & (1 << 13)) >> 11) | ((val & (1 << 14)) >> 13) | ((val & (1 << 15)) >> 15);
  
  PORTL &= ~_BV(0); // 11
  PORTL |= (val & (1 << 11)) >> 11; // PL0
}

/* Be cafeul not to call this without setting the data bus direction first. */
uint16_t read_eprom(uint32_t addr) {
    set_address(addr);
    delay(1);
    digitalWrite(EPROM_CE, LOW);
    delay(1);
    digitalWrite(EPROM_OE, LOW);
    delay(1);
    uint16_t d = data_bus_value();
  
    digitalWrite(EPROM_CE, HIGH);
    digitalWrite(EPROM_OE, HIGH);
    return d;
}

uint32_t getCmdAddr(uint8_t pos) {
  while (cmdbuf[pos] == ' ') {
    pos++;
  }
  uint32_t addr = 0;
  while (cmdbuf[pos] != ' ' && cmdbuf[pos] != '\n' && cmdbuf[pos] != '\0') {
    if (cmdbuf[pos] >= '0' && cmdbuf[pos] <= '9') {
      addr *= 10;
      addr += cmdbuf[pos++] - '0';
    } else {
      break;
    }
  }
  cmdptr = pos;
  return addr;
}

void doReadCmd() {
  uint32_t start = getCmdAddr(cmdptr);
  uint32_t len = getCmdAddr(cmdptr);
  if (len == 0) {
    len = 16;
  }
  Serial.print("# READ ");
  Serial.print(start, DEC);
  Serial.print(", ");
  Serial.println(len, DEC);  
  
  data_bus_direction(DATA_IN);
  
  for (uint32_t addr = start; addr < start + len; addr++) {
    uint16_t val = read_eprom(addr);

    Serial.print(addr, HEX);
    Serial.print(": ");
    Serial.println(val, HEX);
  }    
}

void doWriteCmd() {
  uint32_t addr = getCmdAddr(cmdptr);
  uint32_t value = getCmdAddr(cmdptr);
  Serial.print("# WRITE ");
  Serial.print(addr, DEC);
  Serial.print(", ");
  Serial.println(value, HEX);  
  
  data_bus_direction(DATA_OUT);
  digitalWrite(EPROM_VCC, HIGH);
  digitalWrite(EPROM_VPP, HIGH);
  delay(10);
  set_address(addr);
  data_bus_write(value);

  // Wait for tAS, tDS, tVPS, tVCS (uS)
  _delay_us(10);

  // Pulse CE for exactly 50uS
  digitalWrite(EPROM_CE, LOW);
  _delay_us(50);
  digitalWrite(EPROM_CE, HIGH);

  // Wait for tDH (data hold time)
  _delay_us(2);

  // Can now lower programming voltages and let them settle
  digitalWrite(EPROM_VPP, LOW);
  digitalWrite(EPROM_VCC, LOW);
  delay(100); 
}

uint8_t getDataBuffer(uint16_t size) {
  dataused = 0;
  uint16_t bytecount = 0;
  uint16_t lastbyte;
  uint16_t timeout = 0;
  while (1) {
    if (Serial.available()) {
      timeout = 0;
      int c = Serial.read();

      if (bytecount++ % 2 == 0) {
        lastbyte = c;
      } else {
        databuf[dataused++] = (lastbyte << 8) | c;
      }
      
      if (dataused == DATALEN || dataused == size) {
        return 0;
      }
    } else {
      delay(10);
      if (timeout++ == 1000) {
        dataused = 0;
        return 1;
      }
    }
  }  
}

void doBufferCmd() {
  uint16_t size = getCmdAddr(cmdptr);
  if (size > DATALEN) {
    size = DATALEN;
  }
  Serial.print("# BUFFER ");
  Serial.println(size, DEC);
  Serial.println("READY");
  uint8_t result = getDataBuffer(size);
  if (result == 0) {
    Serial.print("OK ");
    Serial.println(dataused, DEC);
  } else {
    Serial.println("ERROR");
  }
}

void doProgramCmd() {
  uint32_t addr = getCmdAddr(cmdptr);
  uint32_t size = getCmdAddr(cmdptr);
  Serial.print("# PROGRAM ");
  Serial.print(addr, DEC);
  Serial.print(", ");
  Serial.println(size, DEC);

  if (size == 0) {
    return;
  }

  data_bus_direction(DATA_OUT);
  digitalWrite(EPROM_VCC, HIGH);
  digitalWrite(EPROM_VPP, HIGH);
  delay(10);

  for (uint32_t count = 0; count < size; count++) {
    set_address(addr + count);
    data_bus_write(databuf[count]);
  
    // Wait for tAS, tDS, tVPS, tVCS (uS)
    _delay_us(10);
  
    // Pulse CE for exactly 50uS
    digitalWrite(EPROM_CE, LOW);
    _delay_us(50);
    digitalWrite(EPROM_CE, HIGH);
  
    // Wait for tDH (data hold time)
    _delay_us(2);
  }

  // Can now lower programming voltages and let them settle
  digitalWrite(EPROM_VPP, LOW);
  digitalWrite(EPROM_VCC, LOW);
  delay(100); 
}

// This algorithm doesn't seem to match anything online, but I couldn't figure out what the difference is.
#define CRC_POLY 0x3D65
uint16_t crc16(uint16_t crc, uint8_t b) 
{
  crc ^= ((uint16_t)b) << 8;
  for (uint8_t i = 0; i < 8; i++) {
    if ((crc & 0x8000) != 0) {
      crc = (crc << 1) ^ CRC_POLY;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

void doCRCCmd() {
  uint32_t start = getCmdAddr(cmdptr);
  uint32_t size = getCmdAddr(cmdptr);

  data_bus_direction(DATA_IN);
  uint16_t crc = 0;
  for (uint32_t addr = start; addr < start + size; addr++) {
    uint16_t operand = read_eprom(addr);
    crc = crc16(crc, operand >> 8);
    crc = crc16(crc, operand & 0xff);
  }
  
  Serial.print("# CRC ");
  Serial.print(start);
  Serial.print(" ");
  Serial.print(size);
  Serial.print(" ");
  Serial.println(crc, HEX);
}

void doShowCmd() {
  Serial.print("# SHOW ");
  Serial.println(dataused, DEC);  
  
  for (uint32_t addr = 0; addr < dataused; addr++) {
    Serial.print(addr, HEX);
    Serial.print(": ");
    Serial.println(databuf[addr], HEX);
  } 
}

void getCommand() {
  uint8_t pos = 0;
  while (1) {
    if (Serial.available()) {
      int c = Serial.read();
      if (c == '\n') {
        break;
      }
      if (pos < CMDLEN) {
        cmdbuf[pos++] = c;
      }
    }
  }
  cmdbuf[pos] = '\0';
  cmdptr = 0;
}

void doCommand() {
  switch (cmdbuf[0]) {
    case '\0': break;
    case 'r':
      // Read words
      cmdptr++;
      doReadCmd();
      break;
    case 'w':
      // Write a word
      cmdptr++;
      doWriteCmd();
      break;
    case 'b':
      // Write block of data into buffer
      cmdptr++;
      doBufferCmd();
      break;
    case 'p':
      // Program from buffer
      cmdptr++;
      doProgramCmd();
      break;
    case 'c':
      cmdptr++;
      doCRCCmd();
      break;
    case 's':
      cmdptr++;
      doShowCmd();
      break;
    default:
      Serial.print("Command ");
      Serial.print(cmdbuf[0]);
      Serial.println(" not understood.");
  }
}

void loop() {
  
  Serial.println("?");
  getCommand();
  doCommand();
  

  // Read contents of ROM at address 
/*
  Serial.println("Begin");
  long int addr_max = (long int)256 * 1024;

  for (long int addr = 0; addr < addr_max; addr++) {
    set_address(addr);
    delay(1);
    digitalWrite(EPROM_CE, LOW);
    delay(1);
    digitalWrite(EPROM_OE, LOW);
    delay(1);
    unsigned int d = data_bus_value();
  
    digitalWrite(EPROM_CE, HIGH);
    digitalWrite(EPROM_OE, HIGH);

    if (d != 0xffff) {
      Serial.print(addr, HEX);
      Serial.print(": ");
      Serial.println(d, HEX);
    }

    if (addr % 1024 == 0) {
      Serial.print("... ");
      Serial.println(addr, HEX);
    }
  }  
  Serial.println("End");
*/

  /* Program bit 0 to all zeroes */
  /*
  Serial.println("Writing");
  delay(100);
  data_bus_direction(DATA_OUT);
  digitalWrite(EPROM_VCC, HIGH);
  delay(100);
  digitalWrite(EPROM_VPP, HIGH);
  set_address(5);
  data_bus_write(0xdb7e);

  _delay_us(1000);
  digitalWrite(EPROM_CE, LOW);
  _delay_us(50);
  digitalWrite(EPROM_CE, HIGH);
  delay(100);
  
  digitalWrite(EPROM_VPP, LOW);
  delay(100);
  digitalWrite(EPROM_VCC, LOW);

  delay(100);
*/
/*
  Serial.println("Read back");
  data_bus_direction(INPUT);
  
  long int addr_max = (long int)256 * 1024;

  for (long int addr = 0; addr < 16; addr++) {
    set_address(addr);
    delay(1);
    digitalWrite(EPROM_CE, LOW);
    delay(1);
    digitalWrite(EPROM_OE, LOW);
    delay(1);
    unsigned int d = data_bus_value();
  
    digitalWrite(EPROM_CE, HIGH);
    digitalWrite(EPROM_OE, HIGH);

    Serial.print(addr, HEX);
    Serial.print(": ");
    Serial.println(d, HEX);
  }  
  Serial.println("End");
  
  while(1) {
  }
*/


}
