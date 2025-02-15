// Copyright (c) 2023 Trevor Makes

#include "core/io.hpp"
#include "core/io/bus.hpp"
#include "core/cli.hpp"
#include "core/mon.hpp"
#include "core/mon/z80.hpp"

using core::io::ActiveLow;
using core::io::ActiveHigh;
using core::io::BitExtend;
using core::io::WordExtend;
using core::io::RightAlign;
using core::io::Latch;
using core::io::PortBus;

// Arduino Nano and Uno
#ifdef __AVR_ATmega328P__

// Create Port* wrappers around AVR ports B, C, D
CORE_PORT(B)
CORE_PORT(C)
CORE_PORT(D)

//                        __.___.__ 
//            * RX  - 0  |         | A5 C5 - MSB Latch Enable (active high)
//            * TX  - 1  |         | A4 C4 - LSB Latch Enable (active high)
// OutputLatch/ PD2 - 2  |         | A3 C3 - Bus Read Enable (active low)
// Z80 Clock    PD3 - 3  |         | A2 C2 - Bus Write Enable (active low)
//      Data 4  PD4 - 4  |         | A1 c1 - Z80 RESET
//      Data 5  PD5 - 5  | Adafruit| A0 C0 - Z80 BUSREQ
//      Data 6  PD6 - 6  | Metro   | Aref
//      Data 7  PD7 - 7  |   Mini  | Vin
//      Data 0  PB0 - 8  |         | Gnd
//      Data 1  PB1 - 9  |         | Gnd
//      Data 2  PB2 - 10 |         | 5V
//      Data 3  PB3 - 11 |   ___   | 3.3V
//      *       PB4 - 12 |  |USB|  | Reset for UNO
//       Halt/  PB5 - 13 |__|___|__| USB 5V
// * unused digital pins

//                     __.___.__ 
//         * RX  - 0  |         | x 
//         * TX  - 1  |         | x 
// Output Enable - D2 |         | x
//     Z80 Clock - D3 |         | x
//        Data 4 - D4 |         | C5 - MSB Latch Enable (active high)
//        Data 5 - D5 |         | C4 - LSB Latch Enable (active high)
//        Data 6 - D6 | Arduino | C3 - Bus Read Enable (active low)
//        Data 7 - D7 |   NANO  | C2 - Bus Write Enable (active low)
//        Data 0 - B0 |         | C1 - Z80 RESET
//        Data 1 - B1 |         | C0 - Z80 BUSREQ
//        Data 2 - B2 |   ___   | x
//        Data 3 - B3 |  |USB|  | x
//             * - B4 |__|___|__| B5 - Z80 HALT
// * unused digital pins

// 8-bit data bus [D7 D6 D5 D4 B3 B2 B1 B0]
using DataPort = BitExtend<PortD::Mask<0xF0>, PortB::Mask<0x0F>>;

// Latch upper and lower bytes of 16-bit address from data port
using MSBLatch = ActiveHigh<PortC::Bit<5>>;
using LSBLatch = ActiveHigh<PortC::Bit<4>>;
using OutputEnable = ActiveLow<PortD::Bit<2>>;
using AddressMSB = Latch<DataPort, MSBLatch, OutputEnable>;
using AddressLSB = Latch<DataPort, LSBLatch, OutputEnable>;
using AddressPort = WordExtend<AddressMSB, AddressLSB>;

// Bus control lines
using ReadEnable = ActiveLow<PortC::Bit<3>>;
using WriteEnable = ActiveLow<PortC::Bit<2>>;

using Z80Reset = ActiveLow<PortC::Bit<1>>;
using Z80BusReq = ActiveLow<PortC::Bit<0>>;
using Z80Halt = ActiveLow<PortB::Bit<5>>;

struct Z80Clock {
  // uIO types for Timer2 registers
  CORE_REG(TCCR2A)
  CORE_REG(TCCR2B)
  CORE_REG(OCR2A)
  CORE_REG(OCR2B)
  CORE_REG(TIFR2)

  // Aliases for Timer2 bitfields
  using RegCOM2B = RightAlign<RegTCCR2A::Mask<0x30>>;
  using RegCS2 = RegTCCR2B::Mask<0x07>;
  using RegWGM2 = BitExtend<RegTCCR2B::Bit<WGM22>, RegTCCR2A::Mask<0x03>>;
  using RegOC2B = PortD::Bit<3>;
  using RegOCF2B = RegTIFR2::Bit<OCF2B>;

  static void config() {
    RegWGM2::write(7); // select fast PWM mode w/ OCR2A top
    RegCOM2B::write(3); // clear OC2B at OCR2A, set OC2B at OCR2B
    RegOC2B::config_output(); // GPIO must be in output mode
    RegOCR2A::write(1); // count that resets timer, the clock period
    RegOCR2B::write(0); // count that toggles OC2B (must be <= OCR2A)
    RegCS2::write(1); // 0 to stop, 1 for no prescaler, 2 for /8, etc
    RegOCR2A::write(1); // count that resets timer, the clock period 
    // 0 for half crystal speed, (16 / 2 = 8Mhz)
    // 1 gives 1/4 crystal speed, confirmed with oscilloscope 4Mhz ( 16 / 2 / 2 = 4)
    // 2 gives scope reading of 2.67Mhz (16 / (2+1) / 2 = 2.66667)
    // 3 gives scope reading of 2.00Mhz (16 / (3+1) / 2 = 2.0)
    // 4 gives scope reading of 1.6Mhz  (16 / (4+1) / 2 = 1.6)
    // 5 gives scope reading of 1.33 Mhz (16 / (5+1) / 2 = 1.33333)
    // 6 gives scope reading of 1.16 Mhz (16 / (6+1) / 2 = 1.143 )
    // 7 gives scope reading of 1 Mhz   (16 / (7+1) / 2 = 1.0 )


    RegOCR2B::write(0); // count that toggles OC2B (must be <= OCR2A) - with 0 the pin toggles on rollover or count reset
    RegCS2::write(1); // 0 to stop, 1 for no pre-scaler, 2 for /8, 3 for /32, 4 for / 64, 5 for / 128, 6 for / 256, 7 for / 1024  
  }

  // Wait for N positive clock edges
  template <uint8_t N = 1>
  static void wait_ticks() {
    if (N > 0) {
      RegOCF2B::set(); // clear compare flag
      while (RegOCF2B::is_clear()) {} // wait for clock edge
      wait_ticks<N - 1>();
    }
  }

  static void start() { RegCS2::write(1); }
  static void stop() { RegCS2::write(0); }
};

#else
#error Need to provide configuration for current platform. See __AVR_ATmega328P__ configuration above.
#endif

struct Bus : PortBus<AddressPort, DataPort, ReadEnable, WriteEnable> {
  // Override read_bus to reconfigure DataPort to latch address before reading
  static DATA_TYPE read_bus(ADDRESS_TYPE addr) {
    // Latch address from data port
    DataPort::config_output();
    AddressPort::write(addr);
    // Begin read sequence
    DataPort::config_input();
    ReadEnable::enable();
    // Need at least one cycle delay for AVR read latency
    // Second delay cycle has been sufficient for SRAM/EEPROM with tOE up to 70ns
    core::util::nop<2>(); // insert two NOP delay cycles
    const DATA_TYPE data = DataPort::read();
    // End read sequence
    ReadEnable::disable();
    return data;
  }
};

using core::serial::StreamEx;
using CLI = core::cli::CLI<>;
using core::cli::Args;

// Create command line interface around Arduino Serial
StreamEx serialEx(Serial);
CLI serialCli(serialEx);

// Define interface for core::mon function templates
struct API : core::mon::Base<API> {
  static StreamEx& get_stream() { return serialEx; }
  static CLI& get_cli() { return serialCli; }
  using BUS = Bus;
};

void setup() {
  // Start with Z80 in BUSREQ state to keep RS and WR floating
  // Previously used the RESET state, but this drives RS and WR high instead
  Z80BusReq::config_output();
  Z80BusReq::enable();

  Z80Reset::config_output();
  Z80Halt::config_input();
  Z80Clock::config();

  // Establish serial connection with computer
  Serial.begin(9600);
  while (!Serial) {}
  
  Bus::config_float();  //float the latches and data from Arduino side (read and write should be floated until needed)

}

void set_baud(Args args) {
  CORE_EXPECT_UINT(API, uint32_t, baud, args, return)
  // NOTE in PlatformIO terminal, type `ctrl-t b` to enter matching baud rate
  Serial.print(F("Type: ctrl-t b "));
  Serial.println(baud);
  // https://forum.arduino.cc/t/change-baud-rate-at-runtime/368191
  Serial.flush();
  Serial.begin(baud);
  while (Serial.available()) Serial.read();
}

void run_until_halt(Args args) {
  Bus::config_float();
  // Hold RESET low for 3 clock pulses
  Z80Reset::enable();
  Z80Clock::wait_ticks<3>();
  Z80Reset::disable();
  // Release BUSREQ to let program run
  Z80BusReq::disable();
  while (!Z80Halt::is_enabled()) {}
  Z80BusReq::enable();
}

void loop() {
  static const core::cli::Command commands[] = {
    { "baud", set_baud },
    // Assembler commands
    { "asm", core::mon::z80::cmd_asm<API> },
    { "dasm", core::mon::z80::cmd_dasm<API> },
    { "label", core::mon::cmd_label<API> },
    { "run", run_until_halt },
    // Memory monitor commands
    { "hex", core::mon::cmd_hex<API> },
    { "set", core::mon::cmd_set<API> },
    { "fill", core::mon::cmd_fill<API> },
    { "move", core::mon::cmd_move<API> },
    // Memory transfer commands
    { "export", core::mon::cmd_export<API> },
    { "import", core::mon::cmd_import<API> },
    { "verify", core::mon::cmd_verify<API> },
  };

  serialCli.run_once(commands);
  Bus::config_float();
  
}
