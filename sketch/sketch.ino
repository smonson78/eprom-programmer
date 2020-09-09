int EPROM_CE = 2;
int EPROM_OE = 3;
int EPROM_A0 = 62;
int EPROM_VPP = 7;
int EPROM_VCC = 6;

#define EPROM_DATA_LOW_PIN PINA
#define EPROM_DATA_HIGH_PIN PINC
#define EPROM_DATA_LOW_PORT PORTA
#define EPROM_DATA_HIGH_PORT PORTC
#define EPROM_DATA_LOW_DDR DDRA
#define EPROM_DATA_HIGH_DDR DDRC

int TRANSFERRING_LED = 54; // Arduino pin name A0

// This adjustment is for inaccurancy in the voltage divider
float analogue_scaling_factor = 1.0179;

// current data bus direction
typedef enum { DATA_IN, DATA_OUT } data_bus_direction_t;
data_bus_direction_t data_bus_dir;

// Current address page (everything but the lowest 8 bits)
// This is to minimise the amount of time it takes to change addresses
uint32_t addr_page;

/* Current command */
#define CMDLEN 16
char cmdbuf[CMDLEN + 1];

#define DATALEN 512
uint16_t databuf[DATALEN];
uint16_t dataused;

/* Pointer to next operand */
uint8_t cmdptr;

void led_on() {
  digitalWrite(TRANSFERRING_LED, HIGH);
}

void led_off() {
  digitalWrite(TRANSFERRING_LED, LOW);
}

void setup() {
  pinMode(EPROM_OE, OUTPUT);
  pinMode(EPROM_CE, OUTPUT);
  pinMode(EPROM_VCC, OUTPUT);
  pinMode(EPROM_VPP, OUTPUT);

  pinMode(TRANSFERRING_LED, OUTPUT);

  // Address
  // Least-significant eight lines, which are all in one port (PORTK)
  //for (int pin = EPROM_A0; pin < EPROM_A0 + 8; pin++) {
  //  pinMode(pin, OUTPUT);
  //DDRK = 0xff;
  pinMode(A8, OUTPUT);
  pinMode(A9, OUTPUT);
  pinMode(A10, OUTPUT);
  pinMode(A11, OUTPUT);
  pinMode(A12, OUTPUT);
  pinMode(A13, OUTPUT);
  pinMode(A14, OUTPUT);
  pinMode(A15, OUTPUT);
  
  //}
  // Upper lines
  //for (int pin = 44; pin <= 53; pin++) {
  //  pinMode(pin, OUTPUT);
  //}
  pinMode(53, OUTPUT);
  pinMode(52, OUTPUT);
  pinMode(51, OUTPUT);
  pinMode(50, OUTPUT);
  pinMode(49, OUTPUT);
  pinMode(48, OUTPUT);
  pinMode(47, OUTPUT);
  pinMode(46, OUTPUT);
  pinMode(45, OUTPUT);
  pinMode(44, OUTPUT);
  

  // Address pins are:
  // a0-a7 = 62-69 (PORTK)
  // a8 = 53
  // a9 = 52
  // a10 = 51
  // a11 = 50
  // a12 = 49
  // a13 = 48
  // a14 = 47
  // a15 = 46
  // a16 = 45
  // a17 = 44

  addr_page = 0;

  data_bus_direction(DATA_IN);

  // OE and CE are active-low
  digitalWrite(EPROM_OE, HIGH);
  digitalWrite(EPROM_CE, HIGH);
  digitalWrite(EPROM_VCC, LOW);
  digitalWrite(EPROM_VPP, LOW);

  // Allow time for voltages to settle
  delay(100);
  Serial.begin(57600);

  dataused = 0;
  
}

void data_bus_direction(data_bus_direction_t dir) {
  // Already set the right way - do nothing
  if (data_bus_dir == dir) {
    return;
  }

  data_bus_dir = dir;

  EPROM_DATA_LOW_DDR = dir == DATA_IN ? 0 : 0xff;
  EPROM_DATA_HIGH_DDR = dir == DATA_IN ? 0 : 0xff;

  // In order to disable pullups
  if (dir == DATA_IN) {
    EPROM_DATA_LOW_PORT = 0;
    EPROM_DATA_HIGH_PORT = 0;
  }
}

void set_address(uint32_t addr) {
  uint32_t new_addr_page = addr >> 8;

  // Change to a new page if needed
  //if (new_addr_page != addr_page) {
    digitalWrite(53, addr & (1 << 8) ? HIGH : LOW);
    digitalWrite(52, addr & (1 << 9) ? HIGH : LOW);
    digitalWrite(51, addr & (1 << 10) ? HIGH : LOW);
    digitalWrite(50, addr & (1 << 11) ? HIGH : LOW);
    digitalWrite(49, addr & (1 << 12) ? HIGH : LOW);
    digitalWrite(48, addr & (1 << 13) ? HIGH : LOW);
    digitalWrite(47, addr & (1 << 14) ? HIGH : LOW);
    digitalWrite(46, addr & (1 << 15) ? HIGH : LOW);
    digitalWrite(45, addr & (1 << 16) ? HIGH : LOW);
    digitalWrite(44, addr & (1 << 17) ? HIGH : LOW);
    // TODO make this into a loop
    
    addr_page = new_addr_page;
  //}

  // a0 - a7
  PORTK = addr & 0xff;
}

unsigned int data_bus_value() {
  int acc;

  acc = EPROM_DATA_HIGH_PIN;
  acc <<= 8;
  acc |= EPROM_DATA_LOW_PIN;
  
  return acc;
}

void data_bus_write(unsigned int val) {
  EPROM_DATA_HIGH_PORT = val >> 8;
  EPROM_DATA_LOW_PORT = val & 0xff;
}

float getVppVoltage() {
  float temp = analogRead(A2);
  // 5V is the analogue input max scale
  temp *= 5;
  // 1/3.2 is the voltage divider factor (1K ohm / 2.2K ohm)
  temp *= 3.2;
  temp /= 1023;
  temp *= analogue_scaling_factor;
  return temp;
}

/* Be cafeul not to call this without setting the data bus direction first. */
uint16_t read_eprom(uint32_t addr) {
    set_address(addr);
    _delay_us(100);
    digitalWrite(EPROM_CE, LOW);
    _delay_us(100);
    digitalWrite(EPROM_OE, LOW);
    _delay_us(100);
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
  delay(1);

  led_on();
  for (uint32_t addr = start; addr < start + len; addr++) {
    uint16_t val = read_eprom(addr);

    Serial.print(addr, HEX);
    Serial.print(": ");
    Serial.println(val, HEX);
  }    
  led_off();
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

  // Wait for VPP to reach the target 12.5V
  float voltage = getVppVoltage();
  while (voltage < 12.4 || voltage > 12.6) {
    delay(1);
    voltage = getVppVoltage();
  }
  
  set_address(addr);
  data_bus_write(value);

  // Wait for tAS, tDS, tVPS, tVCS (uS)
  _delay_us(100);

  // Pulse CE for exactly 50uS
  digitalWrite(EPROM_CE, LOW);
  _delay_us(50);
  digitalWrite(EPROM_CE, HIGH);

  // Wait for tDH (data hold time)
  _delay_us(100);

  // Can now lower programming voltages and let them settle
  digitalWrite(EPROM_VPP, LOW);
  digitalWrite(EPROM_VCC, LOW);

  // Wait for programming voltage to dissipate
  voltage = getVppVoltage();
  while (voltage > 0.1) {
    delay(1);
    voltage = getVppVoltage();
  }
  data_bus_direction(DATA_IN);
}

uint8_t getDataBuffer(uint16_t size) {
  dataused = 0;
  uint16_t bytecount = 0;
  uint16_t lastbyte;
  uint16_t timeout = 0;

  led_on();
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
        led_off();
        return 0;
      }
    } else {
      delay(10);
      if (timeout++ == 1000) {
        dataused = 0;
        led_off();
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
  // Wait for VPP to reach the target 12.5V
  float voltage = getVppVoltage();
  while (voltage < 12.4 || voltage > 12.6) {
    delay(1);
    voltage = getVppVoltage();
  }

  for (uint32_t count = 0; count < size; count++) {
    set_address(addr + count);
    data_bus_write(databuf[count]);
  
    // Wait for tAS, tDS, tVPS, tVCS (uS)
    _delay_us(100);
  
    // Pulse CE for exactly 50uS
    digitalWrite(EPROM_CE, LOW);
    _delay_us(50);
    digitalWrite(EPROM_CE, HIGH);
  
    // Wait for tDH (data hold time)
    _delay_us(100);
  }

  // Can now lower programming voltages and let them settle
  digitalWrite(EPROM_VPP, LOW);
  digitalWrite(EPROM_VCC, LOW);

  delay(10);
  
  // Wait for programming voltage to dissipate
  voltage = getVppVoltage();
  while (voltage > 0.1) {
    delay(1);
    voltage = getVppVoltage();
  }
  
  data_bus_direction(DATA_IN);
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

void doBufCRCCmd() {
  uint32_t size = getCmdAddr(cmdptr);

  uint16_t crc = 0;
  for (uint32_t addr = 0; addr < size; addr++) {
    uint16_t operand = databuf[addr];
    crc = crc16(crc, operand >> 8);
    crc = crc16(crc, operand & 0xff);
  }
  
  Serial.print("# BUFCRC ");
  Serial.print(size);
  Serial.print(" ");
  Serial.println(crc, HEX);
}

uint16_t bit_lookup[16] = {
  (1 << 0),
  (1 << 1),
  (1 << 2),
  (1 << 3),
  (1 << 4),
  (1 << 5),
  (1 << 6),
  (1 << 7),
  (1 << 8),
  (1 << 9),
  (1 << 10),
  (1 << 11),
  (1 << 12),
  (1 << 13),
  (1 << 14),
  (1 << 15),
};

void doCheckBlankCmd() {
  uint32_t size = getCmdAddr(cmdptr);

  Serial.print("# CHECK ");
  Serial.println(size);
  
  data_bus_direction(DATA_IN);
  uint32_t bits = 0;
  for (uint32_t addr = 0; addr < size; addr++) {
    uint16_t operand = read_eprom(addr);

    // Count non-blank bits
    for (uint8_t bit = 0; bit < 16; bit++) {
      if (~operand & bit_lookup[bit]) {
        bits++;
      }
    }

    // Display progress
    if (addr > 0 && addr % 1024 == 0) {
      Serial.print(addr, HEX);
      Serial.print(": ");
      Serial.println(bits, HEX);
    }
  }
  
  Serial.print("# BITS ");
  Serial.print(size);
  Serial.print(" ");
  Serial.println(bits);
}

void doDebugCmd() {
  Serial.print("# buffer contains ");
  Serial.print(dataused, 10);
  Serial.println(" words.");
  for (uint16_t i = 0; i < dataused; i++) {
    Serial.print(i, 16);
    Serial.print(": ");
    Serial.println(databuf[i], 16);
  }
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

void doTestVpp() {
  float temp;
  
  Serial.println("# TEST VPP");
  uint16_t log[128];
  digitalWrite(EPROM_VPP, LOW);
  delay(50);
  Serial.print("Low voltage: ");
  Serial.print(getVppVoltage());
  Serial.println(" V");

  digitalWrite(EPROM_VPP, HIGH);
  delay(50);

  Serial.print("High voltage: ");
  Serial.print(getVppVoltage());
  Serial.println(" V");

  digitalWrite(EPROM_VPP, LOW);
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
    case 'h':
      cmdptr++;
      doBufCRCCmd();
      break;      
    case 'l':
      cmdptr++;
      doCheckBlankCmd();
      break;      
    case 's':
      cmdptr++;
      doShowCmd();
      break;
    case 'd':
      cmdptr++;
      doDebugCmd();
      break;

    // Test VPP voltage envelope
    case 'v':
      doTestVpp();
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
}
